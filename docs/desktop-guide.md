# PulseTrace

本地 Windows 活动统计工具 / Local Windows activity monitor

## 简体中文

运行完整便携包中的 PulseTrace.exe，或使用 EXE 安装包。首次启动暂停统计，在设置中选择“跟随系统 / 简体中文 / English”。关闭窗口默认进入托盘，完全退出会保存数据并停止核心。

任务栏网速使用真正的子窗口。设置可选嵌入、独立悬浮或托盘；暂停、不可用与实际速率明确区分。系统无法验证显示时安全回退。默认每秒更新，不另起采集器。

新安装默认 D:\Program Files\PulseTrace，data 固定在安装目录内。D 不可用会给出替代位置；权限不足会提前提示，用户可明确提升安装器或改选可写目录。普通运行不需要管理员权限，不修改整个 Program Files 的 ACL。

升级保留原安装位置和设置。旧版独立数据目录通过安装器安全迁移，一致性备份、原目录保留；目标已有数据时不会覆盖或合并。普通卸载保留 data，重新安装会继续读取历史。

日期选择先编辑草稿，确认后查询；分钟精度、本地时间与 UTC 转换。仅保存聚合统计，不记录输入内容或鼠标轨迹，敏感操作前请暂停。

设置提供 Deep Ocean、Pearl Mist 和跟随系统主题。使用统计分开显示前台停留与有效活跃，默认空闲阈值 120 秒，可排除应用、调整保留期及导出 CSV。阅读或视频可能被判断为空闲，不记录窗口标题、文档或 URL。流量排行的可选来源与域名历史需主动启用，不与物理接口计量相加。

数据库升级至 Schema v5 前自动创建一致性备份。降级时使用兼容备份，不要用旧程序写新结构。

更新支持 GitHub/Gitee。尚未配置可信发布签名，下载核对 SHA-256 后只提供手动安装，不自动执行 EXE。IPv4/IPv6 为真实物理接口观测，仍带未完成独立抓包对照的质量标识，不等于校园网账单。

## English

Run PulseTrace.exe from the complete portable ZIP, or use the EXE installer. Monitoring starts paused. Choose System default, 简体中文, or English in Settings. Closing the window normally hides it to the tray; Exit saves data and stops the core.

Network speed is displayed in a real taskbar child window. Embedded, floating and tray modes are available. Paused and unavailable states are distinct from real rates. Unverifiable display falls back safely. Rates use the existing monitor snapshot, updated once per second.

New installations default to D:\Program Files\PulseTrace. All data stays in its data subfolder. If D is unavailable, an alternative is suggested. Permission checks occur before copying; explicitly elevate the installer if required, or choose another location. The running app does not require administrator rights and the parent Program Files ACL is not changed.

Upgrades keep the existing location and preferences. The installer can copy legacy external data using consistent SQLite backups while preserving the source. An occupied destination is never silently overwritten or merged. Uninstall keeps data; reinstall reads the retained history.

The calendar edits a draft and commits on confirmation, with minute precision and local/UTC conversion. Only aggregate statistics are saved. Input text and mouse trails are not recorded; pause before sensitive activity.

Settings offers Deep Ocean, Pearl Mist and System themes. App Usage separates foreground dwell from input-idle activity (120 seconds by default), with exclusions, retention and CSV export. Reading or video may appear idle. No titles, documents or URLs are recorded. Optional ranking sources and domain history require opt-in; do not add their totals to physical-interface traffic.

Schema v5 upgrades create a consistent backup first. Use a compatible backup for downgrades, rather than writing a newer database with older binaries.

GitHub/Gitee update sources are supported. A trusted publisher signature is not configured, so hash-checked downloads require manual installation and never run automatically. IPv4/IPv6 values remain physical-interface observations with explicit quality limitations, not a billing total.

Microsoft Evergreen WebView2 Runtime is required / 需要系统 WebView2 Runtime:
https://developer.microsoft.com/microsoft-edge/webview2/

验证、迁移与限制 / Validation, migration and limitations: docs/release-notes.md · docs/app-usage.md
