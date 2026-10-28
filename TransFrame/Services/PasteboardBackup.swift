//
//  PasteboardBackup.swift
//  TransFrame
//
//  Shared pasteboard save/restore used by selection capture and the
//  paste fallback for text insertion.
//

import AppKit
import os

/// NSPasteboard 内容的可恢复备份，用于「临时写入剪贴板 → 粘贴 → 恢复」流程。
/// NSPasteboard 本身线程安全（官方文档），因此不标注 MainActor；
/// ClipboardPaste 的 @MainActor 仅为了与 AX 插入路径保持同域。
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
                } else {
                    // 文件承诺/延迟渲染等类型取不到数据，恢复时会丢失，至少留痕
                    Logger.general.notice("PasteboardBackup: 丢弃无法取数据的剪贴板类型 \(type.rawValue, privacy: .public)")
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
