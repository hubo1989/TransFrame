import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers
import os.log

/// PaddleOCR engine implementation.
/// Communicates with PaddleOCR CLI for text recognition.
actor PaddleOCREngine {
    // MARK: - Properties

    /// Shared instance for PaddleOCR operations
    static let shared = PaddleOCREngine()

    /// Whether PaddleOCR is available on the system
    var isAvailable: Bool { PaddleOCRChecker.isAvailable }

    /// Get executable path from checker
    private var executablePath: String {
        PaddleOCRChecker.executablePath ?? "/usr/local/bin/paddleocr"
    }

    /// Maximum concurrent operations
    private var isProcessing = false
    /// FIFO queue of callers waiting for the in-flight recognition; local
    /// PaddleOCR runs a heavyweight Python process, so calls serialize.
    private var executionWaiters: [CheckedContinuation<Void, Never>] = []

    /// Waits for any in-flight recognition to finish instead of failing
    /// with `operationInProgress`.
    private func acquireExecutionSlot() async {
        guard isProcessing else {
            isProcessing = true
            return
        }
        await withCheckedContinuation { continuation in
            executionWaiters.append(continuation)
        }
    }

    /// Hands the slot to the next waiter, or frees it when nobody is queued.
    private func releaseExecutionSlot() {
        if let next = executionWaiters.first {
            executionWaiters.removeFirst()
            next.resume()
        } else {
            isProcessing = false
        }
    }

    // MARK: - Configuration

    /// PaddleOCR configuration options
    struct Configuration: Sendable {
        /// Recognition languages (ch, en for mixed Chinese-English)
        var languages: Set<Language>

        /// Minimum confidence threshold (0.0 to 1.0)
        var minimumConfidence: Float

        /// Whether to use GPU acceleration
        var useGPU: Bool

        /// Whether to use direction classification for rotated text
        var useDirectionClassify: Bool

        /// Detection model type
        var detectionModel: DetectionModel

        /// OCR mode: fast (ocr command) or precise (doc_parser VL-1.5)
        var mode: PaddleOCRMode

        /// Whether to use cloud API
        var useCloud: Bool

        /// Cloud API base URL
        var cloudBaseURL: String

        /// Cloud API key
        var cloudAPIKey: String

        /// Cloud API model ID
        var cloudModelId: String

        /// Local VL model directory (for vllm backend)
        var localVLModelDir: String

        static let `default` = Configuration(
            languages: [.chinese, .english],
            minimumConfidence: 0.0,
            useGPU: false,
            useDirectionClassify: true,
            detectionModel: .default,
            mode: .fast,
            useCloud: false,
            cloudBaseURL: "",
            cloudAPIKey: "",
            cloudModelId: "",
            localVLModelDir: ""
        )
    }

    /// Supported languages for PaddleOCR
    enum Language: String, CaseIterable, Sendable {
        case chinese = "ch"
        case english = "en"
        case french = "french"
        case german = "german"
        case korean = "korean"
        case japanese = "japan"

        /// CLI argument value for language
        var cliValue: String {
            switch self {
            case .chinese: return "ch"
            case .english: return "en"
            case .french: return "french"
            case .german: return "german"
            case .korean: return "korean"
            case .japanese: return "japan"
            }
        }

        /// Localized display name
        var localizedName: String {
            switch self {
            case .chinese: return NSLocalizedString("lang.chinese", comment: "")
            case .english: return NSLocalizedString("lang.english", comment: "")
            case .french: return NSLocalizedString("lang.french", comment: "")
            case .german: return NSLocalizedString("lang.german", comment: "")
            case .korean: return NSLocalizedString("lang.korean", comment: "")
            case .japanese: return NSLocalizedString("lang.japanese", comment: "")
            }
        }
    }

    /// Detection model types
    enum DetectionModel: String, Sendable {
        case `default`
        case server
        case mobile
    }

    // MARK: - Initialization

    private init() {}

    // MARK: - Public API

    /// Performs OCR on a CGImage with the specified configuration.
    /// - Parameters:
    ///   - image: The image to process
    ///   - config: OCR configuration (uses default if not specified)
    /// - Returns: OCRResult containing all recognized text
    /// - Throws: PaddleOCRError if recognition fails
    func recognize(
        _ image: CGImage,
        config: Configuration = .default
    ) async throws -> OCRResult {
        // Serialize behind any in-flight recognition (queued, not rejected)
        await acquireExecutionSlot()
        defer { releaseExecutionSlot() }
        try Task.checkCancellation()

        // Validate image
        guard image.width > 0 && image.height > 0 else {
            throw PaddleOCREngineError.invalidImage
        }

        // Downsample Retina captures once; everything downstream (temp file,
        // cloud upload, coordinate normalization) works against this image.
        let analysisImage = AnalysisImageEncoder.downsampled(image)
        let analysisSize = CGSize(width: analysisImage.width, height: analysisImage.height)

        // Use cloud API if configured
        if config.useCloud {
            return try await recognizeViaCloudAPI(image: analysisImage, config: config)
        }

        // Local mode - check availability
        guard isAvailable else {
            throw PaddleOCREngineError.notInstalled
        }

        // Save image to temporary file
        let tempURL = try saveImageToTempFile(analysisImage)

        defer {
            // Clean up temp file
            try? FileManager.default.removeItem(at: tempURL)
        }

        // Build PaddleOCR command arguments
        let arguments = buildArguments(config: config, imagePath: tempURL.path)

        // Execute PaddleOCR
        let result = try await executePaddleOCR(arguments: arguments)

        // Parse output
        let observations = try parsePaddleOCROutput(result, imageSize: analysisSize, mode: config.mode)

        // Filter by confidence
        let filteredTexts = observations.filter { $0.confidence >= config.minimumConfidence }

        return OCRResult(
            observations: filteredTexts,
            imageSize: analysisSize
        )
    }

    /// Performs OCR on a CGImage with default configuration.
    /// - Parameter image: The image to process
    /// - Returns: OCRResult containing all recognized text
    /// - Throws: PaddleOCRError if recognition fails
    func recognize(_ image: CGImage) async throws -> OCRResult {
        try await recognize(image, config: .default)
    }

    /// Performs OCR with specific languages.
    /// - Parameters:
    ///   - image: The image to process
    ///   - languages: Set of languages to recognize
    /// - Returns: OCRResult containing all recognized text
    /// - Throws: PaddleOCRError if recognition fails
    func recognize(
        _ image: CGImage,
        languages: Set<Language>
    ) async throws -> OCRResult {
        var config = Configuration.default
        config.languages = languages
        return try await recognize(image, config: config)
    }

    // MARK: - Cloud API

    /// Performs OCR via cloud API (OpenAI-compatible format for vllm)
    private func recognizeViaCloudAPI(image: CGImage, config: Configuration) async throws -> OCRResult {
        guard !config.cloudBaseURL.isEmpty else {
            throw PaddleOCREngineError.invalidConfiguration("Cloud API base URL is not configured")
        }

        // Build URL for OpenAI-compatible endpoint
        let trimmedBaseURL = config.cloudBaseURL
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        guard let baseURL = URL(string: trimmedBaseURL),
              baseURL.scheme != nil,
              baseURL.host != nil else {
            throw PaddleOCREngineError.invalidConfiguration("Invalid Cloud API base URL: \(config.cloudBaseURL)")
        }

        let apiURL: URL
        if trimmedBaseURL.hasSuffix("/v1") || trimmedBaseURL.contains("/v1/") {
            apiURL = baseURL.appendingPathComponent("chat/completions")
        } else {
            apiURL = baseURL
                .appendingPathComponent("v1")
                .appendingPathComponent("chat")
                .appendingPathComponent("completions")
        }

        // Convert image to base64 data URL
        let encoded: AnalysisImageEncoder.EncodedImage
        do {
            encoded = try AnalysisImageEncoder.png(image)
        } catch {
            throw PaddleOCREngineError.failedToSaveImage
        }
        let imageURL = encoded.dataURI

        // Build OpenAI-compatible request
        var request = URLRequest(url: apiURL)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        if !config.cloudAPIKey.isEmpty {
            request.setValue("Bearer \(config.cloudAPIKey)", forHTTPHeaderField: "Authorization")
        }

        // Build OpenAI chat completion format
        let modelName = config.cloudModelId.isEmpty ? "default" : config.cloudModelId
        let body: [String: Any] = [
            "model": modelName,
            "messages": [
                [
                    "role": "user",
                    "content": [
                        ["type": "text", "text": "Please recognize all text in this image and return the results in JSON format with 'text', 'confidence', and 'box' (x, y, width, height) for each text region."],
                        ["type": "image_url", "image_url": ["url": imageURL]]
                    ]
                ]
            ],
            "max_tokens": 4096
        ]
        do {
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        } catch {
            throw PaddleOCREngineError.invalidConfiguration("Failed to serialize request body: \(error.localizedDescription)")
        }

        Logger.ocr.info("PaddleOCR cloud API request to: \(apiURL.absoluteString)")

        // Execute request
        let (responseData, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse else {
            throw PaddleOCREngineError.cloudAPIError("Invalid HTTP response")
        }

        Logger.ocr.info("PaddleOCR cloud API response status: \(httpResponse.statusCode)")

        guard httpResponse.statusCode == 200 else {
            let errorMessage = String(data: responseData, encoding: .utf8) ?? "Unknown error"
            Logger.ocr.error("PaddleOCR cloud API error: \(errorMessage)")
            throw PaddleOCREngineError.cloudAPIError("Cloud API error: HTTP \(httpResponse.statusCode) - \(errorMessage)")
        }

        // Parse response
        return try parseCloudAPIResponse(responseData, imageSize: CGSize(width: image.width, height: image.height))
    }

    /// Parses cloud API response into OCRResult (OpenAI chat completion format)
    private func parseCloudAPIResponse(_ data: Data, imageSize: CGSize) throws -> OCRResult {
        guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw PaddleOCREngineError.cloudAPIError("Invalid JSON response")
        }

        Logger.ocr.debug("Parsing cloud API response")

        var observations: [OCRText] = []

        // Parse OpenAI chat completion format
        if let choices = json["choices"] as? [[String: Any]],
           let firstChoice = choices.first,
           let message = firstChoice["message"] as? [String: Any],
           let content = message["content"] as? String {
            // Try to parse JSON from the content
            if let jsonData = extractJSON(from: content),
               let results = try? JSONSerialization.jsonObject(with: jsonData) as? [[String: Any]] {
                // Parse structured OCR results
                for result in results {
                    guard let text = result["text"] as? String else { continue }
                    let confidence = Float(result["confidence"] as? Double ?? 1.0)

                    var boundingBox = CGRect.zero
                    if let box = result["box"] as? [String: Double] {
                        boundingBox = CGRect(
                            x: box["x"] ?? 0,
                            y: box["y"] ?? 0,
                            width: box["width"] ?? 0,
                            height: box["height"] ?? 0
                        )
                    }

                    observations.append(OCRText(
                        text: text,
                        boundingBox: boundingBox,
                        confidence: confidence
                    ))
                }
            } else {
                // Fallback: treat entire content as single text block
                observations.append(OCRText(
                    text: content,
                    boundingBox: CGRect(origin: .zero, size: imageSize),
                    confidence: 1.0
                ))
            }
        } else if let results = json["results"] as? [[String: Any]] {
            // Legacy format with direct results array
            for result in results {
                guard let text = result["text"] as? String else { continue }
                let confidence = Float(result["confidence"] as? Double ?? 1.0)

                var boundingBox = CGRect.zero
                if let box = result["box"] as? [String: Double] {
                    boundingBox = CGRect(
                        x: box["x"] ?? 0,
                        y: box["y"] ?? 0,
                        width: box["width"] ?? 0,
                        height: box["height"] ?? 0
                    )
                }

                observations.append(OCRText(
                    text: text,
                    boundingBox: boundingBox,
                    confidence: confidence
                ))
            }
        } else if let text = json["text"] as? String {
            // Simple text response
            observations.append(OCRText(
                text: text,
                boundingBox: CGRect(origin: .zero, size: imageSize),
                confidence: 1.0
            ))
        }

        Logger.ocr.info("Parsed \(observations.count) text observations from cloud API")
        return OCRResult(observations: observations, imageSize: imageSize)
    }

    /// Extracts JSON array from text content (handles markdown code blocks)
    private func extractJSON(from content: String) -> Data? {
        // Try to extract JSON from markdown code block
        if let range = content.range(of: "```json"),
           let endRange = content.range(of: "```", range: content.index(range.upperBound, offsetBy: 0)..<content.endIndex) {
            let jsonString = String(content[range.upperBound..<endRange.lowerBound])
                .trimmingCharacters(in: .whitespacesAndNewlines)
            return jsonString.data(using: .utf8)
        }

        // Try to find JSON array directly
        if let startIndex = content.firstIndex(of: "["),
           let endIndex = content.lastIndex(of: "]") {
            let jsonString = String(content[startIndex...endIndex])
            return jsonString.data(using: .utf8)
        }

        return nil
    }

    // MARK: - Private Methods

    /// Saves a CGImage to a temporary PNG file
    private func saveImageToTempFile(_ image: CGImage) throws -> URL {
        let tempDir = FileManager.default.temporaryDirectory
        let tempURL = tempDir.appendingPathComponent(
            "ocr_input_\(UUID().uuidString).png"
        )

        guard let destination = CGImageDestinationCreateWithURL(
            tempURL as CFURL,
            UTType.png.identifier as CFString,
            1,
            nil
        ) else {
            throw PaddleOCREngineError.failedToSaveImage
        }

        CGImageDestinationAddImage(destination, image, nil)

        guard CGImageDestinationFinalize(destination) else {
            throw PaddleOCREngineError.failedToSaveImage
        }

        return tempURL
    }

    /// Builds command line arguments for PaddleOCR
    private func buildArguments(config: Configuration, imagePath: String) -> [String] {
        switch config.mode {
        case .fast:
            // Fast mode: use ocr command (~1s)
            let langCode = config.languages.contains(.chinese) ? "ch" : "en"
            return [
                "ocr",
                "-i", imagePath,
                "--lang", langCode,
                "--use_angle_cls", config.useDirectionClassify ? "true" : "false"
            ]
        case .precise:
            // Precise mode: use doc_parser with VL-1.5
            var args = [
                "doc_parser",
                "-i", imagePath,
                "--pipeline_version", "v1.5",
                "--device", config.useGPU ? "gpu" : "cpu"
            ]

            // Use native backend with local model (vllm)
            if !config.localVLModelDir.isEmpty {
                // Expand tilde in path (e.g., ~/.paddlex -> /Users/xxx/.paddlex)
                let expandedPath = NSString(string: config.localVLModelDir).expandingTildeInPath
                args += [
                    "--vl_rec_backend", "native",
                    "--vl_rec_model_dir", expandedPath
                ]
            }

            return args
        }
    }

    /// Executes PaddleOCR with the given arguments
    private func executePaddleOCR(arguments: [String]) async throws -> String {
        let exePath = executablePath
        Logger.ocr.info("Executing: \(exePath) \(arguments.joined(separator: " "))")

        // Security: validate executable path doesn't contain shell metacharacters
        let dangerousChars = CharacterSet(charactersIn: ";|&$`\"'\\(){}[]!#~")
        if exePath.unicodeScalars.contains(where: { dangerousChars.contains($0) }) {
            throw PaddleOCREngineError.invalidConfiguration("Executable path contains unsafe characters: \(exePath)")
        }

        // Security: use Process with direct executableURL + arguments instead of shell
        // This prevents command injection since arguments aren't parsed by a shell
        let task = Process()
        task.executableURL = URL(fileURLWithPath: exePath)
        task.arguments = arguments

        task.environment = [
            "PATH": "\(NSHomeDirectory())/.pyenv/shims:\(NSHomeDirectory())/.pyenv/bin:/usr/local/bin:/usr/bin:/bin",
            "HOME": NSHomeDirectory(),
            "PYENV_ROOT": "\(NSHomeDirectory())/.pyenv",
            "PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK": "True"
        ]

        do {
            let runner = AsyncProcessRunner(process: task)
            Logger.ocr.debug("Process started, waiting...")
            let output = try await runner.run()
            Logger.ocr.debug("Process finished with exit code: \(output.terminationStatus)")

            var stdout = String(data: output.stdoutData, encoding: .utf8) ?? ""
            let stderr = String(data: output.stderrData, encoding: .utf8) ?? ""
            
            // PaddleOCR outputs result to stderr, extract JSON from it
            if stdout.isEmpty, let resultRange = stderr.range(of: "{'res':") {
                let resultStart = stderr[resultRange.lowerBound...]
                // Find the matching closing brace
                if let jsonEnd = findMatchingBrace(in: String(resultStart)) {
                    stdout = String(resultStart.prefix(jsonEnd + 1))
                    // Remove ANSI color codes
                    let ansiPattern = "\u{001B}\\[[0-9;]*m"
                    stdout = stdout.replacingOccurrences(of: ansiPattern, with: "", options: .regularExpression)
                    Logger.ocr.debug("Extracted result from stderr")
                }
            }
            
            Logger.ocr.debug("output length: \(stdout.count)")
            Logger.ocr.debug("output: \(stdout.prefix(1000))")

            let exitCode = output.terminationStatus
            if exitCode != 0 {
                let errorMsg = stderr.isEmpty ? "Exit code \(exitCode)" : stderr
                throw PaddleOCREngineError.recognitionFailed(underlying: errorMsg)
            }

            guard !stdout.isEmpty else {
                Logger.ocr.error("No result found in output")
                throw PaddleOCREngineError.invalidOutput
            }

            return stdout
        } catch let error as PaddleOCREngineError {
            throw error
        } catch is CancellationError {
            throw CancellationError()
        } catch {
            Logger.ocr.error("Error: \(error.localizedDescription)")
            throw PaddleOCREngineError.recognitionFailed(underlying: error.localizedDescription)
        }
    }
    
    nonisolated func findMatchingBrace(in string: String) -> Int? {
        var depth = 0
        var inSingleQuote = false
        var inDoubleQuote = false
        var escaped = false

        for (index, char) in string.enumerated() {
            if escaped {
                escaped = false
                continue
            }
            if char == "\\" && (inSingleQuote || inDoubleQuote) {
                escaped = true
                continue
            }
            if char == "'" && !inDoubleQuote {
                inSingleQuote.toggle()
                continue
            }
            if char == "\"" && !inSingleQuote {
                inDoubleQuote.toggle()
                continue
            }
            guard !inSingleQuote && !inDoubleQuote else { continue }

            if char == "{" {
                depth += 1
            } else if char == "}" {
                depth -= 1
                if depth == 0 { return index }
            }
        }
        return nil
    }

    /// Parses PaddleOCR output into OCRText observations
    private func parsePaddleOCROutput(_ output: String, imageSize: CGSize, mode: PaddleOCRMode) throws -> [OCRText] {
        var observations: [OCRText] = []

        guard let startIndex = output.firstIndex(of: "{"),
              let endIndex = output.lastIndex(of: "}") else {
            Logger.ocr.debug("No JSON found in output")
            return observations
        }

        let jsonLike = String(output[startIndex...endIndex])
        let cleanedJson = convertPythonDictToJson(jsonLike)

        Logger.ocr.debug("Cleaned JSON: \(cleanedJson.prefix(500))")

        guard let jsonData = cleanedJson.data(using: .utf8) else {
            Logger.ocr.error("Failed to convert cleaned JSON to data")
            return observations
        }

        // Try to parse JSON and log detailed error
        var json: [String: Any]?
        do {
            json = try JSONSerialization.jsonObject(with: jsonData) as? [String: Any]
        } catch {
            Logger.ocr.error("JSON parse error: \(error.localizedDescription)")
            // Log the problematic JSON (last 1000 chars to find the issue)
            if let jsonStr = String(data: jsonData, encoding: .utf8) {
                Logger.ocr.error("JSON end portion: ...\(jsonStr.suffix(500))")
            }
            return observations
        }

        guard let json = json else {
            Logger.ocr.error("Failed to parse JSON as dictionary")
            return observations
        }

        guard let res = json["res"] as? [String: Any] else {
            Logger.ocr.error("No 'res' key in JSON. Keys: \(json.keys.joined(separator: ", "))")
            return observations
        }

        switch mode {
        case .fast:
            // Fast mode: parse rec_texts format
            observations = try parseFastModeOutput(res: res, imageSize: imageSize)
        case .precise:
            // Precise mode: parse doc_parser output format: parsing_res_list
            observations = try parsePreciseModeOutput(res: res, imageSize: imageSize)
        }

        return observations
    }

    /// Parse fast mode output (ocr command)
    private func parseFastModeOutput(res: [String: Any], imageSize: CGSize) throws -> [OCRText] {
        var observations: [OCRText] = []

        // Fast mode output has parallel arrays: rec_texts, rec_scores, rec_boxes
        guard let recTexts = res["rec_texts"] as? [String] else {
            Logger.ocr.error("No rec_texts found in fast mode output. Keys: \(res.keys.joined(separator: ", "))")
            return observations
        }

        // Get rec_boxes and rec_scores (optional)
        let recBoxes = res["rec_boxes"] as? [[Double]]
        let recScores = res["rec_scores"] as? [Double]

        Logger.ocr.info("Found \(recTexts.count) text blocks from fast mode")

        for (index, text) in recTexts.enumerated() {
            guard !text.isEmpty else { continue }

            // Get bounding box from rec_boxes (format: [[x1, y1, x2, y2], ...])
            var boundingBox: CGRect
            if let boxes = recBoxes, index < boxes.count {
                let box = boxes[index]
                if box.count >= 4 {
                    let x = CGFloat(box[0])
                    let y = CGFloat(box[1])
                    let x2 = CGFloat(box[2])
                    let y2 = CGFloat(box[3])
                    boundingBox = CGRect(
                        x: x / imageSize.width,
                        y: y / imageSize.height,
                        width: (x2 - x) / imageSize.width,
                        height: (y2 - y) / imageSize.height
                    )
                } else {
                    boundingBox = CGRect(x: 0, y: CGFloat(index) * 0.1, width: 1, height: 0.1)
                }
            } else {
                // Fallback: stack vertically
                boundingBox = CGRect(x: 0, y: CGFloat(index) * 0.1, width: 1, height: 0.1)
            }

            // Get confidence from rec_scores
            let confidence: Float
            if let scores = recScores, index < scores.count {
                confidence = Float(scores[index])
            } else {
                confidence = 0.9
            }

            let observation = OCRText(
                text: text,
                boundingBox: boundingBox,
                confidence: confidence
            )
            observations.append(observation)
            Logger.ocr.debug("Fast mode block: '\(text)', box: \(String(describing: boundingBox))")
        }

        return observations
    }

    /// Parse precise mode output (doc_parser VL-1.5)
    private func parsePreciseModeOutput(res: [String: Any], imageSize: CGSize) throws -> [OCRText] {
        var observations: [OCRText] = []

        // Log all keys in res for debugging
        Logger.ocr.info("Precise mode res keys: \(res.keys.joined(separator: ", "))")

        guard let parsingResList = res["parsing_res_list"] as? [[String: Any]] else {
            Logger.ocr.error("No parsing_res_list found in res. Available keys: \(res.keys.joined(separator: ", "))")
            // Try to log the raw res for debugging
            if let resData = try? JSONSerialization.data(withJSONObject: res),
               let resStr = String(data: resData, encoding: .utf8) {
                Logger.ocr.debug("Raw res content: \(resStr.prefix(1000))")
            }
            return observations
        }

        Logger.ocr.info("Found \(parsingResList.count) blocks from doc_parser")

        for (index, block) in parsingResList.enumerated() {
            guard let text = block["block_content"] as? String else {
                continue
            }

            // Skip non-text blocks (charts, seals, images, etc.)
            if let label = block["block_label"] as? String {
                let skipLabels = ["chart", "seal", "image", "table", "figure"]
                if skipLabels.contains(where: { label.lowercased().contains($0) }) {
                    Logger.ocr.debug("Skipping non-text block: \(label)")
                    continue
                }
            }

            var boundingBox: CGRect
            if let bbox = block["block_bbox"] as? [Double], bbox.count >= 4 {
                let x = CGFloat(bbox[0])
                let y = CGFloat(bbox[1])
                let x2 = CGFloat(bbox[2])
                let y2 = CGFloat(bbox[3])
                boundingBox = CGRect(
                    x: x / imageSize.width,
                    y: y / imageSize.height,
                    width: (x2 - x) / imageSize.width,
                    height: (y2 - y) / imageSize.height
                )
            } else {
                boundingBox = CGRect(x: 0, y: CGFloat(index) * 0.1, width: 1, height: 0.1)
            }

            // doc_parser doesn't provide confidence scores per block, use default
            let confidence: Float = 0.9

            let observation = OCRText(
                text: text,
                boundingBox: boundingBox,
                confidence: confidence
            )
            observations.append(observation)
            Logger.ocr.debug("Block: '\(text)', box: \(String(describing: boundingBox))")
        }

        return observations
    }

    nonisolated func convertPythonDictToJson(_ pythonDict: String) -> String {
        var result = convertPythonLiteralsToJson(pythonDict)
        result = convertNumpyArraysToJson(result)

        // Fix float format: "8." -> "8.0", "-5." -> "-5.0" (valid JSON)
        let floatPattern = #"(-?\d+)\.\s*([,\]\}])"#
        if let regex = try? NSRegularExpression(pattern: floatPattern) {
            let range = NSRange(result.startIndex..., in: result)
            result = regex.stringByReplacingMatches(in: result, options: [], range: range, withTemplate: "$1.0$2")
        }

        return result
    }

    nonisolated private func convertPythonLiteralsToJson(_ input: String) -> String {
        var output = String()
        output.reserveCapacity(input.count)

        var inSingleQuote = false
        var inDoubleQuote = false
        var escaped = false
        var index = input.startIndex

        func isIdentifierChar(_ c: Character) -> Bool {
            c.isLetter || c.isNumber || c == "_"
        }

        while index < input.endIndex {
            let char = input[index]

            if inSingleQuote {
                if escaped {
                    if char == "'" {
                        output.append("'")
                    } else if char == "\"" {
                        output.append("\\\"")
                    } else {
                        output.append("\\")
                        output.append(char)
                    }
                    escaped = false
                } else if char == "\\" {
                    escaped = true
                } else if char == "'" {
                    output.append("\"")
                    inSingleQuote = false
                } else if char == "\"" {
                    output.append("\\\"")
                } else {
                    output.append(char)
                }
                index = input.index(after: index)
            } else if inDoubleQuote {
                if escaped {
                    output.append(char)
                    escaped = false
                } else if char == "\\" {
                    output.append(char)
                    escaped = true
                } else if char == "\"" {
                    output.append(char)
                    inDoubleQuote = false
                } else {
                    output.append(char)
                }
                index = input.index(after: index)
            } else {
                if char == "'" {
                    output.append("\"")
                    inSingleQuote = true
                    index = input.index(after: index)
                } else if char == "\"" {
                    output.append("\"")
                    inDoubleQuote = true
                    index = input.index(after: index)
                } else if isIdentifierChar(char) {
                    let wordStart = index
                    var wordEnd = input.index(after: index)
                    while wordEnd < input.endIndex && isIdentifierChar(input[wordEnd]) {
                        wordEnd = input.index(after: wordEnd)
                    }
                    let word = input[wordStart..<wordEnd]
                    switch word {
                    case "None":
                        output.append("null")
                    case "True":
                        output.append("true")
                    case "False":
                        output.append("false")
                    default:
                        output.append(contentsOf: word)
                    }
                    index = wordEnd
                } else {
                    output.append(char)
                    index = input.index(after: index)
                }
            }
        }

        return output
    }
    
    nonisolated private func convertNumpyArraysToJson(_ input: String) -> String {
        var result = input
        var searchStart = result.startIndex
        
        while let arrayStart = result.range(of: "array(", range: searchStart..<result.endIndex) {
            var depth = 1
            var current = arrayStart.upperBound
            
            while current < result.endIndex && depth > 0 {
                let char = result[current]
                if char == "(" {
                    depth += 1
                } else if char == ")" {
                    depth -= 1
                }
                current = result.index(after: current)
            }
            
            guard depth == 0 else {
                searchStart = arrayStart.upperBound
                continue
            }
            
            let arrayContent = String(result[arrayStart.upperBound..<result.index(before: current)])
            let extracted = extractArrayContent(from: arrayContent)
            
            result.replaceSubrange(arrayStart.lowerBound..<current, with: extracted)
            searchStart = result.index(arrayStart.lowerBound, offsetBy: extracted.count, limitedBy: result.endIndex) ?? result.endIndex
        }
        
        return result
    }
    
    nonisolated private func extractArrayContent(from arrayContent: String) -> String {
        var content = arrayContent

        // Remove shape and dtype info
        if let shapeRange = content.range(of: ", shape=") {
            content = String(content[..<shapeRange.lowerBound])
        }
        if let dtypeRange = content.range(of: ", dtype=") {
            content = String(content[..<dtypeRange.lowerBound])
        }

        content = content.trimmingCharacters(in: .whitespacesAndNewlines)

        if content.hasPrefix("[") {
            // Remove ellipsis (numpy truncation indicator)
            content = content.replacingOccurrences(of: "...", with: "")
            // Remove newlines and extra spaces
            content = content.replacingOccurrences(of: "\n", with: "")
            content = content.replacingOccurrences(of: " ", with: "")
            // Clean up multiple commas and brackets
            while content.contains(",,") {
                content = content.replacingOccurrences(of: ",,", with: ",")
            }
            content = content.replacingOccurrences(of: "[,", with: "[")
            content = content.replacingOccurrences(of: ",]", with: "]")
            // Handle edge case of empty nested arrays - return early
            return content
        }

        return "[]"
    }

    private func parseBoundingBox(from line: String, imageSize: CGSize) -> CGRect? {
        nil
    }
}

