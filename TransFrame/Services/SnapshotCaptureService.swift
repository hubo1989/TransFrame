import Foundation
import CoreGraphics
import AppKit
import ScreenCaptureKit
import os

// MARK: - Snapshot Models

public struct DisplayDescriptor: Sendable {
    public let displayID: CGDirectDisplayID
    public let localizedName: String
    public let frame: CGRect
    public let scaleFactor: CGFloat
    public let isPrimary: Bool

    public init(
        displayID: CGDirectDisplayID,
        localizedName: String,
        frame: CGRect,
        scaleFactor: CGFloat,
        isPrimary: Bool
    ) {
        self.displayID = displayID
        self.localizedName = localizedName
        self.frame = frame
        self.scaleFactor = scaleFactor
        self.isPrimary = isPrimary
    }
}

/// Snapshot of a single display, captured at the freeze instant.
public struct DisplaySnapshot: Sendable {
    public let displayID: CGDirectDisplayID
    public let descriptor: DisplayDescriptor
    public let cocoaBounds: CocoaGlobalRect
    public let quartzBounds: QuartzGlobalRect
    public let scaleFactor: CGFloat
    public let image: CGImage

    @MainActor
    public var screen: NSScreen? {
        NSScreen.screens.first {
            ($0.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? CGDirectDisplayID) == displayID
        }
    }

    public init(
        displayID: CGDirectDisplayID,
        descriptor: DisplayDescriptor,
        cocoaBounds: CocoaGlobalRect,
        quartzBounds: QuartzGlobalRect,
        scaleFactor: CGFloat,
        image: CGImage
    ) {
        self.displayID = displayID
        self.descriptor = descriptor
        self.cocoaBounds = cocoaBounds
        self.quartzBounds = quartzBounds
        self.scaleFactor = scaleFactor
        self.image = image
    }
}

/// Frozen snapshot collection representing all screens at the moment capture was triggered.
public struct FrozenSnapshotSet: Sendable {
    public let snapshots: [DisplaySnapshot]
    public let candidateWindows: [WindowInfo]
    public let triggerReceivedAt: ContinuousClock.Instant
    public var freezeVisibleAt: ContinuousClock.Instant?

    public init(
        snapshots: [DisplaySnapshot],
        candidateWindows: [WindowInfo],
        triggerReceivedAt: ContinuousClock.Instant,
        freezeVisibleAt: ContinuousClock.Instant? = nil
    ) {
        self.snapshots = snapshots
        self.candidateWindows = candidateWindows
        self.triggerReceivedAt = triggerReceivedAt
        self.freezeVisibleAt = freezeVisibleAt
    }

    public func snapshot(for displayID: CGDirectDisplayID) -> DisplaySnapshot? {
        snapshots.first { $0.displayID == displayID }
    }

    public func snapshot(forQuartzPoint point: CGPoint) -> DisplaySnapshot? {
        snapshots.first { $0.quartzBounds.rawValue.contains(point) }
    }

    public func snapshot(forCocoaPoint point: CGPoint) -> DisplaySnapshot? {
        snapshots.first { $0.cocoaBounds.rawValue.contains(point) }
    }
}

// MARK: - SnapshotCaptureService

/// High-performance capture service that snapshots all displays in parallel at trigger time.
/// Prevents UI race conditions and guarantees 100% frozen screen inspection.
@MainActor
public final class SnapshotCaptureService {
    public static let shared = SnapshotCaptureService()

    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "SnapshotCapture")

    private init() {}

    /// Concurrently captures full snapshots across all available displays and enumerates stable candidate windows.
    /// - Parameters:
    ///   - excludingWindowIDs: Window IDs that should be ignored during window detection.
    ///   - triggerTime: Monotonic instant when user requested capture.
    public func captureAllDisplays(
        excludingWindowIDs: Set<CGWindowID> = [],
        triggerTime: ContinuousClock.Instant = .now
    ) async throws -> FrozenSnapshotSet {
        // Step 1: Pre-snapshot window enumeration
        let preWindows = WindowDetector.detectWindows(excludingWindowIDs: excludingWindowIDs)

        // Step 2: Extract Sendable display descriptors on MainActor
        let screens = NSScreen.screens
        guard !screens.isEmpty else {
            throw TransFrameError.captureError(message: "No active displays found")
        }
        let primaryHeight = CoordinateMapper.primaryScreenHeight(screens: screens)

        let descriptors: [DisplayDescriptor] = screens.map { screen in
            let displayID = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? CGDirectDisplayID
                ?? CGMainDisplayID()
            return DisplayDescriptor(
                displayID: displayID,
                localizedName: screen.localizedName,
                frame: screen.frame,
                scaleFactor: screen.backingScaleFactor,
                isPrimary: screen.frame.origin == .zero
            )
        }

        // Prepare SCShareableContent for ScreenCaptureKit if available
        let shareableContent = try? await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)

        // Step 3: Snapshot each display sequentially on MainActor to avoid SCShareableContent cross-actor data races
        var snapshots: [DisplaySnapshot] = []
        for descriptor in descriptors {
            let displayID = descriptor.displayID
            let frame = descriptor.frame
            let scale = descriptor.scaleFactor
            let cocoaBounds = CocoaGlobalRect(frame)
            let quartzBounds = QuartzGlobalRect(CGDisplayBounds(displayID))

            if let scDisplay = shareableContent?.displays.first(where: { $0.displayID == displayID }) {
                do {
                    let filter = SCContentFilter(display: scDisplay, excludingWindows: [])
                    let config = SCStreamConfiguration()
                    config.width = Int(frame.width * scale)
                    config.height = Int(frame.height * scale)
                    config.showsCursor = false
                    config.scalesToFit = false

                    let cgImage = try await SCScreenshotManager.captureImage(
                        contentFilter: filter,
                        configuration: config
                    )

                    snapshots.append(DisplaySnapshot(
                        displayID: displayID,
                        descriptor: descriptor,
                        cocoaBounds: cocoaBounds,
                        quartzBounds: quartzBounds,
                        scaleFactor: scale,
                        image: cgImage
                    ))
                } catch {
                    logger.warning("Failed to capture display \(displayID): \(error.localizedDescription)")
                }
            }
        }

        guard !snapshots.isEmpty else {
            throw TransFrameError.captureError(message: "Failed to capture snapshots for any display")
        }

        return FrozenSnapshotSet(
            snapshots: snapshots,
            candidateWindows: preWindows,
            triggerReceivedAt: triggerTime
        )
    }
}

