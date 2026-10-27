import CoreGraphics
import Foundation

@available(macOS 13.0, *)
actor AnalysisPipeline {
    typealias Analyzer = @Sendable (CGImage) async throws -> ScreenAnalysisResult
    typealias OCRFallback = @Sendable (CGImage) async throws -> OCRResult

    private let analyzer: Analyzer
    private let ocrFallback: OCRFallback
    private let recorder: PerformanceRecorder

    init(
        analyzer: @escaping Analyzer = { image in
            try await ScreenCoderEngine.shared.analyze(image: image)
        },
        ocrFallback: @escaping OCRFallback = { image in
            try await OCRService.shared.recognize(image)
        },
        recorder: PerformanceRecorder = .shared
    ) {
        self.analyzer = analyzer
        self.ocrFallback = ocrFallback
        self.recorder = recorder
    }

    func analyze(image: CGImage, context: PipelineContext) async throws -> ScreenAnalysisResult {
        do {
            return try await recorder.measure(stage: .analysis, operationID: context.operationID) {
                try Task.checkCancellation()
                let initialAnalysis = try await analyzer(image)
                return try await Self.recoverAnalysisResultIfNeeded(initialAnalysis) {
                    try await ocrFallback(image)
                }
            }
        } catch {
            throw PipelineError(stage: .analysis, error: error)
        }
    }

    static func recoverAnalysisResultIfNeeded(
        _ analysisResult: ScreenAnalysisResult,
        ocrFallback: @Sendable () async throws -> OCRResult
    ) async throws -> ScreenAnalysisResult {
        // Fallback to OCR when VLM returned nothing useful: empty segments
        // or only prompt/schema leakage.
        guard analysisResult.segments.isEmpty || analysisResult.containsOnlyPromptLeakage else {
            return analysisResult
        }

        let fallbackResult = try await ocrFallback()
        let recoveredResult = ScreenAnalysisResult(ocrResult: fallbackResult)
        return recoveredResult.segments.isEmpty ? analysisResult : recoveredResult
    }
}
