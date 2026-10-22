import AppKit
import CoreGraphics
import Vision
import os
@preconcurrency import ScreenCaptureKit

/// Manages a continuous scroll screenshot session, performing hardware-accelerated image registration
/// and vertical stitching using ScreenCaptureKit and Vision framework.
@MainActor
final class ScrollCaptureSession {
    private static let logger = Logger(subsystem: "com.transframe.app", category: "ScrollCapture")

    private let displayID: CGDirectDisplayID
    private let screenRect: CGRect // in global quartz coordinates
    private var isCapturing: Bool = false
    private var isProcessing: Bool = false
    private var timer: Timer?

    private var previousImage: CGImage?
    private var stitchedImage: CGImage?
    private var totalHeight: CGFloat = 0

    var onUpdate: ((CGImage) -> Void)?

    init(displayID: CGDirectDisplayID, screenRect: CGRect) {
        self.displayID = displayID
        self.screenRect = screenRect
    }

    /// Starts continuous capture and image registration loop.
    func start() {
        guard !isCapturing else { return }
        isCapturing = true

        Task { @MainActor in
            if let initial = await self.captureCurrentSlice() {
                self.previousImage = initial
                self.stitchedImage = initial
                self.totalHeight = CGFloat(initial.height)
                self.onUpdate?(initial)
            }
        }

        // 10 FPS capture loop
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            Task { @MainActor in
                await self?.processNextSlice()
            }
        }
    }

    /// Stops continuous capture and returns the final stitched image.
    func finish() -> CGImage? {
        timer?.invalidate()
        timer = nil
        isCapturing = false
        return stitchedImage
    }

    /// Cancels capture and discards stitched buffer.
    func cancel() {
        timer?.invalidate()
        timer = nil
        isCapturing = false
        stitchedImage = nil
        previousImage = nil
    }

    // MARK: - Frame Registration & Stitching

    private func processNextSlice() async {
        guard isCapturing, !isProcessing else { return }
        isProcessing = true
        defer { isProcessing = false }

        guard let currentSlice = await captureCurrentSlice(), let prev = previousImage else { return }

        // Perform translation registration using Apple Vision framework
        let request = VNTranslationalImageRegistrationRequest(targetedCGImage: currentSlice, options: [:])
        let handler = VNImageRequestHandler(cgImage: prev, options: [:])

        do {
            try handler.perform([request])
            guard let observation = request.results?.first as? VNImageTranslationAlignmentObservation else {
                return
            }

            // In Vision coordinates, ty > 0 means current image moved downwards relative to target
            let ty = observation.alignmentTransform.ty
            let dy = CGFloat(ty)

            // Filter out noise or non-scrolling frames
            if dy > 4.0 && dy < CGFloat(currentSlice.height) * 0.9 {
                appendSlice(currentSlice, deltaY: dy)
                previousImage = currentSlice
            }
        } catch {
            Self.logger.debug("Image registration failed: \(error.localizedDescription)")
        }
    }

    private func appendSlice(_ slice: CGImage, deltaY: CGFloat) {
        guard let base = stitchedImage else {
            stitchedImage = slice
            return
        }

        let width = base.width
        let addedHeight = Int(round(deltaY))
        let newTotalHeight = base.height + addedHeight

        guard let colorSpace = base.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB) else { return }
        guard let context = CGContext(
            data: nil,
            width: width,
            height: newTotalHeight,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            return
        }

        // Draw existing base image shifted up
        context.draw(base, in: CGRect(x: 0, y: addedHeight, width: width, height: base.height))

        // Draw newly scrolled bottom slice
        context.draw(slice, in: CGRect(x: 0, y: 0, width: width, height: slice.height))

        if let newStitched = context.makeImage() {
            self.stitchedImage = newStitched
            self.totalHeight = CGFloat(newTotalHeight)
            self.onUpdate?(newStitched)
        }
    }

    private func captureCurrentSlice() async -> CGImage? {
        do {
            let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
            guard let scDisplay = content.displays.first(where: { $0.displayID == self.displayID }) ?? content.displays.first else {
                return nil
            }
            let filter = SCContentFilter(display: scDisplay, excludingWindows: [])
            let config = SCStreamConfiguration()
            config.sourceRect = screenRect
            let scale: CGFloat = NSScreen.main?.backingScaleFactor ?? 2.0
            config.width = max(1, Int(screenRect.width * scale))
            config.height = max(1, Int(screenRect.height * scale))
            config.showsCursor = false
            config.scalesToFit = false
            return try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: config)
        } catch {
            Self.logger.debug("Scroll slice capture failed: \(error.localizedDescription)")
            return nil
        }
    }
}