// MARK: - PaddleOCR Engine Errors

/// Errors that can occur during PaddleOCR operations
enum PaddleOCREngineError: LocalizedError, Sendable {
    /// PaddleOCR is not installed
    case notInstalled

    /// The provided image is invalid or empty
    case invalidImage

    /// Failed to save image to temporary file
    case failedToSaveImage

    /// Text recognition failed with an underlying error
    case recognitionFailed(underlying: String)

    /// Invalid output from PaddleOCR
    case invalidOutput

    /// Invalid configuration
    case invalidConfiguration(String)

    /// Cloud API error
    case cloudAPIError(String)

    var errorDescription: String? {
        switch self {
        case .notInstalled:
            return NSLocalizedString(
                "error.paddleocr.not.installed",
                comment: "PaddleOCR is not installed"
            )
        case .invalidImage:
            return NSLocalizedString(
                "error.ocr.invalid.image",
                comment: "The provided image is invalid or empty"
            )
        case .failedToSaveImage:
            return NSLocalizedString(
                "error.paddleocr.save.image",
                comment: "Failed to save image for processing"
            )
        case .recognitionFailed:
            return NSLocalizedString(
                "error.ocr.failed",
                comment: "Text recognition failed"
            )
        case .invalidOutput:
            return NSLocalizedString(
                "error.paddleocr.invalid.output",
                comment: "Invalid output from PaddleOCR"
            )
        case .invalidConfiguration(let message):
            return message
        case .cloudAPIError(let message):
            return message
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .notInstalled:
            return NSLocalizedString(
                "error.paddleocr.not.installed.recovery",
                comment: "Install PaddleOCR using: pip install paddleocr"
            )
        case .invalidImage:
            return NSLocalizedString(
                "error.ocr.invalid.image.recovery",
                comment: "Provide a valid image with non-zero dimensions"
            )
        case .failedToSaveImage:
            return NSLocalizedString(
                "error.paddleocr.save.image.recovery",
                comment: "Check disk space and permissions"
            )
        case .recognitionFailed(let message):
            return message
        case .invalidOutput:
            return NSLocalizedString(
                "error.paddleocr.invalid.output.recovery",
                comment: "Ensure PaddleOCR is correctly installed"
            )
        case .invalidConfiguration:
            return NSLocalizedString(
                "error.paddleocr.invalid.config.recovery",
                comment: "Check your PaddleOCR cloud API settings"
            )
        case .cloudAPIError:
            return NSLocalizedString(
                "error.paddleocr.cloud.api.recovery",
                comment: "Check your network connection and cloud API settings"
            )
        }
    }
}
