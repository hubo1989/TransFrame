<p align="center">
  <img src="TransFrame/Resources/Assets.xcassets/AppIcon.appiconset/icon_128x128.png" alt="TransFrame" width="128" height="128">
</p>

<h1 align="center">TransFrame</h1>

<p align="center">
  macOS 菜单栏截图翻译工具，支持 OCR 识别、多引擎翻译、文本选择翻译和翻译插入
</p>

<p align="center">
  <a href="https://github.com/hubo1989/TransFrame/releases"><img src="https://img.shields.io/badge/version-1.0.0-blue.svg" alt="Version"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <a href="https://www.apple.com/macos/"><img src="https://img.shields.io/badge/macOS-13.0%2B-brightgreen.svg" alt="macOS"></a>
  <a href="https://swift.org/"><img src="https://img.shields.io/badge/Swift-6.0-orange.svg" alt="Swift"></a>
</p>

## ✨ 功能特性

### 截图功能
- **区域截图** - 选择屏幕任意区域进行截图
- **全屏截图** - 一键截取整个屏幕
- **翻译模式** - 截图后直接翻译，无需额外操作
- **多显示器支持** - 自动识别并支持多显示器环境
- **Retina 屏幕优化** - 完美支持高分辨率显示器

### 🆕 文本翻译功能
- **文本选择翻译** - 选中任意文本，一键翻译并弹出结果窗口
- **翻译并插入** - 选中文本翻译后，自动替换为译文（绕过输入法）
- **独立语言设置** - 翻译并插入支持独立的目标语言配置

### OCR 文字识别
- **Apple Vision** - 原生 OCR，无需额外配置
- **PaddleOCR** - 可选外部引擎，中文识别更准确

### 多引擎翻译
两种收敛模式：**顺序退避**（最多 5 个引擎按序尝试，失败自动降级）与**并行对比**（2–4 个引擎同时翻译，逐引擎延迟对比）。
- **Apple 翻译** - 系统内置，离线可用
- **MTranServer** - 自建翻译服务器
- **OpenAI 兼容** - OpenAI 官方 / Ollama 本地 / 自定义端点
- **Claude / Gemini / Google / DeepL / 百度** - 云翻译 API
- 真实可用性探测：状态点反映引擎实际可达性

### 标注工具
- 矩形框选
- 箭头标注
- 手绘涂鸦
- 文字注释
- 截图裁剪

### 其他功能
- **翻译历史** - 保存翻译记录，支持搜索和导出
- **双语对照** - 原文译文并排显示
- **覆盖层显示** - 翻译结果直接显示在截图上方
- **自定义快捷键** - 支持全局快捷键快速截图和翻译
- **菜单栏快捷操作** - 所有功能均可通过菜单栏访问
- **多语言支持** - 支持 25+ 种语言翻译

## ⌨️ 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Cmd+Shift+3` | 全屏截图 |
| `Cmd+Shift+4` | 区域截图翻译（默认） |
| `Cmd+Shift+T` | 翻译模式（截图后直接翻译） |
| `Cmd+Shift+Y` | 文本选择翻译 |
| `Cmd+Shift+I` | 翻译并插入 |

> 所有快捷键均可在设置中自定义

## 预览窗口操作

| 快捷键 | 功能 |
|--------|------|
| `Enter` / `Cmd+S` | 保存截图 |
| `Cmd+C` | 复制到剪贴板 |
| `Escape` | 关闭窗口 / 取消裁剪 |
| `R` / `1` | 矩形工具 |
| `D` / `2` | 手绘工具 |
| `A` / `3` | 箭头工具 |
| `T` / `4` | 文字工具 |
| `C` | 裁剪模式 |
| `Cmd+Z` | 撤销 |
| `Cmd+Shift+Z` | 重做 |

## 📦 安装要求

- macOS 13.0 (Ventura) 或更高版本
- 屏幕录制权限（首次使用时会提示）
- 辅助功能权限（文本翻译功能需要）

## 下载安装

