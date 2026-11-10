# PulseTrace 第三方组件

PulseTrace 自有源码与文档采用根目录 [MIT License](LICENSE)。下列第三方代码、数据、加载器和安装工具保留各自许可，不因本项目 MIT 声明而重新授权。`licenses/` 路径指发行包中的许可副本；源码中的色图许可见 `web/desktop/assets/Matplotlib-LICENSE.txt`。

- Microsoft.Web.WebView2 SDK 1.0.4314.35，Microsoft 官方 NuGet 包。发布中只包含 x64 WebView2Loader.dll，遵循 SDK LICENSE/NOTICE 中的再分发条款；系统 Evergreen Runtime 单独提供，不随包附带 Chromium。完整条款见 licenses/WebView2-LICENSE.txt 和 WebView2-NOTICE.txt。
- SQLite 3.54.1 amalgamation，官方 sqlite.org 发行，公有领域。来源：https://www.sqlite.org/copyright.html 。静态链接；禁用动态扩展加载与 DQS，不包含数据库 CLI。
- nlohmann/json 3.12.0，MIT，Copyright Niels Lohmann。完整许可见 licenses/json-LICENSE.MIT。
- LLVM-MinGW 20260616 / Clang 22.1.8、libc++/libunwind 静态运行库，LLVM Apache-2.0 with LLVM exceptions 及 mingw-w64 对应许可。完整 LLVM 许可随包提供；mingw-w64 组件含公有领域、ZPL 等许可，来源 https://github.com/mingw-w64/mingw-w64 。CMake 仅构建工具，不是产品运行依赖。

  MinGW-w64 静态运行库的完整版权和再分发声明现随源码与发行包保留在 `licenses/MinGW-w64-runtime-LICENSE.txt`、`licenses/MinGW-w64-LICENSE.txt`、`licenses/winpthreads-LICENSE.txt`；原文来自固定版本工具链，未经改写。
- Windows UCRT、User32、Kernel32、Shell、Ole、IP Helper 等为系统依赖，不随本应用复制系统 DLL。
- Matplotlib v3.10.0 的 Viridis、Inferno、Magma、Cividis 完整色图查找表；官方来源与 SHA-256 见 docs/ui-design.md，完整许可见 web/desktop/assets/Matplotlib-LICENSE.txt。只有静态色图数据随包提供，不需要安装 Python 或 Matplotlib。

应用不会自动上传监测数据，不使用外部 CDN 或 Node/Python 后台服务。WebView2 Runtime 本身由 Microsoft 维护，其系统组件行为不等同于本应用的数据采集与持久化策略。

- Inno Setup 7.1.0 用于生成 EXE 安装包，Copyright (C) 1997–2026 Jordan Russell，Portions Copyright (C) 2000–2026 Martijn Laan。保留原始安装器版权与网站信息；完整许可在构建工具 `tools/license.txt`，发行副本见 `licenses/InnoSetup-LICENSE.txt`。来源 https://jrsoftware.org/ 。编译器仅在构建时使用，不随程序启动。
- TrafficMonitor 六个任务栏相关源文件仅用于理解 Windows 任务栏集成机制；本项目独立实现，没有复制其代码，不含 MFC 或其二进制组件。参考 https://github.com/zhongyang219/TrafficMonitor ，原项目许可 Anti-996。

2026-11-05 核对当前任务栏实现、Git 记录及上游 TaskBarDlg / Win11TaskbarDlg / TaskbarHelper：本地使用独立 native helper、受限共享内存、UI Automation 空间检测和自有 BGRA 绘制，与上游 MFC 实现不同，未发现代码移植。此处记录技术参考，不将上游 Anti-996 代码作为 MIT 组件再分发；后续若引入第三方代码，须重新核对原许可并保留声明。

License scope: the root MIT license applies to PulseTrace-owned material only. Third-party components keep their original licenses. TrafficMonitor was consulted for taskbar integration concepts; no TrafficMonitor code, MFC dependency or binary is redistributed. Its Anti-996 license is not replaced by this project's MIT license.

Mihomo 仅作为用户可选的外部控制 API 来源；不随 PulseTrace 分发其二进制或配置，也未移植其实现代码。API 兼容不代表将该外部项目改为 MIT。

- Microsoft C++/WinRT 2.0.240405.15: MIT, Copyright Microsoft Corporation; generated Windows SDK projections, header-only runtime. License: licenses/CppWinRT-LICENSE.txt in distributed packages. SDK Contracts 10.0.26100.3916 supply build-time metadata only (not redistributed). https://github.com/microsoft/cppwinrt
