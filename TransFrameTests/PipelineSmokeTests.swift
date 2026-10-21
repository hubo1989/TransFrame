import CoreGraphics
import XCTest
@testable import TransFrame

@available(macOS 13.0, *)
final class PipelineSmokeTests: XCTestCase {
    func testAnalysisPipelineFallsBackWhenAnalyzerLeaksPrompt() async throws {
        let recorder = PerformanceRecorder()
        let image = try XCTUnwrap(Self.makeImage())
        let pipeline = AnalysisPipeline(
            analyzer: { _ in
                ScreenAnalysisResult(
                    segments: [
                        TextSegment(text: "置信度: 0.0-1.0", boundingBox: .zero, confidence: 1),
                        TextSegment(text: "x, y: 左上角 (0.0-1.0)", boundingBox: .zero, confidence: 1)
                    ],
                    imageSize: CGSize(width: 16, height: 16)
                )
            },
            ocrFallback: { _ in
                OCRResult(
                    observations: [
                        OCRText(
                            text: "Settings",
                            boundingBox: CGRect(x: 0.2, y: 0.2, width: 0.3, height: 0.1),
                            confidence: 0.9
                        )
                    ],
                    imageSize: CGSize(width: 16, height: 16)
                )
            },
            recorder: recorder
        )

        let result = try await pipeline.analyze(image: image, context: PipelineContext())

        XCTAssertEqual(result.segments.map(\.text), ["Settings"])
        let summaries = await recorder.summaries()
        XCTAssertEqual(summaries[.analysis]?.count, 1)
    }

    func testTranslationRenderPipelineSupportsTranslationOnlyAndRender() async throws {
        let recorder = PerformanceRecorder()
        let image = try XCTUnwrap(Self.makeImage())
        let segment = TextSegment(text: "Hello", boundingBox: .zero, confidence: 1)
        let service = MockTranslationServicing(
            nextResult: [
                BilingualSegment(
                    original: segment,
                    translated: "你好",
                    sourceLanguage: "English",
                    targetLanguage: "Chinese"
                )
            ]
        )
        let pipeline = TranslationRenderPipeline(
            translationService: service,
            renderer: { image, _, _ in image },
            recorder: recorder
        )
        let config = ImageTranslationPipelineConfig(
            targetLanguage: "zh-Hans",
            sourceLanguage: "en",
            preferredEngine: .apple,
            scene: nil,
            mode: .primaryWithFallback,
            fallbackEnabled: true,
            parallelEngines: [],
            sceneBindings: [:]
        )
        let context = PipelineContext()

        let translated = try await pipeline.translate(
            analysisResult: ScreenAnalysisResult(
                segments: [segment],
                imageSize: CGSize(width: 16, height: 16)
            ),
            config: config,
            context: context
        )
        let rendered = try await pipeline.render(
            image: image,
            segments: translated,
            theme: .light,
            context: context
        )

        XCTAssertEqual(translated.map(\.translated), ["你好"])
        XCTAssertEqual(rendered.width, image.width)
        let summaries = await recorder.summaries()
        XCTAssertEqual(summaries[.translation]?.count, 1)
        XCTAssertEqual(summaries[.render]?.count, 1)
    }

    func testTranslationRenderPipelineThrowsWhenSegmentCountsMismatch() async throws {
        let segment = TextSegment(text: "Hello", boundingBox: .zero, confidence: 1)
        let service = MockTranslationServicing(nextResult: [])
        let pipeline = TranslationRenderPipeline(
            translationService: service,
            renderer: { image, _, _ in image },
            recorder: PerformanceRecorder()
        )
        let config = ImageTranslationPipelineConfig(
            targetLanguage: "zh-Hans",
            sourceLanguage: "en",
            preferredEngine: .apple,
            scene: nil,
            mode: .primaryWithFallback,
            fallbackEnabled: true,
            parallelEngines: [],
            sceneBindings: [:]
        )

        do {
            _ = try await pipeline.translate(
                analysisResult: ScreenAnalysisResult(
                    segments: [segment],
                    imageSize: CGSize(width: 16, height: 16)
                ),
                config: config,
                context: PipelineContext()
            )
            XCTFail("Expected translation count mismatch")
        } catch let error as PipelineError {
            XCTAssertEqual(error.category, .translation)
        }
    }

