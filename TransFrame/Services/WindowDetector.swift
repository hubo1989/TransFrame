import Foundation
import CoreGraphics
import AppKit
import ApplicationServices

/// Type of detectable UI boundary
public enum WindowElementType: String, Codable, Sendable {
    case window
    case popoverOrMenu
}

/// Represents a window or sub-element detected by CGWindowListCopyWindowInfo or AXUIElement
public struct WindowInfo: Identifiable, Hashable, Sendable {
    /// Unique identifier for Identifiable conformance
    public var id: UInt { UInt(windowID) }

    /// System window identifier (CGWindowID)
    public let windowID: CGWindowID

    /// Process ID owning this window
    public let ownerPID: pid_t

    /// Position and size in Quartz global screen coordinates (origin top-left, Y down)
    public let frame: CGRect

    /// Name of the application that owns this window
    public let ownerName: String

    /// Title of the window or label of the sub-element
    public let windowName: String

    /// Window layer level (0 = normal windows, >0 = floating windows/menus)
    public let windowLayer: Int

    /// Whether this window is on-screen
    public let isOnScreen: Bool

    /// Alpha value (0.0 - 1.0)
    public let alpha: Double

    /// Element classification (window, popoverOrMenu, or UI element)
    public let elementType: WindowElementType

    public init(
        windowID: CGWindowID,
        ownerPID: pid_t = 0,
        frame: CGRect,
        ownerName: String,
        windowName: String,
        windowLayer: Int,
        isOnScreen: Bool,
        alpha: Double,
        elementType: WindowElementType = .window
    ) {
        self.windowID = windowID
        self.ownerPID = ownerPID
        self.frame = frame
        self.ownerName = ownerName
        self.windowName = windowName
        self.windowLayer = windowLayer
        self.isOnScreen = isOnScreen
        self.alpha = alpha
        self.elementType = elementType
    }

    // MARK: - Computed Properties

    public var displayName: String {
        if !windowName.isEmpty {
            return windowName
        }
        return ownerName
    }

    public var isNormalWindow: Bool {
        windowLayer == 0
    }

    // MARK: - Hashable

    public func hash(into hasher: inout Hasher) {
        hasher.combine(windowID)
        hasher.combine(frame.origin.x)
        hasher.combine(frame.origin.y)
        hasher.combine(frame.size.width)
        hasher.combine(frame.size.height)
    }

    public static func == (lhs: WindowInfo, rhs: WindowInfo) -> Bool {
        lhs.windowID == rhs.windowID && lhs.frame == rhs.frame
    }
}

// MARK: - Window Detection Service

/// Service responsible for detecting windows and sub-controls.
/// Uses CGWindowListCopyWindowInfo and optionally AXUIElement (when enabled).
public actor WindowDetector {
    public static let shared = WindowDetector()

    private init() {}

    /// Enumerates all candidate windows synchronously and purely without thread blocking.
    /// - Parameter excludingWindowIDs: Specific window IDs to ignore (such as active overlay windows).
    ///   Existing PinnedWindows or other TransFrame windows are explicitly kept as capture candidates.
    public static func detectWindows(
        excludingWindowIDs: Set<CGWindowID> = []
    ) -> [WindowInfo] {
        guard let windowList = CGWindowListCopyWindowInfo(
            [.optionOnScreenOnly, .excludeDesktopElements],
            kCGNullWindowID
        ) as? [[String: Any]] else {
            return []
        }

        var results: [WindowInfo] = []

        for dict in windowList {
            guard let windowID = dict[kCGWindowNumber as String] as? CGWindowID,
                  !excludingWindowIDs.contains(windowID) else {
                continue
            }

            guard let boundsDict = dict[kCGWindowBounds as String] as? [String: Any],
                  let x = boundsDict["X"] as? CGFloat,
                  let y = boundsDict["Y"] as? CGFloat,
                  let width = boundsDict["Width"] as? CGFloat,
                  let height = boundsDict["Height"] as? CGFloat else {
                continue
            }

            let frame = CGRect(x: x, y: y, width: width, height: height)
            guard width >= 10 && height >= 10 else { continue }

            let ownerPID = (dict[kCGWindowOwnerPID as String] as? Int32).map { pid_t($0) } ?? 0
            let ownerName = dict[kCGWindowOwnerName as String] as? String ?? ""
            let windowName = dict[kCGWindowName as String] as? String ?? ""
            let layer = dict[kCGWindowLayer as String] as? Int ?? 0
            let alpha = dict[kCGWindowAlpha as String] as? Double ?? 1.0

            guard alpha > 0.05 else { continue }

            // Filter out system background layers, dock, status bar background, or screensaver
            if layer < 0 || layer >= 1000 {
                continue
            }
            let ignoredOwners: Set<String> = [
                "Window Server", "Dock", "程序坞", "Wallpaper", "壁纸",
                "NotificationCenter", "通知中心", "ControlCenter", "控制中心",
                "Spotlight", "聚焦", "loginwindow", "SystemUIServer"
            ]
            if ignoredOwners.contains(ownerName) {
                continue
            }
            // Filter out non-normal windows that are too small or bar-like (e.g. status bar tools, bar overlays)
            if layer != 0 && (height < 60 || width < 60) {
                continue
            }

            let elementType: WindowElementType = (layer > 0) ? .popoverOrMenu : .window

            results.append(
                WindowInfo(
                    windowID: windowID,
                    ownerPID: ownerPID,
                    frame: frame,
                    ownerName: ownerName,
                    windowName: windowName,
                    windowLayer: layer,
                    isOnScreen: true,
                    alpha: alpha,
                    elementType: elementType
                )
            )
        }

        return results
    }

    // MARK: - Synchronous Pure Candidate Filtering

    /// Synchronously filters windows under a Quartz point in front-to-back Z-order.
    /// Preserves the natural system window order (frontmost windows first).
    public static func candidatesUnderPoint(
        _ quartzPoint: CGPoint,
        in windows: [WindowInfo]
    ) -> [WindowInfo] {
        windows.filter { $0.frame.contains(quartzPoint) }
    }

    // MARK: - Backward Compatibility API for Actors

    public func visibleWindows() async -> [WindowInfo] {
        Self.detectWindows()
    }

    public func windowUnderPoint(_ point: CGPoint) async -> WindowInfo? {
        let windows = Self.detectWindows()
        return windows.first { $0.frame.contains(point) }
    }
}
