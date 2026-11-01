# Windows Translation/OCR Services (Task C)

翻译/OCR 服务层的 C++20 实现目录。Core 纯逻辑（平台无关，Catch2 驱动），
NativeShell 为 Win32/WinRT/UIA/CredMan 实现（`#ifdef _WIN32`，仅 Windows 构建）。

## 目录

```
windows/
├── Core/                    # 纯逻辑：跨平台，无 UI/Win32 依赖
│   ├── include/tf/          # 公共头（所有服务接口）
│   ├── src/                 # 实现
│   └── vendor/nlohmann/     # JSON（与任务 B 共享）
├── NativeShell/             # Win32 实现（仅 _WIN32 编译）
│   ├── include/tf/          # WinHttpTransport / CredManKeyStore / Win32 hosts
│   └── src/
└── Tests/                   # Catch2 v3 + 手工冒烟
    ├── *.cpp                # 单测（纯逻辑，mock transport/provider）
    ├── TestSupport.hpp      # 共享 mock
    ├── vendor/              # catch_amalgamated
    └── manual/              # smoke_translation.cpp（Windows 上手工跑）
```

## 构建（Core 单测 — 任意平台）

macOS/Linux/Windows 均可（Apple clang 21 已验证；MSVC `/std:c++20`、GCC 13+ 等价）：

```bash
clang++ -std=c++20 \
  -I windows/Core/include -I windows/Core/vendor \
  -I windows/Tests -I windows/Tests/vendor \
  windows/Core/src/*.cpp windows/Tests/*.cpp \
  windows/Tests/vendor/catch_amalgamated.cpp \
  -o tests_runner && ./tests_runner
```

**不要**把 `windows/NativeShell/src/*.cpp` 编入 — 它们是 `_WIN32`-only。

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

当前：**105 test cases / 370 assertions 全绿**（macOS arm64, Apple clang 21.0.0）。

## 引擎文档

每引擎请求/响应/错误映射：`docs/windows/engines/<engine>.md` ×9
（mtran / openai / claude / gemini / ollama / google / deepl / baidu / custom）。