从 [Releases](../../releases) 页面下载最新版本。

> ⚠️ **注意：应用未经过 Apple 开发者签名**
>
> 由于目前没有 Apple Developer 账号，应用未进行代码签名。首次运行时 macOS 会提示「无法打开」或「开发者无法验证」。
>
> **解决方法**（二选一）：
>
> **方法 1 - 终端命令（推荐）**
> ```bash
> xattr -rd com.apple.quarantine /Applications/TransFrame.app
> ```
>
> **方法 2 - 系统设置**
> 1. 打开「系统设置」→「隐私与安全性」
> 2. 在「安全性」部分找到关于 TransFrame 的提示
> 3. 点击「仍要打开」
>
> 两种方法都只需要执行一次，之后可以正常使用。

## 🔧 技术栈

- **Swift 6.0** - 现代 Swift 语言特性，严格并发检查
- **SwiftUI + AppKit** - 声明式 UI 与原生 macOS 组件结合
- **ScreenCaptureKit** - 系统级屏幕录制与截图
- **Vision** - Apple 原生 OCR 文字识别
- **Translation** - Apple 系统翻译框架
- **CoreGraphics** - 图像处理与渲染

## 📁 项目结构

```text
TransFrame/
├── App/                    # 应用入口与协调器
│   ├── AppDelegate.swift
│   └── Coordinators/       # 功能协调器
│       ├── CaptureCoordinator.swift
│       ├── TextTranslationCoordinator.swift
│       └── HotkeyCoordinator.swift
├── Features/               # 功能模块
│   ├── Capture/           # 截图功能
│   ├── Preview/           # 预览与标注
│   ├── TextTranslation/   # 文本翻译
│   ├── Overlay/           # 翻译覆盖层
│   ├── BilingualResult/   # 双语结果展示
│   ├── History/           # 历史记录
│   ├── Settings/          # 设置界面
│   └── MenuBar/           # 菜单栏控制
├── Services/              # 业务服务
│   ├── Protocols/         # 服务协议（依赖注入）
│   ├── OCREngine/         # OCR 引擎
│   ├── Translation/       # 翻译服务
│   └── VLMProvider/       # 视觉语言模型
├── Models/                # 数据模型
└── Resources/             # 资源文件
```

## 🛠️ 构建源码

```bash
# 克隆仓库
git clone https://github.com/hubo1989/TransFrame.git
cd TransFrame

# 用 Xcode 打开
open TransFrame.xcodeproj

# 或命令行构建
xcodebuild -project TransFrame.xcodeproj -scheme TransFrame
```

## 📝 更新日志

### v1.0.0 (TransFrame 首次发布)
- 🎉 应用品牌从 **ScreenTranslate** 彻底更名为 **TransFrame**
- ✨ 默认 Bundle Identifier 变更为 `com.transframe.app`
- ⚙️ 全面重构并重命名了项目目录、文件和底层配置，以适配新品牌名称
- 🧪 升级并重构单元测试，实现完全的环境独立性验证

### v1.3.0
- ✨ 新增关于菜单（版本、许可证、致谢）
- ✨ 集成 Sparkle 自动更新框架
- ✨ 添加 GitHub Actions CI/CD 自动发布
- 📚 README 翻译为英文

### v1.1.0
- ✨ 新增文本选择翻译功能（选中任意文本一键翻译）
- ✨ 新增翻译并插入功能（自动替换选中文本为译文）
- ✨ 菜单栏快捷键与设置同步
- 🏗️ 架构重构：AppDelegate 拆分为 3 个 Coordinator
- 🧪 添加单元测试覆盖
- 🐛 修复 Retina 屏幕显示问题
- 🐛 修复翻译并插入语言设置不生效问题

### v1.0.2
- 🐛 深度修复 Retina 屏幕缩放问题

### v1.0.1
- 🎉 首次发布

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request。

## 📄 许可证

MIT License - 详见 [LICENSE](LICENSE) 文件

---

Made with Swift for macOS
