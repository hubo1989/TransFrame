import XCTest
import SwiftUI
@testable import TransFrame

/// Engine inventory convergence tests (PRD US-003)
/// tasks/prd-engine-mode-convergence.md
final class EngineInventoryConvergenceTests: XCTestCase {

    // MARK: - Card list

    func testUICardsContainEightEnginesExcludingOllamaAndCustom() {
        XCTAssertEqual(
            TranslationEngineType.uiCards,
            [.apple, .mtranServer, .openai, .claude, .gemini, .google, .deepl, .baidu]
        )
        XCTAssertFalse(TranslationEngineType.uiCards.contains(.ollama))
        XCTAssertFalse(TranslationEngineType.uiCards.contains(.custom))
    }

    func testEveryUICardBelongsToARenderedCategory() {
        // categories rendered as cards: builtIn, llm, cloudService
        let rendered: Set<EngineCategory> = [.builtIn, .llm, .cloudService]
        for engine in TranslationEngineType.uiCards {
            XCTAssertTrue(rendered.contains(engine.category), "\(engine) in unexpected category")
        }
    }

    // MARK: - Preset inference

    func testPresetInferredFromOfficialEndpoints() {
        XCTAssertEqual(
            OpenAICompatiblePreset.inferred(fromBaseURL: "https://api.openai.com/v1"),
            .openAI
        )
        XCTAssertEqual(
            OpenAICompatiblePreset.inferred(fromBaseURL: "http://localhost:11434"),
            .ollamaLocal
        )
    }

    func testPresetInferredCustomForArbitraryURL() {
        XCTAssertEqual(
            OpenAICompatiblePreset.inferred(fromBaseURL: "https://relay.example.com/v1"),
            .custom
        )
        XCTAssertEqual(OpenAICompatiblePreset.inferred(fromBaseURL: ""), .custom)
    }

    // MARK: - Ollama proxy save

    func testOllamaProxyConfigMapsExplicitFields() {
        let config = EngineConfigSheet.makeOllamaProxyConfig(
            isEnabled: true,
            baseURL: "http://localhost:11434",
            modelName: "llama3",
            options: nil
        )
        XCTAssertEqual(config.id, .ollama)
        XCTAssertTrue(config.isEnabled)
        XCTAssertEqual(config.options?.baseURL, "http://localhost:11434")
        XCTAssertEqual(config.options?.modelName, "llama3")
    }

    func testOllamaProxyConfigFillsDefaultsWhenFieldsEmpty() {
        let config = EngineConfigSheet.makeOllamaProxyConfig(
            isEnabled: false,
            baseURL: "",
            modelName: "",
            options: nil
        )
        XCTAssertEqual(config.id, .ollama)
        XCTAssertFalse(config.isEnabled)
        XCTAssertEqual(config.options?.baseURL, TranslationEngineType.ollama.defaultBaseURL)
        XCTAssertEqual(config.options?.modelName, TranslationEngineType.ollama.defaultModelName)
    }

    func testOllamaProxyConfigPreservesCustomOptions() {
        var existing = EngineOptions()
        existing.timeout = 42
        let config = EngineConfigSheet.makeOllamaProxyConfig(
            isEnabled: true,
            baseURL: "http://nas.local:11434",
            modelName: "qwen2.5",
            options: existing
        )
        XCTAssertEqual(config.options?.timeout, 42)
        XCTAssertEqual(config.options?.baseURL, "http://nas.local:11434")
        XCTAssertEqual(config.options?.modelName, "qwen2.5")
    }
}