    func testCapturePipelineRefreshesDisplayLookupOnlyWhenStale() async throws {
        let recorder = PerformanceRecorder()
        let pipeline = CapturePipeline(recorder: recorder)
        let context = PipelineContext()
        let refreshCount = LockedCounter()

        let first: Int = try await pipeline.lookupDisplay(
            context: context,
            freshness: .refreshIfOlderThan(60),
            refresh: {
                await refreshCount.increment()
            },
            lookup: {
                1
            }
        )
        let second: Int = try await pipeline.lookupDisplay(
            context: context,
            freshness: .refreshIfOlderThan(60),
            refresh: {
                await refreshCount.increment()
            },
            lookup: {
                2
            }
        )

        XCTAssertEqual(first, 1)
        XCTAssertEqual(second, 2)
        let refreshes = await refreshCount.currentValue()
        XCTAssertEqual(refreshes, 1)
        let summaries = await recorder.summaries()
        XCTAssertEqual(summaries[.displayLookup]?.count, 2)
    }

    func testImagePipelineStaysWithinMockPerformanceBudget() async throws {
        let recorder = PerformanceRecorder()
        let image = try XCTUnwrap(Self.makeImage())
        let service = MockTranslationServicing(
            nextResult: [
                BilingualSegment(
                    original: TextSegment(text: "Hello", boundingBox: .zero, confidence: 1),
                    translated: "你好",
                    sourceLanguage: "English",
                    targetLanguage: "Chinese"
                )
            ]
        )
        let pipeline = ImageTranslationPipeline(
            analyzer: { _ in
                ScreenAnalysisResult(
                    segments: [TextSegment(text: "Hello", boundingBox: .zero, confidence: 1)],
                    imageSize: CGSize(width: 16, height: 16)
                )
            },
            ocrFallback: { _ in
                OCRResult(observations: [], imageSize: CGSize(width: 16, height: 16))
            },
            translationService: service,
            renderer: { image, _, _ in image },
            recorder: recorder
        )
        let config = ImageTranslationPipelineConfig(
            targetLanguage: "zh-Hans",
            sourceLanguage: "en",
            preferredEngine: .apple,
            scene: nil,
            mode: .primaryWithFallback,
            fallbackEnabled: true,
            parallelEngines: [],
            sceneBindings: [:]
        )

        for _ in 0..<5 {
            _ = try await pipeline.run(image: image, config: config, theme: .light)
        }

        let summaries = await recorder.summaries()
        XCTAssertTrue(PerformanceBudget(stage: .analysis, p95Milliseconds: 100).allows(try XCTUnwrap(summaries[.analysis])))
        XCTAssertTrue(PerformanceBudget(stage: .translation, p95Milliseconds: 100).allows(try XCTUnwrap(summaries[.translation])))
        XCTAssertTrue(PerformanceBudget(stage: .render, p95Milliseconds: 100).allows(try XCTUnwrap(summaries[.render])))
    }

