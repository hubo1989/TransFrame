import Foundation
import AppKit
import CoreGraphics
import os

/// Orchestrates the entire lifecycle of a capture session:
/// 1. Instant screen freeze
/// 2. Multi-monitor overlay display
/// 3. In-place interaction & annotation
/// 4. Dispathing to In-Place Pin, Copy, OCR, Translation, or Editor.
@MainActor
final class CaptureSessionController: NSObject, SelectionOverlayDelegate {
    static let shared = CaptureSessionController()

    enum CaptureMode {
        case region
        case screenshotTranslation
        case fullScreen
    }

    var onCaptureComplete: ((CaptureDocument) -> Void)?
    var onTranslationRequested: ((CaptureDocument) -> Void)?
    var onOCRRequested: ((CaptureDocument) -> Void)?

    private var activeOverlays: [SelectionOverlayWindow] = []
    private var currentMode: CaptureMode = .region
    private var currentSnapshotSet: FrozenSnapshotSet?
    private var beautifyConfig: BeautifyConfig?

    private var localKeyMonitor: Any?
    private var globalKeyMonitor: Any?

    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "CaptureSession")

    private override init() {
        super.init()
    }

    /// Triggers screen capture session with instant freeze.
    func startCapture(mode: CaptureMode = .region) {
        // Prevent launching another session if overlays are active
        guard activeOverlays.isEmpty else {
            logger.warning("Capture session already in progress, ignoring trigger")
            return
        }

        self.currentMode = mode
        let triggerTime = ContinuousClock.now

        Task { @MainActor in
            do {
                // Collect existing overlay IDs if any (empty now)
                let excludingIDs = Set(self.activeOverlays.compactMap { CGWindowID($0.windowNumber) })

                // 1. Capture frozen snapshots of all displays in parallel
                let snapshotSet = try await SnapshotCaptureService.shared.captureAllDisplays(
                    excludingWindowIDs: excludingIDs,
                    triggerTime: triggerTime
                )
                self.currentSnapshotSet = snapshotSet

                if mode == .fullScreen {
                    // Fullscreen fast path
                    let primarySnapshot = snapshotSet.snapshots.first(where: { $0.descriptor.isPrimary }) ?? snapshotSet.snapshots.first!

                    let doc = CaptureDocument(
                        baseImage: primarySnapshot.image,
                        sourceScreenRect: primarySnapshot.quartzBounds,
                        sourceDisplay: DisplayInfo(
                            id: primarySnapshot.displayID,
                            name: primarySnapshot.descriptor.localizedName,
                            frame: primarySnapshot.descriptor.frame,
                            scaleFactor: primarySnapshot.descriptor.scaleFactor,
                            isPrimary: true
                        )
                    )
                    CaptureDocumentStore.shared.register(doc)
                    self.dispatchAction(.copy, for: doc)
                    self.onCaptureComplete?(doc)
                    return
                }

                // 2. Instantiate and present full-screen frozen overlays across all screens
                var windows: [SelectionOverlayWindow] = []
                for snapshot in snapshotSet.snapshots {
                    guard let screen = snapshot.screen else { continue }
                    let overlay = SelectionOverlayWindow(
                        screen: screen,
                        snapshot: snapshot,
                        snapshotSet: snapshotSet
                    )
                    overlay.setDelegate(self)
                    windows.append(overlay)
                }

                self.activeOverlays = windows

                // Show all overlays: prioritize the display under the current mouse cursor as the active Key Window
                let currentMouseLocation = NSEvent.mouseLocation
                let primaryOverlay = windows.first { NSMouseInRect(currentMouseLocation, $0.targetScreen.frame, false) } ?? windows.first

                for window in windows where window !== primaryOverlay {
                    window.showOverlay(isKey: false)
                }
                primaryOverlay?.showOverlay(isKey: true)

                // Install local & global ESC key monitors as bulletproof fallbacks
                self.setupKeyMonitors()

                // Record freeze visible time
                self.currentSnapshotSet?.freezeVisibleAt = .now
                self.logger.info("All frozen overlays presented successfully")

            } catch {
                self.logger.error("Failed to start capture session: \(error.localizedDescription)")
                self.closeAllOverlays()
            }
        }
    }

    // MARK: - SelectionOverlayDelegate

    func selectionOverlay(
        didConfirmSelection selectionQuartz: QuartzGlobalRect,
        action: SelectionInPlaceToolbar.Action,
        annotations: [Annotation],
        on snapshot: DisplaySnapshot
    ) {
        guard let snapshotSet = currentSnapshotSet else {
            closeAllOverlays()
            return
        }

        let isBeautifyActive = activeOverlays.contains { $0.overlayView?.isBeautifyEnabled == true }
        self.beautifyConfig = isBeautifyActive ? BeautifyConfig(isEnabled: true) : nil

        // Close overlays immediately
        closeAllOverlays()

        do {
            // Non-destructive memory crop from frozen snapshot set (no live capture!)
            let croppedImage = try SnapshotCropper.crop(quartzRect: selectionQuartz, from: snapshotSet)

            let doc = CaptureDocument(
                baseImage: croppedImage,
                sourceScreenRect: selectionQuartz,
                sourceDisplay: DisplayInfo(
                    id: snapshot.displayID,
                    name: snapshot.descriptor.localizedName,
                    frame: snapshot.descriptor.frame,
                    scaleFactor: snapshot.scaleFactor,
                    isPrimary: snapshot.displayID == CGMainDisplayID()
                ),
                annotations: annotations
            )

            CaptureDocumentStore.shared.register(doc)

            // If triggered via screenshot translation shortcut, force translate action
            let finalAction = (currentMode == .screenshotTranslation && action == .copy) ? .translate : action
            dispatchAction(finalAction, for: doc)

        } catch {
            logger.error("Failed to crop snapshot: \(error.localizedDescription)")
        }
    }

    func selectionOverlayDidCancel() {
        closeAllOverlays()
    }

    // MARK: - Action Dispatching

    private func dispatchAction(_ action: SelectionInPlaceToolbar.Action, for document: CaptureDocument) {
        switch action {
        case .copy:
            playCaptureSound()
            let clipboard = ClipboardService()
            do {
                var finalImage = DocumentRenderer.shared.render(document: document) ?? document.baseImage
                if let bConfig = beautifyConfig, bConfig.isEnabled {
                    if let beautified = BeautifyRenderer.shared.render(image: finalImage, config: bConfig) {
                        finalImage = beautified
                    }
                }
                try clipboard.copy(finalImage, annotations: [])
            } catch {
                logger.error("Clipboard copy failed: \(error.localizedDescription)")
            }

        case .pin:
            playCaptureSound()
            PinnedWindowsManager.shared.pin(document: document)

        case .ocr:
            onOCRRequested?(document)

        case .translate:
            onTranslationRequested?(document)

        case .beautify:
            if beautifyConfig == nil {
                beautifyConfig = .default
            }
            beautifyConfig?.isEnabled.toggle()

        case .smartMosaic:
            // Handled inside the selection overlay (adds mosaic annotations in place)
            break

        case .save:
            playCaptureSound()
            let settings = AppSettings.shared
            let exporter = ImageExporter.shared
            let directory = settings.saveLocation
            let format = settings.defaultFormat
            let url = exporter.generateFileURL(in: directory, format: format)
            var finalImage = DocumentRenderer.shared.render(document: document) ?? document.baseImage
            if let bConfig = beautifyConfig, bConfig.isEnabled {
                if let beautified = BeautifyRenderer.shared.render(image: finalImage, config: bConfig) {
                    finalImage = beautified
                }
            }
            do {
                try exporter.save(finalImage, annotations: [], to: url, format: format, beautifyConfig: nil)
            } catch {
                logger.error("Save failed: \(error.localizedDescription)")
            }

        case .upload:
            playCaptureSound()
            Task { @MainActor in
                var finalImage = DocumentRenderer.shared.render(document: document) ?? document.baseImage
                if let bConfig = self.beautifyConfig, bConfig.isEnabled {
                    if let beautified = BeautifyRenderer.shared.render(image: finalImage, config: bConfig) {
                        finalImage = beautified
                    }
                }
                do {
                    _ = try await ImageUploadService.shared.upload(image: finalImage)
                } catch {
                    self.logger.error("Upload failed: \(error.localizedDescription)")
                }
            }

        case .cancel:
            break
        }
    }

    private func closeAllOverlays() {
        removeKeyMonitors()
        for window in activeOverlays {
            window.orderOut(nil)
            window.close()
        }
        activeOverlays.removeAll()
        currentSnapshotSet = nil
    }

    private func setupKeyMonitors() {
        removeKeyMonitors()

        // 1. Local event monitor when app is active
        localKeyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            if event.keyCode == 53 { // ESC
                if let editingOverlay = self?.activeOverlays.first(where: { $0.overlayView?.isEditingText == true }) {
                    editingOverlay.overlayView?.cancelTextInput()
                    return nil
                }
                self?.closeAllOverlays()
                return nil
            }
            return event
        }

        // 2. Global event monitor as bulletproof fallback if another process briefly retains key status
        globalKeyMonitor = NSEvent.addGlobalMonitorForEvents(matching: .keyDown) { [weak self] event in
            if event.keyCode == 53 { // ESC
                DispatchQueue.main.async {
                    self?.closeAllOverlays()
                }
            }
        }
    }

    private func removeKeyMonitors() {
        if let monitor = localKeyMonitor {
            NSEvent.removeMonitor(monitor)
            localKeyMonitor = nil
        }
        if let monitor = globalKeyMonitor {
            NSEvent.removeMonitor(monitor)
            globalKeyMonitor = nil
        }
    }

    private func playCaptureSound() {
        NSSound(named: "Screen Capture")?.play()
    }
}
