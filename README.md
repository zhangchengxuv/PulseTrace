# PulseTrace

面向 Windows 的本地键盘、鼠标与网络活动统计工具。

**简体中文** · [English](README_EN.md)

## 功能特性

- **键盘**：按键次数、频率和 ANSI 104 热力图，仅保存聚合计数。
- **鼠标**：五键、时间双击、垂直/水平滚轮事件与累计滚动量。
- **应用使用**：前台停留与有效活跃时长、排行、小时/日期趋势、排除列表和 CSV；[统计说明](docs/app-usage.md)。
- **网络**：接口状态、切换历史、上传/下载速率及物理接口 IPv4/IPv6 流量观测。
- **流量排行**：可选 Mihomo 应用/连接域名排行，支持协议族、时间范围、搜索和排序；域名历史单独授权。
- **任务栏网速**：Windows 11 原生组件、透明背景、上传/下载双行显示，支持悬浮窗或托盘回退。
- **历史分析**：分钟/小时/天及自定义范围、SQLite 持久化、月度网络归档、导出与备份。

支持中英双语、深浅色主题与后台托盘运行，首次启动默认暂停。不记录输入文本或鼠标轨迹。IPv4/IPv6 观测及排行受权限、采样与数据质量限制，不代表系统全量流量或校园网计费；[统计边界](docs/traffic-rankings.md)。

## 软件截图

真实 Release 构建，采用独立空数据目录与暂停状态，未填充模拟统计或展示个人记录。页面统一为 1600×900 DIP；任务栏为原尺寸局部。

| 总览 | 网络与排行 |
| --- | --- |
| ![总览](docs/screenshots/dashboard.png) | ![网络与排行](docs/screenshots/network.png) |
| **ANSI 104 键盘热力图** | **任务栏组件（暂停）** |
| ![键盘热力图](docs/screenshots/keyboard.png) | ![任务栏组件](docs/screenshots/taskbar.png) |

## 性能占用

2026-11-05，Windows 11 26300、Ryzen 5 5600（12 逻辑核）、31.92 GiB RAM、WebView2 156.0.4314.58。测试 main `14f4f1f` 的 x64 Release 构建，监测启用、无主动输入/网络负载，排行来源关闭。每种状态运行 45 秒，取第 10–40 秒均值。

| 运行状态 | CPU（整机归一化） | Private Bytes | Working Set |
| --- | ---: | ---: | ---: |
| 后台监测，仅托盘 | 0.031% | 8.30 MiB | 34.58 MiB |
| 主界面打开，含 WebView2 子进程 | 0.095% | 261.07 MiB | 456.77 MiB |
| 后台监测 + 任务栏网速 | 0.049% | 9.38 MiB | 40.47 MiB |

任务栏组件进程单独为 **2.81 MiB Private Bytes / 17.73 MiB Working Set**，此短测未分辨到 CPU 时间增量。仅托盘模式也保留 helper，因此组件进程内存不等于两行总量之差。Private Bytes 是私有提交量；Working Set 是驻留工作集，进程求和会重复计算共享页。

实际安装程序文件 **17.39 MiB**（含卸载器，不含系统 WebView2 Runtime 和 `data`）。45 秒后数据目录：后台约 **0.10 MiB**，首次打开界面约 **10.72 MiB**，主要为 WebView2 缓存。以上为文件大小总和，不是物理写入量或长期增长预测。[采样方法与完整数据](docs/performance.md)。

## 下载安装

[GitHub Releases](https://github.com/zhangchengxuv/PulseTrace/releases) · [Gitee Releases](https://gitee.com/zhangchenxuv/pulse-trace/releases)

Windows 10/11 x64，需要 Microsoft Edge WebView2 Evergreen Runtime。

- **安装版**：运行 `PulseTrace-Setup-*.exe`，选择安装位置；数据位于安装目录的 `data`，卸载默认保留。
- **便携版**：完整解压 ZIP 后运行 `PulseTrace.exe`，保留同目录的 Web 资源、DLL 和 helper。

当前候选版 **v0.3.0-rc3** 已在两源提供相同附件。main 可能领先于发布包；截图和性能对应上述开发构建。[使用说明](docs/desktop-guide.md) · [源码构建](docs/build.md)。

## 技术栈

C++20 · Win32 / Raw Input · WebView2 · SQLite · CMake

## AI 开发声明与开源协议

PulseTrace 是一个使用 OpenAI Codex 辅助开发的开源项目。主要代码、界面和文档由 OpenAI 模型辅助生成，并经过实际构建与测试。本项目并非 OpenAI 官方产品。

- **AI 工具**：OpenAI Codex。
- **可确认模型**：GPT-6；历史会话没有完整模型清单，未核实的模型不列出。
- **协议**：PulseTrace 自有源码及文档采用 [MIT License](LICENSE)；第三方组件保留各自许可，见 [第三方声明](THIRD-PARTY-NOTICES.md)。TrafficMonitor 仅作任务栏机制参考，未移植其代码。
