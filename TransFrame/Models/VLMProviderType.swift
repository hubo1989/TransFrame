//
//  VLMProviderType.swift
//  TransFrame
//
//  Created for US-015: VLM and Translation Configuration UI
//

import Foundation

/// VLM (Vision Language Model) provider types supported by the application
enum VLMProviderType: String, CaseIterable, Sendable, Codable, Identifiable {
    case openai = "openai"
    case claude = "claude"
    case gemini = "gemini"
    case ollama = "ollama"
    case paddleocr = "paddleocr"
    
    var id: String { rawValue }
    
    /// Localized display name
    var localizedName: String {
        switch self {
        case .openai:
            return "OpenAI"
        case .claude:
            return "Claude"
        case .gemini:
            return "Gemini"
        case .ollama:
            let str = NSLocalizedString("vlm.provider.local", comment: "Local Model")
            return (str.isEmpty || str == "vlm.provider.local") ? "本地模型" : str
        case .paddleocr:
            return "PaddleOCR"
        }
    }
    
    /// Description of the provider
    var providerDescription: String {
        switch self {
        case .openai:
            return NSLocalizedString(
                "vlm.provider.openai.description",
                comment: "OpenAI GPT-4 Vision / Compatible API"
            )
        case .claude:
            return NSLocalizedString(
                "vlm.provider.claude.description",
                comment: "Anthropic Claude Vision API"
            )
        case .gemini:
            return NSLocalizedString(
                "vlm.provider.gemini.description",
                comment: "Google Gemini Vision API"
            )
        case .ollama:
            return NSLocalizedString(
                "vlm.provider.local.description",
                comment: "Local model server (Ollama, LM Studio, vLLM, LocalAI, etc.)"
            )
        case .paddleocr:
            return NSLocalizedString(
                "vlm.provider.paddleocr.description",
                comment: "Local OCR engine (free, offline)"
            )
        }
    }
    
    /// Default base URL for this provider
    var defaultBaseURL: String {
        switch self {
        case .openai:
            return "https://api.openai.com/v1"
        case .claude:
            return "https://api.anthropic.com/v1"
        case .gemini:
            return "https://generativelanguage.googleapis.com/v1beta"
        case .ollama:
            return "http://localhost:11434"
        case .paddleocr:
            return ""
        }
    }
    
    /// Default model name for this provider
    var defaultModelName: String {
        switch self {
        case .openai:
            return "gpt-4o"
        case .claude:
            return "claude-sonnet-4-20250514"
        case .gemini:
            return "gemini-2.0-flash"
        case .ollama:
            return "llava"
        case .paddleocr:
            return ""
        }
    }
    
    /// Whether this provider typically uses an API key (for hint text, never used as hard validation)
    var typicallyRequiresAPIKey: Bool {
        switch self {
        case .openai, .claude, .gemini:
            return true
        case .ollama, .paddleocr:
            return false
        }
    }

    var requiresAPIKey: Bool { typicallyRequiresAPIKey }
}

/// OpenAI API mode (Chat completions vs Responses API)
enum OpenAIAPIMode: String, CaseIterable, Sendable, Codable, Identifiable {
    case chat = "chat"
    case response = "response"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .chat:
            return "Chat (/chat/completions)"
        case .response:
            return "Response (/responses)"
        }
    }
}

/// Quick endpoint presets for local providers
struct LocalProviderEndpointPreset: Identifiable, Sendable {
    let id: String
    let name: String
    let url: String
}

extension VLMProviderType {
    static let localEndpointPresets: [LocalProviderEndpointPreset] = [
        LocalProviderEndpointPreset(id: "ollama", name: "Ollama (11434)", url: "http://localhost:11434"),
        LocalProviderEndpointPreset(id: "lmstudio", name: "LM Studio (1234)", url: "http://localhost:1234/v1"),
        LocalProviderEndpointPreset(id: "vllm", name: "vLLM (8000)", url: "http://localhost:8000/v1"),
        LocalProviderEndpointPreset(id: "localai", name: "LocalAI (8080)", url: "http://localhost:8080/v1"),
        LocalProviderEndpointPreset(id: "xinference", name: "Xinference (9997)", url: "http://localhost:9997/v1")
    ]
}

/// Preferred translation engine type for the translation workflow
enum PreferredTranslationEngine: String, CaseIterable, Sendable, Codable, Identifiable {
    case apple = "apple"
    case mtranServer = "mtran"
    
    var id: String { rawValue }
    
    /// Localized display name
    var localizedName: String {
        switch self {
        case .apple:
            return NSLocalizedString("translation.engine.apple", comment: "Apple Translation")
        case .mtranServer:
            return NSLocalizedString("translation.engine.mtran", comment: "MTransServer")
        }
    }
    
    /// Description of the engine
    var engineDescription: String {
        switch self {
        case .apple:
            return NSLocalizedString(
                "translation.preferred.apple.description",
                comment: "Built-in macOS translation, works offline"
            )
        case .mtranServer:
            return NSLocalizedString(
                "translation.preferred.mtran.description",
                comment: "Self-hosted translation server for better quality"
            )
        }
    }
}
