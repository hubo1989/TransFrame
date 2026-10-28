import os
import Foundation

extension Logger {
    private static let subsystem = Bundle.main.bundleIdentifier ?? "com.transframe"
    
    static let general = Logger(subsystem: subsystem, category: "general")
    static let ocr = Logger(subsystem: subsystem, category: "ocr")
    static let translation = Logger(subsystem: subsystem, category: "translation")
    static let capture = Logger(subsystem: subsystem, category: "capture")
    static let ui = Logger(subsystem: subsystem, category: "ui")
    static let settings = Logger(subsystem: subsystem, category: "settings")
    static let textInsertion = Logger(subsystem: subsystem, category: "TextInsertion")
}
