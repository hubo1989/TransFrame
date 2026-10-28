//
//  PasteboardBackup.swift
//  TransFrame
//
//  Shared pasteboard save/restore used by selection capture and the
//  paste fallback for text insertion.
//

import AppKit

/// NSPasteboard 内容的可恢复备份，用于「临时写入剪贴板 → 粘贴 → 恢复」流程。
struct PasteboardBackup: Sendable {
    let items: [[String: Data]]
    let string: String?
    let changeCount: Int

    /// 备份当前剪贴板内容（含多 item 与全部类型）。
    static func save() -> PasteboardBackup {
        let pasteboard = NSPasteboard.general
        let stringContent = pasteboard.string(forType: .string)

        var items: [[String: Data]] = []
        for item in pasteboard.pasteboardItems ?? [] {
            var itemData: [String: Data] = [:]
            for type in item.types {
                if let data = item.data(forType: type) {
                    itemData[type.rawValue] = data
                }
            }
            if !itemData.isEmpty {
                items.append(itemData)
            }
        }

        return PasteboardBackup(
            items: items,
            string: stringContent,
            changeCount: pasteboard.changeCount
        )
    }

    /// 恢复备份内容。空备份等价于恢复为空剪贴板。
    static func restore(_ backup: PasteboardBackup) {
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()

        if backup.items.isEmpty {
            if let stringContent = backup.string {
                pasteboard.setString(stringContent, forType: .string)
            }
        } else {
            for itemData in backup.items {
                let item = NSPasteboardItem()
                for (typeRawValue, data) in itemData {
                    item.setData(data, forType: NSPasteboard.PasteboardType(rawValue: typeRawValue))
                }
                pasteboard.writeObjects([item])
            }
        }
    }
}
