# Windows Translation/OCR Services (Task C)

翻译/OCR 服务层的 C++20 实现目录。Core 纯逻辑（平台无关，Catch2 驱动），
NativeShell 为 Win32/WinRT/UIA/CredMan 实现（`#ifdef _WIN32`，仅 Windows 构建）。
App（`windows/App`）将各模块接入完整用户流程：托盘 + 全局热键 + 截图选区
+ OCR + 翻译 + 结果弹窗。

## 用户流程（v1 集成）

- 启动 `TransFrame.App.exe` 后常驻托盘（无主窗口）。
- `Ctrl+Alt+A` 区域截图：全屏遮罩 → 拖拽选区 / 点击窗口（FR-01/02）→
  截图 → Windows OCR → 视觉行合并 → MTranServer 翻译 → 区域旁弹窗显示
  原文+译文（复制按钮，Esc/失焦关闭）。
- `Ctrl+Alt+F` 全屏截图：主显示器直接进入同一管线。
- `Ctrl+Alt+D` 划词翻译：UIA 优先，剪贴板回退（保存→Ctrl+C→轮询→还原）。
- `Ctrl+Alt+I` 翻译并插入：划词 → 翻译 → 目标重验证（PID/UIA 锚点）→
  仅验证通过才 Ctrl+V（FR-29 绝不盲贴）。
- `Esc` 取消选区；托盘菜单提供相同动作 + 退出。
- 翻译引擎默认 MTranServer `http://localhost:8989`；`%APPDATA%\TransFrame\engines.json`
  （可选，见 `docs/engines-config.md`）配置 9 引擎注册表、兼容多实例、
  场景绑定与三层 Prompt；环境变量 `TRANSFRAME_MTRAN_URL` /
  `TRANSFRAME_TARGET_LANG`（默认 `zh`）可覆盖。

## 目录

```
windows/
├── App/                    # 应用入口（AppMain.cpp：托盘+热键+选区+管线）
├── Core/                   # 纯逻辑：跨平台，无 UI/Win32 依赖
│   ├── include/tf/         # 公共头（所有服务接口；含 EngineConfigStore/HistoryStore）
│   ├── src/                # 实现
│   ├── vendor/nlohmann/    # JSON（与任务 B 共享）
│   └── vendor/sqlite/      # SQLite 3.45.1 amalgamation（FR-30 history.db）
├── NativeShell/            # Win32 实现（仅 _WIN32 编译）
│   ├── include/tf/         # WinHttpTransport / CredManKeyStore / Win32 hosts
│   └── src/                # 捕获链 / 选区覆盖层 / 托盘 / 结果窗 / OCR 胶水
└── Tests/                  # Catch2 v3 + 手工冒烟
    ├── *.cpp               # 单测（纯逻辑，mock transport/provider）
    ├── TestSupport.hpp     # 共享 mock
    ├── vendor/             # catch_amalgamated
    └── manual/             # smoke_translation.cpp（Windows 上手工跑）
```

## 构建（Core 单测 — 任意平台）

macOS/Linux/Windows 均可（Apple clang 21 已验证；MSVC `/std:c++20`、GCC 13+ 等价）。
文件清单与 `CMakeLists.txt` 的 `translation_tests` 目标一致（EngineBootstrap
虽在 `App/`，但平台无关，同样编入）：

```bash
# sqlite3.c 先单独按 C 编译（amalgamation 按 C++ 编译会有大量告警）
clang -x c -c -O1 -w -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
  windows/Core/vendor/sqlite/sqlite3.c -o /tmp/sqlite3.o
clang++ -std=c++20 \
  -I windows/Core -I windows/Core/include -I windows/Core/vendor \
  -I windows/Core/vendor/nlohmann -I windows/App \
  -I windows/Tests -I windows/Tests/vendor \
  windows/Core/src/*.cpp windows/Core/AnnotationDocument.cpp \
  windows/App/EngineBootstrap.cpp \
  windows/Tests/TranslationSchedulerTests.cpp windows/Tests/ProvidersTests.cpp \
  windows/Tests/LLMTranslationCoreTests.cpp windows/Tests/EngineHealthCheckerTests.cpp \
  windows/Tests/OcrAndTextSelectionTests.cpp windows/Tests/EngineBootstrapTests.cpp \
  windows/Tests/AnnotationDocumentTests.cpp \
  windows/Tests/EngineConfigStoreTests.cpp windows/Tests/HotkeySettingsStoreTests.cpp \
  windows/Tests/HistoryStoreTests.cpp \
  windows/Tests/vendor/catch_amalgamated.cpp \
  -o tests_runner && ./tests_runner
```

**不要**把 `windows/NativeShell/src/*.cpp` 编入 — 它们是 `_WIN32`-only；
也不要 glob `windows/Tests/*.cpp`（CoreModels/VisualLineGrouping 等测试
依赖 `tf::core` 实现，不在本目标的文件清单里，混入会链接失败）。

## 构建（Windows 全量，含 NativeShell）

```
cl /std:c++20 /EHsc ^
  /I windows\Core\include /I windows\Core\vendor /I windows\NativeShell\include ^
  windows\Core\src\*.cpp ^
  windows\NativeShell\src\*.cpp ^
  windows\Tests\manual\smoke_translation.cpp ^
  /link winhttp.lib credui.lib advapi32.lib user32.lib ole32.lib windowsapp.lib
```

