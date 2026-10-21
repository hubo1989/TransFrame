import Foundation
import CoreGraphics
import AppKit

// MARK: - Strongly-typed Coordinate Rectangles

/// Global coordinate system used by Cocoa/AppKit (NSScreen frames, NSWindow frames).
/// Origin (0,0) is at the bottom-left of the primary display. Y increases upwards.
public struct CocoaGlobalRect: Equatable, Hashable, Sendable {
    public var rawValue: CGRect

    public init(_ rawValue: CGRect) {
        self.rawValue = rawValue
    }
}

/// Global coordinate system used by Quartz/CoreGraphics (CGWindowList, ScreenCaptureKit, Accessibility API).
/// Origin (0,0) is at the top-left of the primary display. Y increases downwards.
public struct QuartzGlobalRect: Equatable, Hashable, Sendable {
    public var rawValue: CGRect

    public init(_ rawValue: CGRect) {
        self.rawValue = rawValue
    }
}

/// Local coordinate system relative to a single display's bounds.
/// Origin (0,0) is at the top-left of that display. Y increases downwards.
public struct DisplayLocalRect: Equatable, Hashable, Sendable {
    public var rawValue: CGRect

    public init(_ rawValue: CGRect) {
        self.rawValue = rawValue
    }
}

/// Physical pixel coordinate system of a rendered CGImage or snapshot bitmap.
/// Origin (0,0) is at the top-left pixel. Y increases downwards.
public struct ImagePixelRect: Equatable, Hashable, Sendable {
    public var rawValue: CGRect

    public init(_ rawValue: CGRect) {
        self.rawValue = rawValue
    }
}

/// Viewport coordinate system within an interactive preview or canvas.
public struct ViewportRect: Equatable, Hashable, Sendable {
    public var rawValue: CGRect

    public init(_ rawValue: CGRect) {
        self.rawValue = rawValue
    }
}

// MARK: - CoordinateMapper

/// Pure-functional coordinate conversion utility.
/// Standardizes mappings between Cocoa, Quartz, Display-local, and Pixel coordinate spaces,
/// eliminating multi-monitor height flip discrepancies.
public enum CoordinateMapper {
    /// Resolves the height of the primary display in Cocoa global points.
    /// In Cocoa, the primary display always has origin (0,0) and sits at the base of the global coordinate system.
    public static func primaryScreenHeight(screens: [NSScreen] = NSScreen.screens) -> CGFloat {
        if let primary = screens.first(where: { $0.frame.origin == .zero }) {
            return primary.frame.height
        }
        return screens.first?.frame.height ?? CGDisplayBounds(CGMainDisplayID()).height
    }

    /// Converts a rectangle from Cocoa global coordinates (origin bottom-left, Y up)
    /// to Quartz global coordinates (origin top-left, Y down).
    public static func cocoaToQuartz(
        _ cocoaRect: CocoaGlobalRect,
        primaryHeight: CGFloat
    ) -> QuartzGlobalRect {
        let r = cocoaRect.rawValue
        let yPrime = primaryHeight - (r.origin.y + r.size.height)
        return QuartzGlobalRect(CGRect(x: r.origin.x, y: yPrime, width: r.size.width, height: r.size.height))
    }

    /// Converts a rectangle from Quartz global coordinates (origin top-left, Y down)
    /// to Cocoa global coordinates (origin bottom-left, Y up).
    public static func quartzToCocoa(
        _ quartzRect: QuartzGlobalRect,
        primaryHeight: CGFloat
    ) -> CocoaGlobalRect {
        let r = quartzRect.rawValue
        let yPrime = primaryHeight - (r.origin.y + r.size.height)
        return CocoaGlobalRect(CGRect(x: r.origin.x, y: yPrime, width: r.size.width, height: r.size.height))
    }

    /// Converts a Quartz global point to Cocoa global point.
    public static func quartzPointToCocoa(
        _ quartzPoint: CGPoint,
        primaryHeight: CGFloat
    ) -> CGPoint {
        CGPoint(x: quartzPoint.x, y: primaryHeight - quartzPoint.y)
    }

    /// Converts a Cocoa global point to Quartz global point.
    public static func cocoaPointToQuartz(
        _ cocoaPoint: CGPoint,
        primaryHeight: CGFloat
    ) -> CGPoint {
        CGPoint(x: cocoaPoint.x, y: primaryHeight - cocoaPoint.y)
    }

    /// Converts a Quartz global rectangle to a display-local rectangle (top-left origin).
    public static func quartzToDisplayLocal(
        _ quartzRect: QuartzGlobalRect,
        displayQuartzBounds: QuartzGlobalRect
    ) -> DisplayLocalRect {
        let r = quartzRect.rawValue
        let d = displayQuartzBounds.rawValue
        let local = CGRect(
            x: r.origin.x - d.origin.x,
            y: r.origin.y - d.origin.y,
            width: r.size.width,
            height: r.size.height
        )
        return DisplayLocalRect(local)
    }

    /// Converts a display-local rectangle (top-left origin) to Quartz global coordinates.
    public static func displayLocalToQuartz(
        _ localRect: DisplayLocalRect,
        displayQuartzBounds: QuartzGlobalRect
    ) -> QuartzGlobalRect {
        let l = localRect.rawValue
        let d = displayQuartzBounds.rawValue
        let global = CGRect(
            x: l.origin.x + d.origin.x,
            y: l.origin.y + d.origin.y,
            width: l.size.width,
            height: l.size.height
        )
        return QuartzGlobalRect(global)
    }

    /// Converts a display-local rectangle in points to physical pixel coordinates on the snapshot image.
    /// Uses floor for minimum bounds and ceil for maximum bounds to ensure complete boundary coverage.
    public static func displayLocalToPixel(
        _ localRect: DisplayLocalRect,
        scaleFactor: CGFloat,
        imageSize: CGSize
    ) -> ImagePixelRect {
        let r = localRect.rawValue
        let minX = max(0, floor(r.minX * scaleFactor))
        let minY = max(0, floor(r.minY * scaleFactor))
        let maxX = min(imageSize.width, ceil(r.maxX * scaleFactor))
        let maxY = min(imageSize.height, ceil(r.maxY * scaleFactor))

        let width = max(0, maxX - minX)
        let height = max(0, maxY - minY)

        return ImagePixelRect(CGRect(x: minX, y: minY, width: width, height: height))
    }

    /// Converts physical pixel coordinates on the snapshot image back to display-local points.
    public static func pixelToDisplayLocal(
        _ pixelRect: ImagePixelRect,
        scaleFactor: CGFloat
    ) -> DisplayLocalRect {
        guard scaleFactor > 0 else { return DisplayLocalRect(pixelRect.rawValue) }
        let p = pixelRect.rawValue
        return DisplayLocalRect(CGRect(
            x: p.origin.x / scaleFactor,
            y: p.origin.y / scaleFactor,
            width: p.size.width / scaleFactor,
            height: p.size.height / scaleFactor
        ))
    }
}
