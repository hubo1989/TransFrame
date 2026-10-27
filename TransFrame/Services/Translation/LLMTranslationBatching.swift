import Foundation

/// 在达到输出预算前拆批，长片段按字符而非空格切断并保留顺序。
enum LLMTranslationBatching {
    static let separator = "\n---\n"

    static func split(texts: [String], maxOutputTokens: Int, promptTemplate: String?) -> [[String]] {
        let budget = max(1, maxOutputTokens)
        let fixedPrompt = promptTemplate?.replacingOccurrences(of: "{text}", with: "") ?? ""
        let sourceBudget = max(4, budget - estimatedTokens(for: fixedPrompt))
        var batches: [[String]] = []
        var current: [String] = []
        var used = 0

        for text in texts {
            let pieces = estimatedTokens(for: text) > budget
                ? splitLongText(text, budget: sourceBudget) : [text]
            for piece in pieces {
                let cost = piece.utf16.count + (current.isEmpty ? 0 : separator.utf16.count)
                if !current.isEmpty, used + cost > sourceBudget {
                    batches.append(current)
                    current = []
                    used = 0
                }
                used += piece.utf16.count + (current.isEmpty ? 0 : separator.utf16.count)
                current.append(piece)
            }
        }
        if !current.isEmpty { batches.append(current) }
        return batches
    }

    private static func estimatedTokens(for text: String) -> Int {
        text.utf16.count
    }

    private static func splitLongText(_ text: String, budget: Int) -> [String] {
        guard text.utf16.count > budget else { return [text] }
        var pieces: [String] = []
        var current = ""
        for character in text {
            if !current.isEmpty, current.utf16.count + character.utf16.count > budget {
                pieces.append(current)
                current = ""
            }
            current.append(character)
        }
        if !current.isEmpty { pieces.append(current) }
        return pieces
    }
}
