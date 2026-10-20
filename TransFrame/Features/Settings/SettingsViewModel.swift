import Foundation
import SwiftUI
import AppKit
import Carbon.HIToolbox
@preconcurrency import ScreenCaptureKit
import PermissionFlow

// MARK: - Shortcut Recording Type

/// Represents which shortcut is currently being recorded
enum ShortcutRecordingType: Equatable {
    case fullScreen
    case selection
    case translationMode
    case textSelectionTranslation
    case translateAndInsert
}

/// ViewModel for the Settings view.
/// Manages user preferences and provides bindings for the settings UI.
@MainActor
@Observable
final class SettingsViewModel {
    // MARK: - Properties

    /// Reference to shared app settings
    let settings: AppSettings

    /// Reference to app delegate for hotkey re-registration
    private weak var appDelegate: AppDelegate?

    /// Manages per-launch credential authorization.
    let credentialAuth = CredentialAuthManager.shared

    /// The type of shortcut currently being recorded (nil if not recording)
    var recordingType: ShortcutRecordingType?

    /// Temporary storage for shortcut recording
    var recordedShortcut: KeyboardShortcut?

    // MARK: - Backward Compatibility Properties for UI

    /// Whether full screen shortcut is being recorded (for UI binding)
    var isRecordingFullScreenShortcut: Bool {
        recordingType == .fullScreen
    }

    /// Whether selection shortcut is being recorded (for UI binding)
    var isRecordingSelectionShortcut: Bool {
        recordingType == .selection
    }

    /// Whether translation mode shortcut is being recorded (for UI binding)
    var isRecordingTranslationModeShortcut: Bool {
        recordingType == .translationMode
    }

    /// Whether text selection translation shortcut is being recorded (for UI binding)
    var isRecordingTextSelectionTranslationShortcut: Bool {
        recordingType == .textSelectionTranslation
    }

    /// Whether translate and insert shortcut is being recorded (for UI binding)
    var isRecordingTranslateAndInsertShortcut: Bool {
        recordingType == .translateAndInsert
    }

    /// Error message to display
    var errorMessage: String?

    /// Whether to show error alert
    var showErrorAlert = false

    /// Screen recording permission status
    var hasScreenRecordingPermission: Bool = false

    /// Accessibility permission status
    var hasAccessibilityPermission: Bool = false

    /// Folder access permission status
    var hasFolderAccessPermission: Bool = false

    /// Whether permission check is in progress
    var isCheckingPermissions: Bool = false

    /// Task for permission checking (stored for cancellation)
    private var permissionCheckTask: Task<Void, Never>?

    /// Type of permission being requested
    enum PermissionType {
        case screenRecording
        case accessibility
    }

    /// Whether PaddleOCR is installed
    var isPaddleOCRInstalled: Bool = false

    /// Whether PaddleOCR installation is in progress
    var isInstallingPaddleOCR: Bool = false

    /// PaddleOCR installation error message
    var paddleOCRInstallError: String?

    /// PaddleOCR version if installed
    var paddleOCRVersion: String?

    // MARK: - PaddleOCR Settings