// MARK: - SnapshotCropper

/// Performs non-destructive and destructive memory crops directly from a FrozenSnapshotSet.
/// NEVER calls live screen capture APIs during cropping.
public enum SnapshotCropper {
    /// Crops a selection defined in Quartz global coordinates from the frozen snapshot set.
    /// Supports single-screen direct cropping and multi-screen high-DPI compositing.
    public static func crop(
        quartzRect: QuartzGlobalRect,
        from snapshotSet: FrozenSnapshotSet
    ) throws -> CGImage {
        let r = quartzRect.rawValue
        guard r.width > 0 && r.height > 0 else {
            throw TransFrameError.captureError(message: "Cannot crop empty rectangle")
        }

        // 1. Check if selection is contained completely within a single display
        let containingSnapshots = snapshotSet.snapshots.filter { snapshot in
            snapshot.quartzBounds.rawValue.intersects(r)
        }

        guard !containingSnapshots.isEmpty else {
            throw TransFrameError.captureError(message: "Selection rect does not intersect any display")
        }

        if containingSnapshots.count == 1,
           let snapshot = containingSnapshots.first,
           snapshot.quartzBounds.rawValue.contains(r) {
            // Fast-path: Single display direct crop
            let displayLocal = CoordinateMapper.quartzToDisplayLocal(
                quartzRect,
                displayQuartzBounds: snapshot.quartzBounds
            )
            let imageSize = CGSize(width: snapshot.image.width, height: snapshot.image.height)
            let pixelRect = CoordinateMapper.displayLocalToPixel(
                displayLocal,
                scaleFactor: snapshot.scaleFactor,
                imageSize: imageSize
            )

            guard let cropped = snapshot.image.cropping(to: pixelRect.rawValue) else {
                throw TransFrameError.captureError(message: "Failed to crop image from display buffer")
            }
            return cropped
        }

        // 2. Multi-Display Compositing: selection spans across multiple displays
        let maxScale = containingSnapshots.map(\.scaleFactor).max() ?? 2.0
        let targetPixelWidth = Int(ceil(r.width * maxScale))
        let targetPixelHeight = Int(ceil(r.height * maxScale))

        guard targetPixelWidth > 0 && targetPixelHeight > 0 else {
            throw TransFrameError.captureError(message: "Invalid target compositing dimensions")
        }

        guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(
                data: nil,
                width: targetPixelWidth,
                height: targetPixelHeight,
                bitsPerComponent: 8,
                bytesPerRow: 0,
                space: colorSpace,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
              ) else {
            throw TransFrameError.captureError(message: "Failed to create compositing context")
        }

        context.clear(CGRect(x: 0, y: 0, width: targetPixelWidth, height: targetPixelHeight))

        for snapshot in containingSnapshots {
            let intersection = snapshot.quartzBounds.rawValue.intersection(r)
            guard !intersection.isNull && intersection.width > 0 && intersection.height > 0 else {
                continue
            }

            // Local display crop
            let localIntersection = CoordinateMapper.quartzToDisplayLocal(
                QuartzGlobalRect(intersection),
                displayQuartzBounds: snapshot.quartzBounds
            )
            let imageSize = CGSize(width: snapshot.image.width, height: snapshot.image.height)
            let pixelCrop = CoordinateMapper.displayLocalToPixel(
                localIntersection,
                scaleFactor: snapshot.scaleFactor,
                imageSize: imageSize
            )

            guard let croppedPart = snapshot.image.cropping(to: pixelCrop.rawValue) else {
                continue
            }

            // Draw into target context at relative offset
            let relativeX = (intersection.origin.x - r.origin.x) * maxScale
            // Quartz context Y flip: in CGContext, (0,0) is bottom-left
            let relativeY = (r.maxY - intersection.maxY) * maxScale
            let destRect = CGRect(
                x: relativeX,
                y: relativeY,
                width: intersection.width * maxScale,
                height: intersection.height * maxScale
            )

            context.draw(croppedPart, in: destRect)
        }

        guard let composited = context.makeImage() else {
            throw TransFrameError.captureError(message: "Failed to generate composited image")
        }

        return composited
    }
}
