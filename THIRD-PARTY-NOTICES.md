# PulseTrace 第三方组件

- Microsoft.Web.WebView2 SDK 1.0.4314.35，Microsoft 官方 NuGet 包。发布中只包含 x64 WebView2Loader.dll，遵循 SDK LICENSE/NOTICE 中的再分发条款；系统 Evergreen Runtime 单独提供，不随包附带 Chromium。完整条款见 licenses/WebView2-LICENSE.txt 和 WebView2-NOTICE.txt。
- SQLite 3.54.1 amalgamation，官方 sqlite.org 发行，公有领域。来源：https://www.sqlite.org/copyright.html 。静态链接；禁用动态扩展加载与 DQS，不包含数据库 CLI。
- nlohmann/json 3.12.0，MIT，Copyright Niels Lohmann。完整许可见 licenses/json-LICENSE.MIT。
- LLVM-MinGW 20260616 / Clang 22.1.8、libc++/libunwind 静态运行库，LLVM Apache-2.0 with LLVM exceptions 及 mingw-w64 对应许可。完整 LLVM 许可随包提供；mingw-w64 组件含公有领域、ZPL 等许可，来源 https://github.com/mingw-w64/mingw-w64 。CMake 仅构建工具，不是产品运行依赖。
- Windows UCRT、User32、Kernel32、Shell、Ole、IP Helper 等为系统依赖，不随本应用复制系统 DLL。
- Matplotlib v3.10.0 的 Viridis、Inferno、Magma、Cividis 完整色图查找表；官方来源与 SHA-256 见 docs/ui-design.md，完整许可见 web/desktop/assets/Matplotlib-LICENSE.txt。只有静态色图数据随包提供，不需要安装 Python 或 Matplotlib。

应用不会自动上传监测数据，不使用外部 CDN 或 Node/Python 后台服务。WebView2 Runtime 本身由 Microsoft 维护，其系统组件行为不等同于本应用的数据采集与持久化策略。
