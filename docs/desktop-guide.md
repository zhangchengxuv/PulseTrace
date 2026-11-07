# PulseTrace

本地 Windows 活动统计工具 / Local Windows activity monitor

## 简体中文

运行完整便携包中的 PulseTrace.exe，或使用 EXE 安装包。首次启动暂停统计，在设置中选择“跟随系统 / 简体中文 / English”。关闭窗口默认进入托盘，完全退出会保存数据并停止核心。

网速在 Dashboard 和 Network 页面显示。任务栏网速组件已移除，关闭到托盘和后台监测仍可正常使用。

新安装默认 D:\Program Files (x86)\PulseTrace，data 固定在安装目录内。D 不可用会给出替代位置；权限不足会提前提示，用户可明确提升安装器或改选可写目录。普通运行不需要管理员权限，不修改整个 Program Files 的 ACL。

升级保留原安装位置和设置。旧版独立数据目录通过安装器安全迁移，一致性备份、原目录保留；目标已有数据时不会覆盖或合并。普通卸载保留 data，重新安装会继续读取历史。

日期选择先编辑草稿，确认后查询；分钟精度、本地时间与 UTC 转换。仅保存聚合统计，不记录输入内容或鼠标轨迹，敏感操作前请暂停。

设置按常规、监测、校园网、云备份、数据、更新及关于分类，提供 Deep Ocean、Pearl Mist 和跟随系统主题。使用统计分开显示前台停留与有效活跃，默认空闲阈值 120 秒，可排除应用、调整保留期及导出 CSV。阅读或视频可能被判断为空闲，不记录窗口标题、文档或 URL。网络排行界面已移除，旧历史和高级来源配置保留。键位排名默认收起，展开状态随刷新保留。

v0.7.0 新增默认关闭的坚果云 WebDAV 加密备份。使用第三方应用密码与独立恢复口令；从云端下载校验、明确确认后，于下次启动且数据库未打开时恢复。详见云备份文档 `docs/cloud-backup.md`。

校园网监控默认关闭。配置门户与允许登录域名后，在独立 WebView2 窗口完成登录；自动识别结果需在设置页确认，无法识别时可点击网页字段手动映射。门户真实值、本地估算费用和未知状态分开显示；详见 [校园网说明](campus-portal.md)。

当前版本使用 Schema v7，升级前自动创建一致性备份。Schema v7 新增校园网门户快照；保存失败保留待写入聚合并自动重试，详见 [恢复说明](persistence-recovery.md) 与 [校园网说明](campus-portal.md)。降级时使用兼容备份，不要用旧程序写新结构。

更新支持 GitHub/Gitee。尚未配置可信发布签名，下载核对 SHA-256 后只提供手动安装，不自动执行 EXE。IPv4/IPv6 为真实物理接口观测，仍带未完成独立抓包对照的质量标识，不等于校园网账单。

## English

Run PulseTrace.exe from the complete portable ZIP, or use the EXE installer. Monitoring starts paused. Choose System default, 简体中文, or English in Settings. Closing the window normally hides it to the tray; Exit saves data and stops the core.

Network speed is displayed on Dashboard and Network. The taskbar speed widget has been removed; close-to-tray and background monitoring remain available.

New installations default to D:\Program Files (x86)\PulseTrace. All data stays in its data subfolder. If D is unavailable, an alternative is suggested. Permission checks occur before copying; explicitly elevate the installer if required, or choose another location. The running app does not require administrator rights and the parent Program Files ACL is not changed.

Upgrades keep the existing location and preferences. The installer can copy legacy external data using consistent SQLite backups while preserving the source. An occupied destination is never silently overwritten or merged. Uninstall keeps data; reinstall reads the retained history.

The calendar edits a draft and commits on confirmation, with minute precision and local/UTC conversion. Only aggregate statistics are saved. Input text and mouse trails are not recorded; pause before sensitive activity.

Settings has General, Monitoring, Campus, Cloud backup, Data, Updates and About categories, with Deep Ocean, Pearl Mist and System themes. App Usage separates foreground dwell from input-idle activity (120 seconds by default), with exclusions, retention and CSV export. Reading or video may appear idle. No titles, documents or URLs are recorded. Network ranking UI is removed while historical data and advanced source settings remain. Keyboard rankings collapse by default and preserve their state.

v0.7.0 adds opt-in encrypted Jianguoyun WebDAV backup using a third-party app password and a separate recovery passphrase. Restore verifies first, requires confirmation, and runs before opening SQLite on the next startup. See `docs/cloud-backup.md`.

Campus monitoring is disabled by default. After configuring the portal and allowed sign-in origins, complete sign-in in an isolated WebView2 window. Recognition proposals require confirmation, and fields can be selected manually. Portal values, local estimates and unknown states remain separate; see [campus portal details](campus-portal.md).

The current version uses schema v7 and creates a consistent backup before migration. Schema v7 adds campus-portal snapshots. Failed saves retain pending aggregates and retry; see [recovery notes](persistence-recovery.md) and [campus portal details](campus-portal.md). Use a compatible backup for downgrades rather than writing a newer database with older binaries.

GitHub/Gitee update sources are supported. A trusted publisher signature is not configured, so hash-checked downloads require manual installation and never run automatically. IPv4/IPv6 values remain physical-interface observations with explicit quality limitations, not a billing total.

Microsoft Evergreen WebView2 Runtime is required / 需要系统 WebView2 Runtime:
https://developer.microsoft.com/microsoft-edge/webview2/

验证、迁移与限制 / Validation, migration and limitations: docs/release-notes.md · docs/app-usage.md
