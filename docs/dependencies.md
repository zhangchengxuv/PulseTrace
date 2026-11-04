# 依赖及工具链

> 以下为原独立验证程序阶段记录。当前桌面版已经引入 WebView2 SDK 1.0.4314.35、nlohmann/json 3.12.0、SQLite 3.54.1；固定下载与哈希见 tools/bootstrap-deps.ps1，分发许可见根目录 THIRD-PARTY-NOTICES.md 和发布包 licenses/。

验证程序运行时使用 C++20 标准库、Windows User32、Kernel32、Psapi；视觉延迟测试目标额外使用 Gdi32。未引入 SQLite、HTTP 库、Node、Python、Electron 或前端框架，也没有需要安装的附加驱动。

构建工具不随产品作为运行依赖：

| 工具 | 本次版本 | 原因 | 许可 |
| --- | --- | --- | --- |
| LLVM-MinGW | 20260616 / Clang 22.1.8 / UCRT x64 | 当前环境没有可用 MSVC，使用自包含 Windows 编译器、头文件和链接库完成实际编译 | LLVM Apache-2.0 with LLVM-exception；mingw-w64 含 ZPL、公有领域及其他组件许可，参见发行包许可证 |
| CMake | 3.31.8 | 可重复的 C++20 构建和 CTest | BSD 3-Clause，发行包包含完整许可证 |

下载来源为官方 GitHub Release，全部保存在 `build/tools`，不修改全局 PATH。CMake 压缩包核对了同一官方发行的 SHA-256 清单。LLVM-MinGW 压缩包本次计算的 SHA-256 为 `b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35`；这是本地下载指纹，不声称已独立核对发布者签名。

未来 SQLite 选择其官方公有领域发行；优先系统库或明确固定版本的 amalgamation，不在配置时静默下载。引入时补充版本、上游校验、构建选项及许可证交付。