    func testPaddleOCRPythonDictToJsonHandlesApostrophesAndQuotedKeywords() throws {
        let engine = PaddleOCREngine.shared
        let pythonDict = #"  {'res': {'rec_texts': ["don't", 'it\'s "great"', 'True story', 'None of {this}'], 'flag': True, 'empty': None, 'valid': False, 'score': 8.}}"#.trimmingCharacters(in: .whitespaces)

        let braceEnd = try XCTUnwrap(engine.findMatchingBrace(in: pythonDict))
        XCTAssertEqual(braceEnd, pythonDict.count - 1)

        let jsonString = engine.convertPythonDictToJson(pythonDict)
        let data = try XCTUnwrap(jsonString.data(using: .utf8))
        let parsed = try XCTUnwrap(try JSONSerialization.jsonObject(with: data) as? [String: Any])
        let res = try XCTUnwrap(parsed["res"] as? [String: Any])

        XCTAssertEqual(res["rec_texts"] as? [String], ["don't", #"it's "great""#, "True story", "None of {this}"])
        XCTAssertEqual(res["flag"] as? Bool, true)
        XCTAssertTrue(res["empty"] is NSNull)
        XCTAssertEqual(res["valid"] as? Bool, false)
        XCTAssertEqual(res["score"] as? Double, 8.0)
    }

    func testAsyncProcessRunnerDrainsLargePipesWithoutDeadlock() async throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        // Write >128KB to both stdout and stderr to exceed standard 64KB OS pipe buffer
        process.arguments = ["-c", "dd if=/dev/zero bs=1024 count=160 2>/dev/null | tr '\\0' 'a'; dd if=/dev/zero bs=1024 count=160 2>/dev/null | tr '\\0' 'b' >&2"]

        let runner = AsyncProcessRunner(process: process)
        let output = try await runner.run()

        XCTAssertEqual(output.terminationStatus, 0)
        XCTAssertEqual(output.stdoutData.count, 160 * 1024)
        XCTAssertEqual(output.stderrData.count, 160 * 1024)
    }

    private static func makeImage() -> CGImage? {
        guard let context = CGContext(
            data: nil,
            width: 16,
            height: 16,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            return nil
        }

        context.setFillColor(CGColor(gray: 1, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: 16, height: 16))
        return context.makeImage()
    }

    func testVLMJSONExtractorHandlesMarkdownAndSurroundingText() {
        let rawMarkdown = """
        ```json
        {
            "segments": [
                {
                    "text": "Hello World",
                    "boundingBox": {"x": 0.1, "y": 0.2, "width": 0.3, "height": 0.4},
                    "confidence": 0.98
                }
            ]
        }
        ```
        """
        let extracted1 = VLMJSONExtractor.extractJSON(from: rawMarkdown)
        XCTAssertTrue(extracted1.hasPrefix("{"))
        XCTAssertTrue(extracted1.hasSuffix("}"))
        XCTAssertFalse(extracted1.contains("```"))

        let surroundingText = """
        I analyzed the screenshot, here is the result:
        {
            "segments": []
        }
        Let me know if you need more assistance!
        """
        let extracted2 = VLMJSONExtractor.extractJSON(from: surroundingText)
        XCTAssertEqual(extracted2, "{\n    \"segments\": []\n}")
    }

    func testMosaicCacheStoreAndRetrieve() throws {
        let image = try XCTUnwrap(Self.makeImage())
        let cache = MosaicCache.shared
        let key = cache.key(rect: CGRect(x: 10, y: 10, width: 50, height: 50), blockSize: 10, imageID: ObjectIdentifier(image))

        XCTAssertNil(cache.image(for: key))
        cache.setImage(image, for: key)
        XCTAssertNotNil(cache.image(for: key))

        cache.clear()
        XCTAssertNil(cache.image(for: key))
    }

    @MainActor
    func testSettingsViewModelReloadsEditingStateOnUnlock() async throws {
        let previousState = CredentialAuthManager.shared.state
        CredentialAuthManager.shared.setAuthStateForTesting(.unlocked)

        let settings = AppSettings.shared
        let originalKey = settings.vlmAPIKey(for: .openai)
        let originalBaseURL = settings.vlmBaseURL(for: .openai)
        let originalModelName = settings.vlmModelName(for: .openai)

        defer {
            Task { @MainActor in
                try? await KeychainService.shared.saveVLMCredentials(apiKey: originalKey, for: .openai)
                settings.setVLMBaseURL(originalBaseURL, for: .openai)
                settings.setVLMModelName(originalModelName, for: .openai)
                CredentialAuthManager.shared.setAuthStateForTesting(previousState)
            }
        }

        try await KeychainService.shared.saveVLMCredentials(apiKey: "unlocked_test_key", for: .openai)
        settings.setVLMBaseURL("https://api.openai.com/v1", for: .openai)
        settings.setVLMModelName("gpt-4o", for: .openai)

        let viewModel = SettingsViewModel(settings: settings)
        viewModel.vlmProvider = .openai
        viewModel.loadVLMEditingState(for: .openai)

        XCTAssertEqual(viewModel.vlmEditingAPIKey, "unlocked_test_key")
        XCTAssertFalse(viewModel.hasUnsavedVLMChanges)

        // Simulate typing a draft
        viewModel.vlmEditingAPIKey = "different_draft_key"
        XCTAssertTrue(viewModel.hasUnsavedVLMChanges)

        // Reload after credential unlock refreshes the editor
        viewModel.loadVLMEditingState(for: .openai)
        XCTAssertEqual(viewModel.vlmEditingAPIKey, "unlocked_test_key")
        XCTAssertFalse(viewModel.hasUnsavedVLMChanges)
    }
}

private actor LockedCounter {
    private var value = 0

    func increment() {
        value += 1
    }

    func currentValue() -> Int {
        value
    }
}