    /// PaddleOCR mode: fast or precise
    var paddleOCRMode: PaddleOCRMode {
        get { settings.paddleOCRMode }
        set { settings.paddleOCRMode = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.paddleOCRMode) }
    }

    /// Whether to use cloud API
    var paddleOCRUseCloud: Bool {
        get { settings.paddleOCRUseCloud }
        set { settings.paddleOCRUseCloud = newValue; settings.save(newValue, forKey: AppSettings.Keys.paddleOCRUseCloud) }
    }

    /// Cloud API base URL
    var paddleOCRCloudBaseURL: String {
        get { settings.paddleOCRCloudBaseURL }
        set { settings.paddleOCRCloudBaseURL = newValue }
    }

    /// Cloud API key
    var paddleOCRCloudAPIKey: String {
        get { settings.paddleOCRCloudAPIKey }
        set { settings.paddleOCRCloudAPIKey = newValue }
    }

    /// Cloud API model ID
    var paddleOCRCloudModelId: String {
        get { settings.paddleOCRCloudModelId }
        set { settings.paddleOCRCloudModelId = newValue }
    }

    /// Local VL model directory (for vllm backend)
    var paddleOCRLocalVLModelDir: String {
        get { settings.paddleOCRLocalVLModelDir }
        set { settings.paddleOCRLocalVLModelDir = newValue }
    }

    // MARK: - VLM Test State

    /// Whether VLM API test is in progress
    var isTestingVLM = false

    /// VLM API test result message
    var vlmTestResult: String?

    /// Whether VLM test was successful
    var vlmTestSuccess: Bool = false

    // MARK: - MTranServer Test State

    /// Whether MTranServer test is in progress
    var isTestingMTranServer = false

    /// MTranServer test result message
    var mtranTestResult: String?

    /// Whether MTranServer test was successful
    var mtranTestSuccess: Bool = false

    // MARK: - Computed Properties (Bindings to AppSettings)

    /// Save location URL
    var saveLocation: URL {
        get { settings.saveLocation }
        set { settings.saveLocation = newValue }
    }

    /// Save location display path
    var saveLocationPath: String {
        saveLocation.path.replacingOccurrences(of: NSHomeDirectory(), with: "~")
    }

    /// Default export format
    var defaultFormat: ExportFormat {
        get { settings.defaultFormat }
        set { settings.defaultFormat = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.defaultFormat) }
    }

    /// JPEG quality (0.0-1.0)
    var jpegQuality: Double {
        get { settings.jpegQuality }
        set { settings.jpegQuality = newValue; settings.save(newValue, forKey: AppSettings.Keys.jpegQuality) }
    }

    /// JPEG quality as percentage (0-100)
    var jpegQualityPercentage: Double {
        get { jpegQuality * 100 }
        set { jpegQuality = newValue / 100 }
    }

    /// HEIC quality (0.0-1.0)
    var heicQuality: Double {
        get { settings.heicQuality }
        set { settings.heicQuality = newValue; settings.save(newValue, forKey: AppSettings.Keys.heicQuality) }
    }

    /// HEIC quality as percentage (0-100)
    var heicQualityPercentage: Double {
        get { heicQuality * 100 }
        set { heicQuality = newValue / 100 }
    }

    /// Full screen capture shortcut
    var fullScreenShortcut: KeyboardShortcut {
        get { settings.fullScreenShortcut }
        set {
            settings.fullScreenShortcut = newValue
            appDelegate?.updateHotkeys()
            settings.saveShortcut(newValue, forKey: AppSettings.Keys.fullScreenShortcut)
        }
    }

    /// Selection capture shortcut
    var selectionShortcut: KeyboardShortcut {
        get { settings.selectionShortcut }
        set {
            settings.selectionShortcut = newValue
            appDelegate?.updateHotkeys()
            settings.saveShortcut(newValue, forKey: AppSettings.Keys.selectionShortcut)
        }
    }

    /// Translation mode shortcut
    var translationModeShortcut: KeyboardShortcut {
        get { settings.translationModeShortcut }
        set {
            settings.translationModeShortcut = newValue
            appDelegate?.updateHotkeys()
            settings.saveShortcut(newValue, forKey: AppSettings.Keys.translationModeShortcut)
        }
    }

    /// Text selection translation shortcut
    var textSelectionTranslationShortcut: KeyboardShortcut {
        get { settings.textSelectionTranslationShortcut }
        set {
            settings.textSelectionTranslationShortcut = newValue
            appDelegate?.updateHotkeys()
            settings.saveShortcut(newValue, forKey: AppSettings.Keys.textSelectionTranslationShortcut)
        }
    }

    /// Translate and insert shortcut
    var translateAndInsertShortcut: KeyboardShortcut {
        get { settings.translateAndInsertShortcut }
        set {
            settings.translateAndInsertShortcut = newValue
            appDelegate?.updateHotkeys()
            settings.saveShortcut(newValue, forKey: AppSettings.Keys.translateAndInsertShortcut)
        }
    }

    /// Annotation stroke color
    var strokeColor: Color {
        get { settings.strokeColor.color }
        set { settings.strokeColor = CodableColor(newValue); settings.saveColor(CodableColor(newValue), forKey: AppSettings.Keys.strokeColor) }
    }

    /// Annotation stroke width
    var strokeWidth: CGFloat {
        get { settings.strokeWidth }
        set { settings.strokeWidth = newValue; settings.save(newValue, forKey: AppSettings.Keys.strokeWidth) }
    }

    /// Text annotation font size
    var textSize: CGFloat {
        get { settings.textSize }
        set { settings.textSize = newValue; settings.save(newValue, forKey: AppSettings.Keys.textSize) }
    }

    /// OCR engine type
    var ocrEngine: OCREngineType {
        get { settings.ocrEngine }
        set { settings.ocrEngine = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.ocrEngine) }
    }

    /// Translation engine type
    var translationEngine: TranslationEngineType {
        get { settings.translationEngine }
        set { settings.translationEngine = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.translationEngine) }
    }

    /// Translation display mode
    var translationMode: TranslationMode {
        get { settings.translationMode }
        set { settings.translationMode = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.translationMode) }
    }

    /// Translation source language
    var translationSourceLanguage: TranslationLanguage {
        get { settings.translationSourceLanguage }
        set { settings.translationSourceLanguage = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.translationSourceLanguage) }
    }

    /// Translation target language
    var translationTargetLanguage: TranslationLanguage? {
        get { settings.translationTargetLanguage }
        set {
            settings.translationTargetLanguage = newValue
            if let value = newValue {
                settings.save(value.rawValue, forKey: AppSettings.Keys.translationTargetLanguage)
            } else {
                UserDefaults.standard.removeObject(forKey: AppSettings.Keys.translationTargetLanguage)
            }
        }
    }

    /// Whether to automatically detect source language
    var translationAutoDetect: Bool {
        get { settings.translationAutoDetect }
        set { settings.translationAutoDetect = newValue; settings.save(newValue, forKey: AppSettings.Keys.translationAutoDetect) }
    }

    /// Available languages for the current translation engine
    var availableSourceLanguages: [TranslationLanguage] {
        TranslationLanguage.allCases
    }

    /// Available target languages for the current translation engine
    var availableTargetLanguages: [TranslationLanguage] {
        TranslationLanguage.allCases.filter { $0 != .auto }
    }

    // MARK: - Translate and Insert Language Configuration

    /// Source language for translate and insert
    var translateAndInsertSourceLanguage: TranslationLanguage {
        get { settings.translateAndInsertSourceLanguage }
        set { settings.translateAndInsertSourceLanguage = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.translateAndInsertSourceLanguage) }
    }

    /// Target language for translate and insert (nil = follow system)
    var translateAndInsertTargetLanguage: TranslationLanguage? {
        get { settings.translateAndInsertTargetLanguage }
        set {
            settings.translateAndInsertTargetLanguage = newValue
            if let value = newValue {
                settings.save(value.rawValue, forKey: AppSettings.Keys.translateAndInsertTargetLanguage)
            } else {
                UserDefaults.standard.removeObject(forKey: AppSettings.Keys.translateAndInsertTargetLanguage)
            }
        }
    }

    // MARK: - VLM Configuration

    /// Temporary editing fields for VLM configuration (saved on click)
    var vlmEditingAPIKey: String = ""
    var vlmEditingBaseURL: String = ""
    var vlmEditingModelName: String = ""
    var vlmEditingOpenAIMode: OpenAIAPIMode = .chat
    var vlmJustSaved: Bool = false

    var vlmProvider: VLMProviderType {
        get { settings.vlmProvider }
        set {
            settings.vlmProvider = newValue
            loadVLMEditingState(for: newValue)
            vlmTestResult = nil
            vlmTestSuccess = false
        }
    }

    var vlmAPIKey: String {
        get { vlmEditingAPIKey }
        set { vlmEditingAPIKey = newValue }
    }

    var vlmBaseURL: String {
        get { vlmEditingBaseURL }
        set { vlmEditingBaseURL = newValue }
    }

    var vlmModelName: String {
        get { vlmEditingModelName }
        set { vlmEditingModelName = newValue }
    }

    var vlmOpenAIMode: OpenAIAPIMode {
        get { vlmEditingOpenAIMode }
        set { vlmEditingOpenAIMode = newValue }
    }

    var hasUnsavedVLMChanges: Bool {
        let savedBaseURL = settings.vlmBaseURL(for: vlmProvider)
        let savedAPIKey = settings.vlmAPIKey(for: vlmProvider)
        let savedModelName = settings.vlmModelName(for: vlmProvider)
        if vlmEditingBaseURL != savedBaseURL || vlmEditingAPIKey != savedAPIKey || vlmEditingModelName != savedModelName {
            return true
        }
        if vlmProvider == .openai && vlmEditingOpenAIMode != settings.vlmOpenAIMode {
            return true
        }
        return false
    }

    func loadVLMEditingState(for provider: VLMProviderType) {
        vlmEditingBaseURL = settings.vlmBaseURL(for: provider)
        vlmEditingAPIKey = settings.vlmAPIKey(for: provider)
        vlmEditingModelName = settings.vlmModelName(for: provider)
        if provider == .openai {
            vlmEditingOpenAIMode = settings.vlmOpenAIMode
        }
    }

    func saveVLMConfig() {
        settings.setVLMBaseURL(vlmEditingBaseURL, for: vlmProvider)
        settings.setVLMAPIKey(vlmEditingAPIKey, for: vlmProvider)
        settings.setVLMModelName(vlmEditingModelName, for: vlmProvider)
        if vlmProvider == .openai {
            settings.vlmOpenAIMode = vlmEditingOpenAIMode
        }
        vlmJustSaved = true
        Task { @MainActor in
            try? await Task.sleep(for: .seconds(2))
            self.vlmJustSaved = false
        }
    }

    var currentVLMRequiresAPIKey: Bool {
        settings.vlmProvider.requiresAPIKey
    }

    var currentVLMProviderDescription: String {
        settings.vlmProvider.providerDescription
    }

    // MARK: - Translation Workflow Configuration

    var preferredTranslationEngine: PreferredTranslationEngine {
        get { settings.preferredTranslationEngine }
        set { settings.preferredTranslationEngine = newValue; settings.save(newValue.rawValue, forKey: AppSettings.Keys.preferredTranslationEngine) }
    }

    var mtranServerURL: String {
        get { settings.mtranServerURL }
        set {
            settings.mtranServerURL = newValue
            settings.save(newValue, forKey: AppSettings.Keys.mtranServerURL)
            // Clear test result when URL changes
            mtranTestResult = nil
            mtranTestSuccess = false
        }
    }

    var translationFallbackEnabled: Bool {
        get { settings.translationFallbackEnabled }
        set { settings.translationFallbackEnabled = newValue; settings.save(newValue, forKey: AppSettings.Keys.translationFallbackEnabled) }
    }

    // MARK: - Validation Ranges

    /// Valid range for stroke width
    static let strokeWidthRange: ClosedRange<CGFloat> = 1.0...20.0

    /// Valid range for text size
    static let textSizeRange: ClosedRange<CGFloat> = 8.0...72.0

    /// Valid range for JPEG quality
    static let jpegQualityRange: ClosedRange<Double> = 0.1...1.0

    /// Valid range for HEIC quality
    static let heicQualityRange: ClosedRange<Double> = 0.1...1.0

    // MARK: - Initialization

    init(settings: AppSettings = .shared, appDelegate: AppDelegate? = nil) {
        self.settings = settings
        self.appDelegate = appDelegate
        loadVLMEditingState(for: settings.vlmProvider)
        refreshPaddleOCRStatus()
        Task {
            await credentialAuth.refreshCredentialPresence()
        }
    }

    // MARK: - Credential Authorization

    func unlockCredentialAccess() {
        Task {
            await credentialAuth.refreshCredentialPresence()
            let unlocked = await credentialAuth.authenticate()
            if unlocked {
                loadVLMEditingState(for: vlmProvider)
            } else if case .failed(let message) = credentialAuth.state {
                errorMessage = message
                showErrorAlert = true
            }
        }
    }

    // MARK: - Permission Checking

    /// Checks all required permissions and updates status
    func checkPermissions() {
        isCheckingPermissions = true

        hasAccessibilityPermission = PermissionStatusRegistry.provider(for: .accessibility).authorizationState() == .granted
        hasFolderAccessPermission = checkFolderAccess(to: saveLocation)
        hasScreenRecordingPermission = PermissionStatusRegistry.provider(for: .screenRecording).authorizationState() == .granted

        isCheckingPermissions = false
    }

    /// Checks if we have write access to the specified folder
    private func checkFolderAccess(to url: URL) -> Bool {
        let fileManager = FileManager.default

        // Check if directory exists and is writable
        var isDirectory: ObjCBool = false
        guard fileManager.fileExists(atPath: url.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            return false
        }

        return fileManager.isWritableFile(atPath: url.path)
    }

    /// Requests screen recording permission
    func requestScreenRecordingPermission() {
        if PermissionStatusRegistry.provider(for: .screenRecording).authorizationState() == .granted {
            hasScreenRecordingPermission = true
            return
        }

        startPermissionCheck(for: .screenRecording)
    }

    /// Requests accessibility permission - triggers system dialog only
    func requestAccessibilityPermission() {
        if PermissionStatusRegistry.provider(for: .accessibility).authorizationState() == .granted {
            hasAccessibilityPermission = true
            return
        }

        // Just start polling, let PermissionFlow handle the drag & drop authorization
        startPermissionCheck(for: .accessibility)
    }

    /// Starts checking for permission status periodically
    private func startPermissionCheck(for type: PermissionType) {
        permissionCheckTask?.cancel()

        permissionCheckTask = Task {
            for _ in 0..<150 {  // Check for up to 30 seconds
                do {
                    try await Task.sleep(for: .milliseconds(200))
                } catch {
                    return
                }

                switch type {
                case .screenRecording:
                    let isGranted = PermissionStatusRegistry.provider(for: .screenRecording).authorizationState() == .granted
                    if isGranted {
                        hasScreenRecordingPermission = true
                        permissionCheckTask = nil
                        return
                    }

                case .accessibility:
                    let isGranted = PermissionStatusRegistry.provider(for: .accessibility).authorizationState() == .granted
                    if isGranted {
                        hasAccessibilityPermission = true
                        permissionCheckTask = nil
                        return
                    }
                }
            }
        }
    }

    /// Requests folder access by showing a folder picker
    func requestFolderAccess() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = true
        panel.prompt = "Grant Access"
        panel.message = "Select the folder where you want to save screenshots"
        panel.directoryURL = saveLocation

        if panel.runModal() == .OK, let url = panel.url {
            // Save the security-scoped bookmark for persistent access
            do {
                let bookmarkData = try url.bookmarkData(
                    options: .withSecurityScope,
                    includingResourceValuesForKeys: nil,
                    relativeTo: nil
                )
                UserDefaults.standard.set(bookmarkData, forKey: "SaveLocationBookmark")
                saveLocation = url
            } catch {
                // If bookmark fails, just save the URL
                saveLocation = url
            }
        }

        // Recheck permissions
        checkPermissions()
    }

    // MARK: - Actions

    /// Shows folder selection panel to choose save location
    func selectSaveLocation() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = true
        panel.prompt = "Select"
        panel.message = "Choose the default location for saving screenshots"
        panel.directoryURL = saveLocation

        if panel.runModal() == .OK, let url = panel.url {
            // Save the security-scoped bookmark for persistent access
            do {
                let bookmarkData = try url.bookmarkData(
                    options: .withSecurityScope,
                    includingResourceValuesForKeys: nil,
                    relativeTo: nil
                )
                UserDefaults.standard.set(bookmarkData, forKey: "SaveLocationBookmark")
            } catch {
                // Ignore bookmark errors
            }
            saveLocation = url
            checkPermissions()
        }
    }

    /// Reveals the save location in Finder
    func revealSaveLocation() {
        NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: saveLocation.path)
    }

    /// Starts recording a keyboard shortcut for the specified type
    /// - Parameter type: The type of shortcut to record
    func startRecording(_ type: ShortcutRecordingType) {
        recordingType = type
        recordedShortcut = nil
    }

    /// Starts recording a keyboard shortcut for full screen capture
    func startRecordingFullScreenShortcut() {
        startRecording(.fullScreen)
    }

    /// Starts recording a keyboard shortcut for selection capture
    func startRecordingSelectionShortcut() {
        startRecording(.selection)
    }

    /// Starts recording a keyboard shortcut for translation mode
    func startRecordingTranslationModeShortcut() {
        startRecording(.translationMode)
    }

    /// Starts recording a keyboard shortcut for text selection translation
    func startRecordingTextSelectionTranslationShortcut() {
        startRecording(.textSelectionTranslation)
    }

    /// Starts recording a keyboard shortcut for translate and insert
    func startRecordingTranslateAndInsertShortcut() {
        startRecording(.translateAndInsert)
    }

    /// Cancels shortcut recording
    func cancelRecording() {
        recordingType = nil
        recordedShortcut = nil
    }

    /// Handles a key event during shortcut recording
    /// - Parameter event: The key event
    /// - Returns: Whether the event was handled
    func handleKeyEvent(_ event: NSEvent) -> Bool {
        guard recordingType != nil else {
            return false
        }

        // Escape cancels recording
        if event.keyCode == UInt16(kVK_Escape) {
            cancelRecording()
            return true
        }

        // Create shortcut from event
        let shortcut = KeyboardShortcut(
            keyCode: UInt32(event.keyCode),
            modifierFlags: event.modifierFlags.intersection([.command, .shift, .option, .control])
        )

        // Validate shortcut
        guard shortcut.isValid else {
            showError("Shortcuts must include Command, Control, or Option")
            return true
        }

        // Check for conflicts with other shortcuts
        let currentShortcut = getCurrentRecordingShortcut()
        if hasShortcutConflict(shortcut, excluding: currentShortcut) {
            showError("This shortcut is already in use")
            return true
        }

        // Apply the shortcut based on recording type
        switch recordingType {
        case .fullScreen:
            fullScreenShortcut = shortcut
        case .selection:
            selectionShortcut = shortcut
        case .translationMode:
            translationModeShortcut = shortcut
        case .textSelectionTranslation:
            textSelectionTranslationShortcut = shortcut
        case .translateAndInsert:
            translateAndInsertShortcut = shortcut
        case .none:
            break
        }

        // End recording
        cancelRecording()
        return true
    }

    /// Resets a shortcut to its default
    func resetFullScreenShortcut() {
        fullScreenShortcut = .fullScreenDefault
    }

    /// Resets selection shortcut to default
    func resetSelectionShortcut() {
        selectionShortcut = .selectionDefault
    }

    /// Resets translation mode shortcut to default
    func resetTranslationModeShortcut() {
        translationModeShortcut = .translationModeDefault
    }

    /// Resets text selection translation shortcut to default
    func resetTextSelectionTranslationShortcut() {
        textSelectionTranslationShortcut = .textSelectionTranslationDefault
    }

    /// Resets translate and insert shortcut to default
    func resetTranslateAndInsertShortcut() {
        translateAndInsertShortcut = .translateAndInsertDefault
    }

    /// Resets all settings to defaults
    func resetAllToDefaults() {
        settings.resetToDefaults()
        appDelegate?.updateHotkeys()
    }

    // MARK: - Shortcut Conflict Detection

    /// Gets the current shortcut being recorded (if any)
    /// - Returns: The current shortcut value, or nil if not recording
    private func getCurrentRecordingShortcut() -> KeyboardShortcut? {
        switch recordingType {
        case .fullScreen: return fullScreenShortcut
        case .selection: return selectionShortcut
        case .translationMode: return translationModeShortcut
        case .textSelectionTranslation: return textSelectionTranslationShortcut
        case .translateAndInsert: return translateAndInsertShortcut
        case .none: return nil
        }
    }

    /// Checks if a shortcut conflicts with existing shortcuts
    /// - Parameters:
    ///   - shortcut: The shortcut to check
    ///   - excluding: A shortcut to exclude from the check (the one being edited)
    /// - Returns: true if there's a conflict
    private func hasShortcutConflict(_ shortcut: KeyboardShortcut, excluding: KeyboardShortcut?) -> Bool {
        let allShortcuts = [
            fullScreenShortcut, selectionShortcut, translationModeShortcut,
            textSelectionTranslationShortcut, translateAndInsertShortcut
        ].filter { $0 != excluding }
        return allShortcuts.contains(shortcut)
    }

    // MARK: - Private Helpers

    /// Shows an error message
    private func showError(_ message: String) {
        errorMessage = message
        showErrorAlert = true
    }

    // MARK: - PaddleOCR Management

    func refreshPaddleOCRStatus() {
        PaddleOCRChecker.resetCache()
        PaddleOCRChecker.checkAvailabilityAsync()

        Task {
            for _ in 0..<20 {
                try? await Task.sleep(for: .milliseconds(250))
                if PaddleOCRChecker.checkCompleted {
                    break
                }
            }
            await MainActor.run {
                isPaddleOCRInstalled = PaddleOCRChecker.isAvailable
                paddleOCRVersion = PaddleOCRChecker.version
                paddleOCRInstallError = nil
            }
        }
    }

    func installPaddleOCR() {
        isInstallingPaddleOCR = true
        paddleOCRInstallError = nil

        Task.detached(priority: .userInitiated) {
            let result = await self.runPipInstall()
            await MainActor.run {
                self.isInstallingPaddleOCR = false
                if let error = result {
                    self.paddleOCRInstallError = error
                } else {
                    self.refreshPaddleOCRStatus()
                }
            }
        }
    }

    nonisolated private func runPipInstall() async -> String? {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        task.arguments = ["pip3", "install", "paddleocr", "paddlepaddle"]

        do {
            let output = try await AsyncProcessRunner(process: task).run()

            if output.terminationStatus != 0 {
                let stderr = String(data: output.stderrData, encoding: .utf8) ?? "Unknown error"
                return stderr.isEmpty ? "Installation failed with exit code \(output.terminationStatus)" : stderr
            }
            return nil
        } catch {
            return error.localizedDescription
        }
    }

    func copyPaddleOCRInstallCommand() {
        let command = "pip3 install paddleocr paddlepaddle"
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(command, forType: .string)
    }

    // MARK: - VLM API Test

    /// Tests the VLM API connectivity with current configuration
    func testVLMAPI() {
        isTestingVLM = true
        vlmTestResult = nil
        vlmTestSuccess = false

        Task {
            do {
                let effectiveBaseURL = vlmEditingBaseURL.isEmpty ? vlmProvider.defaultBaseURL : vlmEditingBaseURL
                let effectiveModel = vlmEditingModelName.isEmpty ? vlmProvider.defaultModelName : vlmEditingModelName

                guard let parsedBaseURL = URL(string: effectiveBaseURL) else {
                    throw ScreenCoderEngineError.invalidConfiguration("Invalid base URL: \(effectiveBaseURL)")
                }
                let baseURL = parsedBaseURL.resolvingLocalhost

                let testResult = try await performVLMConnectivityTest(
                    provider: vlmProvider,
                    baseURL: baseURL,
                    apiKey: vlmEditingAPIKey,
                    modelName: effectiveModel,
                    openAIMode: vlmEditingOpenAIMode
                )

                await MainActor.run {
                    vlmTestSuccess = testResult.success
                    vlmTestResult = testResult.message
                }

            } catch let error as ScreenCoderEngineError {
                await MainActor.run {
                    vlmTestSuccess = false
                    vlmTestResult = error.localizedDescription
                }
            } catch let error as VLMProviderError {
                await MainActor.run {
                    vlmTestSuccess = false
                    vlmTestResult = error.errorDescription ?? error.localizedDescription
                }
            } catch {
                await MainActor.run {
                    vlmTestSuccess = false
                    vlmTestResult = "Connection failed: \(error.localizedDescription)"
                }
            }

            await MainActor.run {
                isTestingVLM = false
            }
        }
    }

    /// Performs actual connectivity test for different VLM providers
    private func performVLMConnectivityTest(
        provider: VLMProviderType,
        baseURL: URL,
        apiKey: String,
        modelName: String,
        openAIMode: OpenAIAPIMode
    ) async throws -> (success: Bool, message: String) {
        switch provider {
        case .openai:
            return try await testOpenAIConnection(baseURL: baseURL, apiKey: apiKey, modelName: modelName, mode: openAIMode)
        case .gemini:
            return try await testGeminiConnection(baseURL: baseURL, apiKey: apiKey, modelName: modelName)
        case .claude:
            return try await testClaudeConnection(baseURL: baseURL, apiKey: apiKey, modelName: modelName)
        case .ollama:
            return try await testLocalConnection(baseURL: baseURL, apiKey: apiKey, modelName: modelName)
        case .paddleocr:
            return try await testPaddleOCRConnection()
        }
    }

    /// Tests PaddleOCR availability - checks cloud mode first, then local
    private func testPaddleOCRConnection() async throws -> (success: Bool, message: String) {
        let settings = AppSettings.shared

        // If cloud mode is enabled, test cloud connectivity first
        if settings.paddleOCRUseCloud {
            let cloudBaseURL = settings.paddleOCRCloudBaseURL.trimmingCharacters(in: .whitespaces)
            guard !cloudBaseURL.isEmpty,
                  let url = URL(string: cloudBaseURL) else {
                throw VLMProviderError.invalidConfiguration("PaddleOCR cloud base URL is not configured")
            }

            // Test cloud API connectivity with a simple request
            var request = URLRequest(url: url)
            request.timeoutInterval = 10

            // Add API key if configured
            let apiKey = settings.paddleOCRCloudAPIKey.trimmingCharacters(in: .whitespaces)
            if !apiKey.isEmpty {
                request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
            }

            do {
                let (_, response) = try await URLSession.shared.data(for: request)
                guard let httpResponse = response as? HTTPURLResponse else {
                    throw VLMProviderError.invalidResponse("Invalid HTTP response from PaddleOCR cloud")
                }
                switch httpResponse.statusCode {
                case 200, 404:  // 404 is acceptable - means server is reachable
                    return (true, "PaddleOCR cloud is reachable")
                case 401, 403:
                    throw VLMProviderError.authenticationFailed
                default:
                    throw VLMProviderError.invalidResponse("HTTP \(httpResponse.statusCode)")
                }
            } catch let error as VLMProviderError {
                throw error
            } catch {
                throw VLMProviderError.invalidConfiguration("PaddleOCR cloud is not reachable: \(error.localizedDescription)")
            }
        }

        // Local mode - check if PaddleOCR is installed
        let isAvailable = await PaddleOCREngine.shared.isAvailable
        if isAvailable {
            return (true, "PaddleOCR is ready")
        } else {
            throw VLMProviderError.invalidConfiguration("PaddleOCR is not installed. Install it using: pip3 install paddleocr paddlepaddle")
        }
    }

    /// Tests OpenAI API connection by sending a tiny test request
    private func testOpenAIConnection(
        baseURL: URL,
        apiKey: String,
        modelName: String,
        mode: OpenAIAPIMode
    ) async throws -> (success: Bool, message: String) {
        let endpointPath = mode == .response ? "responses" : "chat/completions"
        let requestURL = baseURL.path.hasSuffix(endpointPath) ? baseURL : baseURL.appendingPathComponent(endpointPath)
        var request = URLRequest(url: requestURL)
        request.httpMethod = "POST"
        if !apiKey.isEmpty {
            request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        }
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = 10

        let body: [String: Any]
        if mode == .response {
            body = [
                "model": modelName,
                "input": "Test connection."
            ]
        } else {
            body = [
                "model": modelName,
                "messages": [
                    [
                        "role": "user",
                        "content": [
                            ["type": "text", "text": "Test connection."],
                            [
                                "type": "image_url",
                                "image_url": [
                                    "url": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAIAAAAmkwkpAAAAE0lEQVR4nGP8//8/AwwwwVl4OQCWbgMF7ZjH1AAAAABJRU5ErkJggg=="
                                ]
                            ]
                        ]
                    ]
                ],
                "stream": false,
                "max_tokens": 10
            ]
        }
        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        let (_, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse else {
            throw VLMProviderError.invalidResponse("Invalid HTTP response")
        }

        switch httpResponse.statusCode {
        case 200:
            return (true, String(format: NSLocalizedString("settings.vlm.test.success", comment: ""), modelName))
        case 401:
            throw VLMProviderError.authenticationFailed
        case 429:
            throw VLMProviderError.rateLimited(retryAfter: nil, message: "Rate limited. Please try again later.")
        default:
            throw VLMProviderError.invalidResponse("HTTP \(httpResponse.statusCode)")
        }
    }

    /// Tests Gemini connection
    private func testGeminiConnection(baseURL: URL, apiKey: String, modelName: String) async throws -> (success: Bool, message: String) {
        let endpoint: URL
        if baseURL.absoluteString.contains("googleapis.com") {
            var components = URLComponents(url: baseURL.appendingPathComponent("v1beta/models/\(modelName):generateContent"), resolvingAgainstBaseURL: true)
            if !apiKey.isEmpty {
                components?.queryItems = [URLQueryItem(name: "key", value: apiKey)]
            }
            endpoint = components?.url ?? baseURL
        } else {
            endpoint = baseURL.appendingPathComponent("models/\(modelName):generateContent")
        }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if !apiKey.isEmpty {
            request.setValue(apiKey, forHTTPHeaderField: "x-goog-api-key")
        }
        request.timeoutInterval = 10

        let body: [String: Any] = [
            "contents": [
                [
                    "parts": [
                        ["text": "Test connection."]
                    ]
                ]
            ],
            "generationConfig": [
                "maxOutputTokens": 10
            ]
        ]
        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        let (_, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse else {
            throw VLMProviderError.invalidResponse("Invalid HTTP response")
        }

        switch httpResponse.statusCode {
        case 200:
            return (true, String(format: NSLocalizedString("settings.vlm.test.success", comment: ""), modelName))
        case 400, 401, 403:
            throw VLMProviderError.authenticationFailed
        case 429:
            throw VLMProviderError.rateLimited(retryAfter: nil, message: "Rate limited. Please try again later.")
        default:
            throw VLMProviderError.invalidResponse("HTTP \(httpResponse.statusCode)")
        }
    }

    /// Tests Claude API connection by sending a tiny placeholder image request
    private func testClaudeConnection(baseURL: URL, apiKey: String, modelName: String) async throws -> (success: Bool, message: String) {
        var request = URLRequest(url: baseURL.appendingPathComponent("v1/messages"))
        request.httpMethod = "POST"
        if !apiKey.isEmpty {
            request.setValue(apiKey, forHTTPHeaderField: "x-api-key")
        }
        request.setValue("2023-06-01", forHTTPHeaderField: "anthropic-version")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = 10

        let body: [String: Any] = [
            "model": modelName,
            "max_tokens": 10,
            "messages": [
                [
                    "role": "user",
                    "content": [
                        [
                            "type": "image",
                            "source": [
                                "type": "base64",
                                "media_type": "image/png",
                                "data": "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAIAAAAmkwkpAAAAE0lEQVR4nGP8//8/AwwwwVl4OQCWbgMF7ZjH1AAAAABJRU5ErkJggg=="
                            ]
                        ],
                        [
                            "type": "text",
                            "text": "Test connection."
                        ]
                    ]
                ]
            ]
        ]
        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        let (_, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse else {
            throw VLMProviderError.invalidResponse("Invalid HTTP response")
        }

        switch httpResponse.statusCode {
        case 200:
            return (true, String(format: NSLocalizedString("settings.vlm.test.success", comment: ""), modelName))
        case 401:
            throw VLMProviderError.authenticationFailed
        default:
            throw VLMProviderError.invalidResponse("HTTP \(httpResponse.statusCode)")
        }
    }

    /// Tests local model provider connection (compatible with Ollama, LM Studio, vLLM, LocalAI)
    private func testLocalConnection(baseURL: URL, apiKey: String, modelName: String) async throws -> (success: Bool, message: String) {
        // First try standard OpenAI chat completions endpoint
        var request = URLRequest(url: baseURL.appendingPathComponent("v1/chat/completions"))
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if !apiKey.isEmpty {
            request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        }
        request.timeoutInterval = 8

        let chatBody: [String: Any] = [
            "model": modelName,
            "messages": [["role": "user", "content": "hi"]],
            "max_tokens": 5
        ]
        request.httpBody = try JSONSerialization.data(withJSONObject: chatBody)

        do {
            let (_, response) = try await URLSession.shared.data(for: request)
            if let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) {
                return (true, String(format: NSLocalizedString("settings.vlm.test.success", comment: ""), modelName))
            }
        } catch {
            // Fall through to Ollama native endpoint
        }

        // Try Ollama native endpoint: api/generate
        var nativeRequest = URLRequest(url: baseURL.appendingPathComponent("api/generate"))
        nativeRequest.httpMethod = "POST"
        nativeRequest.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if !apiKey.isEmpty {
            nativeRequest.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        }
        nativeRequest.timeoutInterval = 8

        let nativeBody: [String: Any] = [
            "model": modelName,
            "prompt": "Test connection.",
            "images": ["iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAIAAAAmkwkpAAAAE0lEQVR4nGP8//8/AwwwwVl4OQCWbgMF7ZjH1AAAAABJRU5ErkJggg=="],
            "stream": false,
            "options": ["num_predict": 5]
        ]
        nativeRequest.httpBody = try JSONSerialization.data(withJSONObject: nativeBody)

        let (_, response) = try await URLSession.shared.data(for: nativeRequest)
        guard let httpResponse = response as? HTTPURLResponse else {
            throw VLMProviderError.invalidResponse("Invalid HTTP response")
        }

        switch httpResponse.statusCode {
        case 200:
            return (true, String(format: NSLocalizedString("settings.vlm.test.ollama.success", comment: ""), modelName))
        case 404:
            throw VLMProviderError.modelUnavailable("\(modelName). Ensure endpoint and model name are correct.")
        default:
            throw VLMProviderError.invalidResponse("HTTP \(httpResponse.statusCode)")
        }
    }

    // MARK: - MTranServer Connection Test

    /// Tests MTranServer connection with current configuration
    func testMTranServerConnection() {
        isTestingMTranServer = true
        mtranTestResult = nil
        mtranTestSuccess = false

        Task {
            do {
                // Parse URL and update settings temporarily for test
                guard let (host, port) = parseMTranServerURL(mtranServerURL), !host.isEmpty else {
                    throw MTranServerError.invalidURL
                }

                // Save current settings
                let originalHost = settings.mtranServerHost
                let originalPort = settings.mtranServerPort

                // Update settings for test
                settings.mtranServerHost = host
                settings.mtranServerPort = port

                // Reset cache to use new settings
                MTranServerChecker.resetCache()

                // Check availability
                let isAvailable = await MTranServerChecker.checkAvailability()

                // Restore original settings if test is just for checking
                settings.mtranServerHost = originalHost
                settings.mtranServerPort = originalPort

                await MainActor.run {
                    mtranTestSuccess = isAvailable
                    if isAvailable {
                        mtranTestResult = NSLocalizedString("settings.translation.mtran.test.success", comment: "")
                    } else {
                        mtranTestResult = String(
                            format: NSLocalizedString("settings.translation.mtran.test.failed", comment: ""),
                            "Server not responding"
                        )
                    }
                }
            } catch {
                await MainActor.run {
                    mtranTestSuccess = false
                    mtranTestResult = String(
                        format: NSLocalizedString("settings.translation.mtran.test.failed", comment: ""),
                        error.localizedDescription
                    )
                }
            }

            await MainActor.run {
                isTestingMTranServer = false
            }
        }
    }

    /// Parses MTranServer URL to extract host and port
    private func parseMTranServerURL(_ url: String) -> (host: String, port: Int)? {
        let trimmed = url.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }

        // Remove protocol if present
        var hostPart = trimmed
        if hostPart.hasPrefix("http://") {
            hostPart = String(hostPart.dropFirst(7))
        } else if hostPart.hasPrefix("https://") {
            hostPart = String(hostPart.dropFirst(8))
        }

        // Split by colon for port
        if let colonIndex = hostPart.firstIndex(of: ":") {
            let host = String(hostPart[..<colonIndex])
            let portAndPath = String(hostPart[hostPart.index(after: colonIndex)...])
            // Extract only the port number (stop at first non-digit or path separator)
            let portString = portAndPath.prefix { $0.isNumber }
            let port = Int(portString) ?? 8989
            return (host.isEmpty ? "localhost" : host, port)
        } else {
            return (hostPart.isEmpty ? "localhost" : hostPart, 8989)
        }
    }
}

// MARK: - Preset Colors

extension SettingsViewModel {
    /// Preset colors for the color picker
    static let presetColors: [Color] = [
        .red,
        .orange,
        .yellow,
        .green,
        .blue,
        .purple,
        .pink,
        .white,
        .black
    ]
}
