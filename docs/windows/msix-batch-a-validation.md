# Windows 发布批次 A：本地验证记录

验证环境：Windows 11 build 26200，VS Build Tools 2022、Windows SDK 10.0.26100.0，
分支 `lkl-hubo/wt-release-msix`，功能基点 `45a474f`。
这是本机验证记录；未触发 GitHub Actions、未上传产物、未在 Win10 VM 或 ARM64 真机运行。

## 构建与测试

| 验证 | 结果 |
|---|---|
| Release solution / x64，MSBuild `-warnaserror` | 通过 |
| Release solution / ARM64，MSBuild `-warnaserror` | 交叉编译通过 |
| EXE + bootstrap DLL 的 PE Machine | x64 均为 `0x8664`；ARM64 均为 `0xAA64` |
| `python -m unittest discover -s windows/scripts -v` | 12 tests，全部通过 |
| Core Catch2（默认语言表路径） | 1402 assertions / 215 cases；语言表检查按原逻辑跳过 |
| Core Catch2（设置 `TRANSFRAME_STRINGS_DIR=windows/App/Assets/Strings` 绝对路径） | 6926 assertions / 215 cases，语言表检查通过 |
| NativeShell Catch2 | 542 assertions / 106 cases，全部通过；原基线 526 / 102 |
| `PlatformFeatures.cpp` 的 GCC `-std=c++20 -Wall -Wextra -Werror` 编译 | 通过；不是整个 NativeShell 的 MinGW 构建 |
| `build_smoke_*` | 7 个构建脚本全部通过；7 个程序退出码均为 0 |
| Python `yaml.safe_load` | 5 个 jobs 解析成功；x64/ARM64 矩阵与 SDK 前置顺序检查通过 |
| 最终 CI 的 SDK 配置、固定 URL 下载、自签打包命令 | 双架构在本机实际执行成功 |

七个冒烟：capture、overlay、pinned_history、uploads_picker、locale_onboard、
settings_winui、settings_tabs。capture 确认实际引擎为 WGC；settings_tabs 确认六页内容。
locale_onboard 的 N2 托盘气球在本会话为 N/A（tray unavailable），不是已验证通过。
日志和生成图片保存在忽略目录 `dist-test/validation/`。

## 最终产物

| 文件 | 字节数 |
|---|---:|
| `dist/x64/TransFrame-1.0.4.0-x64.msix` | 2,398,716 |
| `dist/x64/TransFrame-x64.appinstaller` | 932 |
| `dist/x64/Microsoft.VCLibs.Desktop-14.0.33321.0-x64.appx` | 6,764,349 |
| `dist/ARM64/TransFrame-1.0.4.0-arm64.msix` | 2,304,661 |
| `dist/ARM64/TransFrame-arm64.appinstaller` | 942 |
| `dist/ARM64/Microsoft.VCLibs.Desktop-14.0.33321.0-arm64.appx` | 11,174,638 |

MSIX SHA-256：

```text
x64   88ca047130954a89daaaee8a36be213465f395d5d66e80515b2ef3b89de1b406
ARM64 ef3a0f7a686b025a69b1578c89e14f30c2437e25cae88844b186d24f93a8f9b3
```

两个包均包含 EXE、对应架构 bootstrap DLL、三张清单图标、ICO、10 份语言 JSON、
AppxManifest、BlockMap、CodeIntegrity catalog、签名。打包脚本已打印完整文件/大小清单，
日志为 `dist-test/validation/package-x64.log` 和 `package-ARM64.log`。

## 签名验证

对两个最终包分别执行 `signtool verify /pa /v`：

```text
Successfully verified: dist\x64\TransFrame-1.0.4.0-x64.msix
Number of files successfully Verified: 1
Number of warnings: 0
Number of errors: 0

Successfully verified: dist\ARM64\TransFrame-1.0.4.0-arm64.msix
Number of files successfully Verified: 1
Number of warnings: 0
Number of errors: 0
```

测试证书 Subject 为 `CN=TransFrame CI Test`，每次生成新证书，有效期一天，私钥不可导出，
测试签名无时间戳。验证日志记录的到期日为 2026-11-12；过期后应重新运行测试打包。
本机会话为非管理员，已核对本轮指纹并接受 Windows 的测试证书信任框；信任在
CurrentUser/Root。提升权限的 CI runner 使用 LocalMachine/Root，脚本不自行提权。
这些产物仅用于验证打包，下载 feed 使用示例地址，不能视为生产发布或可用更新服务。
生产仍通过 `--certificate-thumbprint` 选择真实证书并请求时间戳。

## CI 步骤及失败行为

1. checkout、MSBuild 环境、Release solution 构建（矩阵 x64 / ARM64）。
2. 将已安装 Windows SDK 的 x64 主机工具加入 PATH，检查 MakeAppx 与 SignTool。
3. 从固定 Microsoft Download Center 资源 URL 下载 VCLibs 14.0.33321.0。
   校验 SHA-256、Name、Publisher、Version、ProcessorArchitecture 和微软签名；任一失败终止。
4. `build_msix.py --self-signed-test-cert`：版本检查、PE 架构、完整语言表和清单图标检查、
   `.appinstaller` 结构/标识一致性检查、MakeAppx、自签、`verify /pa`、包内文件清单检查。
5. `tar -tf` 列出包内容，`upload-artifact` 发布 `msix-x64` / `msix-ARM64` 测试 artifacts，
   含 MSIX / appinstaller / VCLibs，保留 14 天。

Win10 API 审计及修复详见 [降级矩阵](win10-degradation-matrix.md)。
