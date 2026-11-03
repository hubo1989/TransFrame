# Windows 编译 + 测试实跑报告（GitHub Actions）

补验任务：把 A/B/C 三个 Windows 子任务的"编译 + 测试"从本机 clang++ 验证推进到 GitHub Actions windows-latest 实跑留证。全部三个分支最终全绿。

## 最终状态矩阵

| 分支 | MSVC x64（编译+测试） | MSVC ARM64（编译+链接） | GCC ubuntu（编译+测试） | 最终 run |
|---|---|---|---|---|
| A `hubo/win-skeleton-core` | ✅ 68 cases / 261 assertions | ✅ compile+link | ✅ 68 cases / 261 assertions | [41370755812](https://github.com/hubo1989/TransFrame/actions/runs/41370755812) |
| B `hubo/win-capture-shell` | ✅ 60 cases / 235 assertions | ✅ compile+link | ✅ 60 cases / 235 assertions | [41369850862](https://github.com/hubo1989/TransFrame/actions/runs/41369850862) |
| C `hubo/win-translation-services` | ✅ 105 cases / 370 assertions | ✅ compile+link（含 NativeShell smoke 链接） | ✅ 105 cases / 370 assertions | [41370116943](https://github.com/hubo1989/TransFrame/actions/runs/41370116943) |

测试通过输出行（原样摘录）：

```
A (MSVC x64):   All tests passed (261 assertions in 68 test cases)
A (GCC):        All tests passed (261 assertions in 68 test cases)
B (MSVC x64):   All tests passed (235 assertions in 60 test cases)
B (GCC):        All tests passed (235 assertions in 60 test cases)
C (MSVC x64):   All tests passed (370 assertions in 105 test cases)
C (GCC):        All tests passed (370 assertions in 105 test cases)
```

C 的 105/370 与本机 clang++ 实跑（brief 记录 105 cases/370 assertions）完全一致。
ARM64 说明：windows-latest（x64 runner）无法执行 ARM64 PE（"not a valid application
for this OS platform"），ARM64 腿定位为编译+链接验证；测试执行在 x64 腿完成
（workflow 内已注明）。这是 runner 能力边界，不是代码问题。

## 逐分支修复轮次

### A `hubo/win-skeleton-core`（vcxproj/msbuild 侧，5 轮）

| 轮 | 失败摘要 | 修复 | 结果 |
|---|---|---|---|
| 1 | ARM64: MSB8020 v143 toolset 不存在（runner 是 VS2026/VS18）；x64: STL4038/C2429（std 落回 C++14）；GCC: `Core/Annotation.h` 找不到 | toolset 改 `$(DefaultPlatformToolset)`；`LanguageStandard` 移入 Configuration PropertyGroup；GCC 加 `-I .` | x64 仍 C7589（std 仍没传到 cl） |
| 2 | C7589/C2039 std::optional：`<LanguageStandard>` 作为属性对 cl 无效 | 移入每个项目的 `<ClCompile>` ItemDefinitionGroup（真正的 metadata 通道） | std 生效，错误转移到 Tests |
| 3 | Tests: C1083 `Core/...` 头找不到；App: MSB4044 CompileXaml 缺 TargetPlatformMinVersion + D8016 `/ZW 与 /std:c++20 冲突` | Tests IncludePath 加 `$(ProjectDir)..`；App 暂时注释 WindowsAppSDK PackageReference（骨架 AppMain 是纯 Win32 窗口，不需要 WinAppSDK；任务 B/C 引入 WinUI 3 时恢复） | 只剩 VisualLineGroupingTests C4244 |
| 4 | C4244 double→float（fixture confidence） | 显式 `static_cast<float>` | LNK2019: main 缺失 + BCryptGenRandom 未解析 |
| 5 | LNK2019 main（CATCH_CONFIG_MAIN 在 v3 amalgamated 下不提供 main，且 vcxproj 又定义了 CATCH_AMALGAMATED_CUSTOM_MAIN）；LNK2001 bcrypt（静态库内 #pragma comment(lib) 不总传播） | 去掉 CATCH_AMALGAMATED_CUSTOM_MAIN + TestMain.cpp 不再定义 CATCH_CONFIG_MAIN（amalgamated .cpp 自带 main）；Link 显式加 bcrypt.lib | 编译链接全过，仅 fixture 打不开 |
| 6 | fixture REQUIRE(in.is_open()) false（exe 目录下没有 fixture） | 测试步骤 working-directory 改为 `windows/`，cwd 相对路径 `Tests/fixtures/...` 直达（先修 fixtures/ 子目录猜测，再修 exe 路径 `.\build\...`） | ✅ 全绿 |

### B `hubo/win-capture-shell`（CMake 源码侧，4 轮 + 初始）

| 轮 | 失败摘要 | 修复 | 结果 |
|---|---|---|---|
| 0 | （初始，workflow 本次新建）x64/ARM64: C1083 `winrt/Windows.Graphics.Capture.Interop.h` 不存在；ID2D1Factory2 未定义；LoadCursorW/LoadIconW LPSTR→LPCWSTR；cancelSession 静态调用；D2D1_ANTIALIAS_ALIASED | 删除两个文件里未使用的 Interop include；`ID2D1Factory2`→`ID2D1Factory1`（Factory2 是 d2d1_3.h 的，Factory1 已带 CreateDevice）；CMakeLists 加 `add_compile_definitions(UNICODE _UNICODE)`（CMake 生成的 vcxproj 默认不定义 UNICODE，MAKEINTRESOURCE 展开 LPSTR）；WM_KILLFOCUS 分支内联 session 清理（不能从静态 wndProc 路径调成员函数）；`D2D1_ANTIALIAS_ALIASED`→`D2D1_ANTIALIAS_MODE_ALIASED` | 只剩 Metadata 命名空间和 Direct3D11CaptureFrame 命名空间 |
| 2 | C3083 `Metadata` 不是类型（WGCCaptureEngine.cpp 用 `Foundation::Metadata::ApiInformation` 但没 include 对应头）；C2039 Direct3D11CaptureFrame 不在 DirectX 命名空间 | WGCCaptureEngine.cpp 加 `winrt/Windows.Foundation.Metadata.h`；Full 里 `wdx::Direct3D11CaptureFrame`→`wgc::Direct3D11CaptureFrame`（该类型在 Capture 命名空间） | 只剩 C4100 |
| 3 | C4100 未引用形参（占位 stub） | 占位参数注释化 `/*obj*/`、`/*request*/` | x64 ✅；ARM64 运行失败（见下） |
| 4 | ARM64: exe 无法启动（x64 runner 不能跑 ARM64 PE） | 测试执行限定 x64（`if: matrix.platform == 'x64'`），ARM64 保持编译+链接验证 | ✅ 全绿 |

### C `hubo/win-translation-services`（workflow 新建 + 源码，5 轮）

| 轮 | 失败摘要 | 修复 | 结果 |
|---|---|---|---|
| 0 | （初始，workflow 本次新建）`cl` 不在 pwsh PATH（未跑 vcvars）；GCC: -Werror parentheses + missing-field-initializers | workflow 改用 CMake（自带 MSVC 环境发现），新增 `windows/CMakeLists.txt`（translation_tests + nativeshell_smoke）；HttpTransport.cpp `h.size() > 6 && ...` 加括号；portable check 去掉 -Werror 对 missing-field-initializers 的硬拒（测试 {status, body} 部分初始化是有意设计，CMake 版本显式 -Wno-missing-field-initializers） | MSVC 编译 NativeShell 时 C3536/C3779 |
| 2 | C3536/C3779 `IIterable::begin/end` 未定义（consume_* 模板在无 `winrt/Windows.Foundation.Collections.h` 时未实例化） | Win32OcrUiaHosts.cpp 加 `#include <winrt/Windows.Foundation.Collections.h>` | 只剩 CreateLanguage / OcrResult 歧义 |
| 3 | C3861 `Language::CreateLanguage` 不存在；C2440 `OcrResult` 解析到 `tf::OcrResult`（Core 模型）而非 WinRT 投影类型（using namespace 双方都有 OcrResult，tf 内层胜出） | `Language(to_hstring(tag))` 构造函数（无静态工厂，解析失败抛异常走 catch）；结果类型全限定 `winrt::Windows::Media::Ocr::OcrResult` | 只剩 BoundingRect |
| 4 | C2039 `OcrLine::BoundingRect` 不存在（OcrLine 只有 Text 和 Words；词级才有 BoundingRect） | 遍历 line.Words() 求词矩形并集作为行框 | LNK1120: 14 个未解析符号 |
| 5 | nativeshell_smoke 缺 Core 符号（smoke_translation.cpp 引用 Core 的 makeBuiltinProvider 等） | CMakeLists 把 `${CORE_SRC}` 编入 nativeshell_smoke（与 README 手工命令一致） | ✅ 全绿 |

## 各分支 workflow 说明

- A 沿用既有 `windows-ci.yml`（msbuild sln x64+ARM64 + ubuntu g++-13 portable check）
- B 本次新建：`cmake -S windows/NativeShell`（x64+ARM64 + ubuntu portable，两平台同一 CMake 工程与测试集）
- C 本次新建：新增 `windows/CMakeLists.txt`（translation_tests：Core+Tests 任意平台；nativeshell_smoke：Core+NativeShell+manual smoke，仅 WIN32，链接 winhttp/credui/advapi32/user32/ole32——只验证编译+链接，运行需 GUI 会话属手工冒烟范畴）

三个 workflow 均为 push（hubo/** 路径过滤 windows/** 与 workflow 自身）+ PR + workflow_dispatch。

## 仍未验证项

1. **ARM64 测试执行**：windows-latest 是 x64 runner，不能执行 ARM64 PE。ARM64 腿现为编译+链接验证。如需 ARM64 实跑测试，需 `windows-11-arm` runner（GitHub 已提供该 label，未在本轮引入，避免再开一轮矩阵）。
2. **C 的 NativeShell 运行时行为**（WinHTTP 真实网络、CredMan 读写、UIA 取词、SendInput 插入）：nativeshell_smoke 只做了编译+链接；真实运行需要 GUI 会话与网络，属 `Tests/manual/smoke_translation.cpp` 标注的手工冒烟（S1-S8），CI 不替代。
3. **B 的 WGC/DXGI/GDI 捕获运行时**与 D2D/DComp overlay 渲染：同样只有编译+链接级验证；运行需交互桌面。
4. **A 的 App 真实 WinUI 3 装配**：AppMain.cpp 是纯 Win32 占位窗口；WindowsAppSDK PackageReference 因 `/ZW 与 /std:c++20 冲突`暂时注释（vcxproj 内有注释说明），任务 B/C 引入 WinUI 3 时需恢复并处理 TargetPlatformMinVersion。
5. 本机 macOS 侧从未编译过的 `windows/Core/src/translation/` 空目录（分支 C）未在 CI 暴露问题（file(GLOB) 不含空目录），无代码因此未验证。

## 复现

```bash
gh run watch <run-id> --repo hubo1989/TransFrame   # 任一最终 run
# 或本地复验（macOS）：
# A: cd windows && clang++ -std=c++20 -I . -I Core -I external Core/*.cpp external/catch2/catch_amalgamated.cpp Tests/*.cpp -o t && ./t
# B: cmake -S windows/NativeShell -B build && cmake --build build && ./build/native_shell_tests
# C: g++/clang++ -std=c++20 -I windows/Core/include -I windows/Core/vendor -I windows/Tests -I windows/Tests/vendor windows/Core/src/*.cpp windows/Tests/*.cpp windows/Tests/vendor/catch_amalgamated.cpp -o t && ./t
```
