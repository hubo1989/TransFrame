import Foundation
import os

/// OCR engine types supported by the application
enum OCREngineType: String, CaseIterable, Sendable, Codable {
    /// macOS native Vision framework (local, default)
    case vision = "vision"

    /// PaddleOCR (optional, external)
    case paddleOCR = "paddleocr"

    /// Localized display name
    var localizedName: String {
        switch self {
        case .vision:
            return NSLocalizedString("ocr.engine.vision", comment: "Vision (Local)")
        case .paddleOCR:
            return NSLocalizedString("ocr.engine.paddleocr", comment: "PaddleOCR")
        }
    }

    /// Description of the engine
    var description: String {
        switch self {
        case .vision:
            return NSLocalizedString(
                "ocr.engine.vision.description",
                comment: "Built-in macOS engine, no setup required"
            )
        case .paddleOCR:
            return NSLocalizedString(
                "ocr.engine.paddleocr.description",
                comment: "External OCR engine for enhanced accuracy"
            )
        }
    }

    /// Whether this engine is available
    /// Vision is always available; PaddleOCR requires external setup
    var isAvailable: Bool {
        switch self {
        case .vision:
            return true
        case .paddleOCR:
            return PaddleOCRChecker.isAvailable
        }
    }
}

// MARK: - PaddleOCR Availability Checker

/// Helper to check if PaddleOCR is available on the system
enum PaddleOCRChecker {
    private struct State {
        var isAvailable: Bool = false
        var executablePath: String?
        var version: String?
        var checkCompleted: Bool = false
    }

    private static let lock = NSLock()
    nonisolated(unsafe) private static var state = State()

    static var isAvailable: Bool { lock.withLock { state.isAvailable } }
    static var executablePath: String? { lock.withLock { state.executablePath } }
    static var version: String? { lock.withLock { state.version } }
    static var checkCompleted: Bool { lock.withLock { state.checkCompleted } }

    static func checkAvailabilityAsync() {
        Task.detached(priority: .userInitiated) {
            let result = await performFullCheck()
            lock.withLock {
                state.isAvailable = result.available
                state.executablePath = result.path
                state.version = result.version
                state.checkCompleted = true
            }
        }
    }

    private static func performFullCheck() async -> (available: Bool, path: String?, version: String?) {
        let possiblePaths = [
            "\(NSHomeDirectory())/.pyenv/shims/paddleocr",
            "/usr/local/bin/paddleocr",
            "/opt/homebrew/bin/paddleocr",
            "\(NSHomeDirectory())/.local/bin/paddleocr"
        ]

        Logger.ocr.debug("[PaddleOCRChecker] Checking paths: \(possiblePaths)")

        for path in possiblePaths where FileManager.default.isExecutableFile(atPath: path) {
            Logger.ocr.debug("[PaddleOCRChecker] Found executable at: \(path)")

            let task = Process()
            task.executableURL = URL(fileURLWithPath: path)
            task.arguments = ["--version"]
            task.environment = [
                "PATH": "\(NSHomeDirectory())/.pyenv/shims:/usr/local/bin:/usr/bin:/bin",
                "HOME": NSHomeDirectory(),
                "PYENV_ROOT": "\(NSHomeDirectory())/.pyenv",
                "PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK": "True"
            ]

            do {
                let output = try await AsyncProcessRunner(process: task).run()
                let outputText = String(data: output.stdoutData + output.stderrData, encoding: .utf8) ?? ""
                Logger.ocr.debug("Version output: \(outputText)")

                let versionLine = outputText.components(separatedBy: .newlines)
                    .first { $0.contains("paddleocr") }?
                    .trimmingCharacters(in: .whitespaces)

                Logger.ocr.info("Found: path=\(path), version=\(versionLine ?? "unknown")")
                return (true, path, versionLine)
            } catch {
                Logger.ocr.error("Error running \(path): \(error.localizedDescription)")
            }
        }

        Logger.ocr.info("Not found in any known path")
        return (false, nil, nil)
    }

    static func resetCache() {
        lock.withLock {
            state = State()
        }
    }
}