（UIA/WinRT 需 `windowsapp.lib`；具体见各 NativeShell 源文件顶部注释。）

## 测试

```bash
./tests_runner                     # 全部
./tests_runner "[scheduler]"       # 按标签过滤
./tests_runner "[health][cache]"   # 组合过滤
```

当前：**合并后以本地运行与 CI 为准**（macOS arm64, Apple clang 21；四线套件：
bootstrap / AnnotationDocument / EngineConfigStore / HotkeySettingsStore / HistoryStore）。

## 引擎文档

每引擎请求/响应/错误映射：`docs/windows/engines/<engine>.md` ×9
（mtran / openai / claude / gemini / ollama / google / deepl / baidu / custom）。

## Windows 图标、右键菜单与自动更新

EXE、托盘、结果窗口和 MSIX 开始菜单使用同一套 TransFrame 图标，
资源在 `App/Assets`（ICO 含 16/24/32/48/64/128/256 像素）。
托盘左键、右键均可打开菜单；Explorer 重启后会重新注册托盘图标。
这里的右键菜单是**系统托盘菜单**，不注册资源管理器文件右键扩展。

自动更新采用 Windows App Installer：安装 `.appinstaller` 后，系统在启动时检查，
并通过后台任务定期检查更新（系统通常约每 8 小时一次，受系统策略和网络影响）。
托盘「检查更新」会异步查询当前安装关联的更新源，发现新版后可打开系统安装程序。
网络错误、无更新源和免安装版分别显示明确提示。
直接运行 EXE 或只安装 MSIX 不会自动关联更新源；请通过 `.appinstaller` 安装。

### 生成签名安装包及更新源

在 Windows SDK 开发者命令行中构建 Release，再运行（示例值必须替换）：

```powershell
msbuild windows/TransFrame.sln /m /p:Configuration=Release /p:Platform=x64
python windows/scripts/build_msix.py `
  --exe windows/build/x64/Release/TransFrame.App/TransFrame.App.exe `
  --output windows/dist/x64-1.0.5 `
  --version 1.0.5.0 --arch x64 `
  --vclibs C:/downloads/Microsoft.VCLibs.x64.14.00.Desktop.appx `
  --publisher "CN=Your actual certificate subject" `
  --certificate-thumbprint YOUR_40_HEX_CERTIFICATE_THUMBPRINT `
  --base-url https://your-download-host.example/windows
```

- 证书需已存在于 CurrentUser/My，Publisher 必须与证书 Subject 完全相符。
  脚本不会生成证书、修改信任设置或上传文件；签名验证失败会停止。
- ARM64 使用 `/p:Platform=ARM64`、对应 EXE 和 `--arch arm64`，输出独立更新源。
- 每次发布递增四段版本号，保持包 Name、Publisher、架构及更新源 URL 稳定。
- 先上传版本化 `.msix` 和生成目录中的 VCLibs `.appx`，再替换同架构的 `TransFrame-x64.appinstaller`
  或 `TransFrame-arm64.appinstaller`。两者必须可通过 HTTPS 下载。
- 下载站应为 `.appinstaller` 返回 `application/appinstaller`，为 `.msix` 返回
  `application/msix`；首次安装需要 App Installer 和受系统信任的签名证书。
- 从微软官方下载匹配架构的 VCLibs Desktop 运行库（例如
  `https://aka.ms/Microsoft.VCLibs.x64.14.00.Desktop.appx`），用 `--vclibs` 传入。
  脚本验证签名、包标识和架构，读取实际版本并生成固定下载路径，避免运行库
  下载别名更新后与描述文件里的版本不匹配。
- `App/TransFrame.Package.wapproj` 是旧包装骨架；此流程直接调用 SDK 的
  MakeAppx / SignTool，并使用 `App/Package.appxmanifest` 生成实际包清单。

### 验证

```bash
python3 -m unittest discover -s windows/scripts -v
```

菜单回归在 `NativeShell/tests/TrayMenuTests.cpp`，覆盖取消、首尾命令 ID、
禁用命令拦截和「检查更新」可用状态；安装描述测试覆盖双架构、版本与
Publisher 一致性、图标引用、后台更新设置、错误 URL/版本拒绝。

Windows 实机验收（macOS 单测无法替代）：

1. 构建 x64 / ARM64，确认 EXE 文件图标、托盘图标、结果窗口图标正确。
2. 左/右键托盘：全屏截图和退出分别触发正确动作；按 Esc 或点菜单外关闭。
   重启 Explorer，确认图标恢复且菜单仍可用。
3. 直接运行 EXE，点「检查更新」应提示安装 MSIX；不会声称已是最新版。
4. 将受信任签名的 N 版 MSIX 与 `.appinstaller` 放到 HTTPS 站点，通过
   `.appinstaller` 安装；开始菜单应显示图标。
5. 发布同证书、同架构、较高版本 N+1，替换稳定 `.appinstaller`；手动检查
   应发现新版，允许打开系统安装程序，取消则保持当前版本。
6. 保持 N 版安装，验证系统后台/下次启动升级到 N+1，确认安装后的版本号。
7. 断网、不可达更新源、签名不匹配分别验证：不声称最新、不安装不可信包。

当前仓库未配置生产 Windows 签名证书或下载站，此步骤不代表已发布更新服务。
