<p align="center">
  <img src="TransFrame/Resources/Assets.xcassets/AppIcon.appiconset/icon_128x128.png" alt="TransFrame" width="128" height="128">
</p>

<h1 align="center">TransFrame</h1>

<p align="center">
  macOS menu bar app for screenshot translation with OCR, multi-engine translation, text selection translation, and translate-and-insert features
</p>

<p align="center">
  <a href="https://github.com/hubo1989/TransFrame/releases"><img src="https://img.shields.io/badge/version-1.0.0-blue.svg" alt="Version"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <a href="https://www.apple.com/macos/"><img src="https://img.shields.io/badge/macOS-13.0%2B-brightgreen.svg" alt="macOS"></a>
  <a href="https://swift.org/"><img src="https://img.shields.io/badge/Swift-6.0-orange.svg" alt="Swift"></a>
</p>

<p align="center">
  <a href="README_CN.md">简体中文</a>
</p>

## ✨ Features

### Screenshot Capture
- **Region Capture** - Select any area of the screen to capture
- **Full Screen Capture** - Capture the entire screen with one click
- **Translation Mode** - Translate directly after capture, no extra steps needed
- **Multi-Monitor Support** - Automatic detection and support for multiple displays
- **Retina Display Optimized** - Perfect support for high-resolution displays

### 🆕 Text Translation
- **Text Selection Translation** - Select any text and translate with a popup result window
- **Translate and Insert** - Replace selected text with translation (bypasses input method)
- **Independent Language Settings** - Separate target language configuration for translate-and-insert

### OCR Text Recognition
- **Apple Vision** - Native OCR, no additional configuration required
- **PaddleOCR** - Optional external engine with better Chinese recognition

### Multi-Engine Translation
Two converged modes: **ordered fallback chain** (up to 5 engines, try in order until one succeeds) and **parallel compare** (2–4 engines side by side with per-engine latency).
- **Apple Translation** - Built-in system translation, works offline
- **MTranServer** - Self-hosted translation server for high-quality translation
- **OpenAI-compatible** - OpenAI official / Ollama local / custom endpoints
- **Claude / Gemini / Google / DeepL / Baidu** - Cloud translation APIs
- Real availability probing: status dots reflect actual engine reachability

### Annotation Tools
- Rectangle selection
- Arrow annotation
- Freehand drawing
- Text annotation
- Screenshot cropping

