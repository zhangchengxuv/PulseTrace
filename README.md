# PulseTrace

面向 Windows 的本地键鼠、应用使用与网络活动统计工具。

**简体中文** · [English](README_EN.md)

## 功能特性

- **键盘**：按键聚合统计、ANSI 104 布局及 Arctic 连续热力图。
- **鼠标**：二维五键热力图与排行，Mouse4 后退、Mouse5 前进；时间双击、滚轮事件与 delta 分开统计。
- **应用使用**：前台停留与有效活跃时长、排行、小时/日期趋势、排除列表及 CSV 导出。
- **网络**：出口上传/下载、物理接口 IPv4/IPv6 观测、自然月每日流量热力图、接口与连接历史。
- **流量排行**：可选 Mihomo 应用/连接域名排行，支持协议族、范围、搜索与排序；域名历史单独授权。
- **任务栏网速**：原生透明双行组件，支持字体、字号、单位、精度、颜色及位置设置，提供悬浮窗/托盘回退。
- **历史与设置**：默认全部数据、分钟/小时/天及自定义范围，SQLite、月度归档、备份与导出；中英双语、Deep Ocean / Pearl Mist、托盘后台及双源更新检查。

首次启动默认暂停，不记录输入文本、窗口标题或鼠标轨迹。活跃时长使用空闲阈值；IPv4/IPv6 与排行受权限、采样及质量限制，不代表全系统流量或校园网计费。未签名更新包只支持校验后手动安装。[统计边界](docs/traffic-rankings.md) · [应用使用说明](docs/app-usage.md)

## 软件截图

main `b3a2cbf` 的真实 x64 Release 开发构建；独立空数据目录、暂停状态，无模拟统计或个人历史。页面为 1600×900 DIP，任务栏为实际局部。

| 总览 | 网络 |
| --- | --- |
| ![总览](docs/screenshots/dashboard.png) | ![网络](docs/screenshots/network.png) |
| **ANSI 104 键盘热力图** | **任务栏组件（暂停）** |
| ![键盘热力图](docs/screenshots/keyboard.png) | ![任务栏组件](docs/screenshots/taskbar.png) |

<details>
<summary>鼠标与应用使用统计</summary>

| 鼠标 | 应用使用 |
| --- | --- |
| ![鼠标](docs/screenshots/mouse.png) | ![应用使用](docs/screenshots/usage.png) |

</details>

## 性能占用

实测 main `d414ecf`：Windows 11 26300、Ryzen 5 5600（12 逻辑核）、31.92 GiB RAM、125% DPI、WebView2 156.0.4314.58。每组 45 秒，取第 10–40 秒均值；启用监测，未主动生成输入或网络负载，排行来源关闭。

| 运行状态 | CPU（整机归一化） | Private Bytes | Working Set |
| --- | ---: | ---: | ---: |
| 后台监测，仅托盘 | 0.039% | 8.54 MiB | 34.69 MiB |
| 主界面打开，含 WebView2 子进程 | 0.111% | 262.10 MiB | 460.99 MiB |
| 后台监测 + 任务栏网速 | 0.022% | 9.20 MiB | 40.37 MiB |

以上均包含原生 helper；任务栏 helper 单独为 **2.81 MiB Private Bytes / 17.55 MiB Working Set**。Private Bytes 是私有提交量；Working Set 是驻留工作集，进程求和会重复计算共享页。短测不代表长期上限或性能提升。

当前安装清单中的程序文件合计 **11.07 MiB**，不含卸载器、系统 Runtime 和 `data`；含卸载器的当前安装体积待测。45 秒后数据文件：后台/任务栏约 **0.12 MiB**，界面约 **10.73 MiB**，包含 WebView2 缓存；排除测量报告，不是磁盘写入量。[完整口径与实测](docs/performance.md)

## 下载安装

[GitHub Releases](https://github.com/zhangchengxuv/PulseTrace/releases) · [Gitee Releases](https://gitee.com/zhangchenxuv/pulse-trace/releases)

Windows 10/11 x64，需要 Microsoft Edge WebView2 Evergreen Runtime。

- **安装版**：运行 `PulseTrace-Setup-*.exe`；数据在安装目录的 `data`，卸载默认保留。
- **便携版**：完整解压 ZIP，保留 Web 资源、DLL 和 helper，运行 `PulseTrace.exe`。

公开候选版 **v0.3.0-rc5** 包含会话恢复与持久化修复；同一源码、安装包及便携包发布到两个仓库。[使用说明](docs/desktop-guide.md) · [源码构建](docs/build.md)

## 技术栈

C++20 · Win32 / Raw Input · WebView2 · SQLite · CMake

## AI 开发声明与开源协议

PulseTrace 是一个使用 OpenAI Codex 辅助开发的开源项目。主要代码、界面和文档由 OpenAI 模型辅助生成，并经过实际构建与测试。本项目并非 OpenAI 官方产品。

- **AI 工具**：OpenAI Codex。
- **可核实模型**：GPT-6；未确认的历史模型不列出。
- **协议**：自有源码及文档采用 [MIT License](LICENSE)；第三方组件保留各自许可，见 [第三方声明](THIRD-PARTY-NOTICES.md)。TrafficMonitor 仅作机制参考，未移植其代码。