### Other Features
- **Translation History** - Save translation records with search and export
- **Bilingual Display** - Side-by-side original and translated text
- **Overlay Display** - Translation results displayed directly on the screenshot
- **Custom Shortcuts** - Global hotkeys for quick capture and translation
- **Menu Bar Quick Access** - All features accessible from menu bar
- **Multi-Language Support** - Support for 25+ languages

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Cmd+Shift+3` | Capture Full Screen |
| `Cmd+Shift+4` | Capture Selection (default) |
| `Cmd+Shift+T` | Translation Mode (translate after capture) |
| `Cmd+Shift+Y` | Text Selection Translation |
| `Cmd+Shift+I` | Translate and Insert |

> All shortcuts can be customized in Settings

## Preview Window Shortcuts

| Shortcut | Action |
|----------|--------|
| `Enter` / `Cmd+S` | Save Screenshot |
| `Cmd+C` | Copy to Clipboard |
| `Escape` | Close Window / Cancel Crop |
| `R` / `1` | Rectangle Tool |
| `D` / `2` | Freehand Tool |
| `A` / `3` | Arrow Tool |
| `T` / `4` | Text Tool |
| `C` | Crop Mode |
| `Cmd+Z` | Undo |
| `Cmd+Shift+Z` | Redo |

## 📦 Requirements

- macOS 13.0 (Ventura) or later
- Screen Recording permission (prompted on first use)
- Accessibility permission (required for text translation features)

## Download & Installation

Download the latest version from the [Releases](../../releases) page.

> ⚠️ **Note: The app is not signed by Apple Developer**
>
> Since there's no Apple Developer account, the app is not code-signed. On first launch, macOS may show "cannot be opened" or "developer cannot be verified".
>
> **Solutions** (choose one):
>
> **Method 1 - Terminal Command (Recommended)**
> ```bash
> xattr -rd com.apple.quarantine /Applications/TransFrame.app
> ```
>
> **Method 2 - System Settings**
> 1. Open "System Settings" → "Privacy & Security"
> 2. Find the notification about TransFrame under "Security"
> 3. Click "Open Anyway"
>
> Either method only needs to be done once, after which the app can be used normally.

## 🔧 Tech Stack

- **Swift 6.0** - Modern Swift language features with strict concurrency checking
- **SwiftUI + AppKit** - Declarative UI combined with native macOS components
- **ScreenCaptureKit** - System-level screen recording and capture
- **Vision** - Apple native OCR text recognition
- **Translation** - Apple system translation framework
- **CoreGraphics** - Image processing and rendering

## 📁 Project Structure

```text
TransFrame/
├── App/                    # App entry point and coordinators
│   ├── AppDelegate.swift
│   └── Coordinators/       # Feature coordinators
│       ├── CaptureCoordinator.swift
│       ├── TextTranslationCoordinator.swift
│       └── HotkeyCoordinator.swift
├── Features/               # Feature modules
│   ├── Capture/           # Screenshot capture
│   ├── Preview/           # Preview and annotation
│   ├── TextTranslation/   # Text translation
│   ├── Overlay/           # Translation overlay
│   ├── BilingualResult/   # Bilingual result display
│   ├── History/           # Translation history
│   ├── Settings/          # Settings UI
│   └── MenuBar/           # Menu bar control
├── Services/              # Business services
│   ├── Protocols/         # Service protocols (dependency injection)
│   ├── OCREngine/         # OCR engines
│   ├── Translation/       # Translation services
│   └── VLMProvider/       # Vision-language models
├── Models/                # Data models
└── Resources/             # Resource files
```

## 🛠️ Build from Source

```bash
# Clone the repository
git clone https://github.com/hubo1989/TransFrame.git
cd TransFrame

# Open in Xcode
open TransFrame.xcodeproj

# Or build from command line
xcodebuild -project TransFrame.xcodeproj -scheme TransFrame
```

## 📝 Changelog

### v1.0.0 (TransFrame Initial Release)
- 🎉 Rebranded the application from **ScreenTranslate** to **TransFrame**
- ✨ Changed default Bundle Identifier to `com.transframe.app`
- ⚙️ Completely restructured project directories and files to match the new brand
- 🧪 Upgraded unit tests to be fully isolated from local environments

### v1.4.1
- 🐛 Fixed duplicate text segments in VLM responses
- 🐛 Simplified toolbar UI
- 🐛 Fixed CGColorSpace compatibility for older macOS versions
- ✨ Added new annotation tools (shapes, highlighter)
- ✨ Added pinned window feature for annotation mode

### v1.3.0
- ✨ Added About menu with version, license, and acknowledgements
- ✨ Integrated Sparkle auto-update framework
- ✨ Added GitHub Actions CI/CD for automated releases
- 📚 Translated README to English

### v1.2.0
- ✨ Added unified EngineIdentifier for standard and compatible engines
- ✨ Added multi-instance support for OpenAI-compatible engines
- ✨ Optimized engine selection UI and added Gemini support
- ✨ Improved prompt editor UX with copyable variables
- ✨ Improved engine config UX with API key links
- ✨ Moved prompt configuration to dedicated sidebar tab
- ✨ Implemented multi-translation engine support
- 🐛 Fixed quick switch order editing
- 🐛 Improved multi-engine settings interface
- 🌐 Added Chinese localization for multi-engine settings

### v1.1.0
- ✨ Added text selection translation feature
- ✨ Added translate and insert feature
- ✨ Menu bar shortcuts synced with settings
- 🏗️ Architecture refactoring: AppDelegate split into 3 Coordinators
- 🧪 Added unit test coverage
- 🐛 Fixed Retina display issues
- 🐛 Fixed translate-and-insert language settings not applying

### v1.0.2
- 🐛 Deep fix for Retina display scaling issues

### v1.0.1
- 🎉 Initial release

## 🤝 Contributing

Issues and Pull Requests are welcome!

## 📄 License

MIT License - See [LICENSE](LICENSE) file for details

---

Made with Swift for macOS
