# 音乐统计 / Music statistics

设置 → 监测 → 音乐：默认关闭，可分别启用 QQ 音乐和网易云音乐。还须开启主监测开关。有效播放阈值为连续播放 30 秒，可设为 1–3600 秒。Music 页使用公共“全部数据 / 自定义范围”，提供播放器筛选、可选跨播放器合并、歌曲/歌手 Top 10、每日/每周/每月检测时长和 CSV 导出。

采集仅通过 C++/WinRT `GlobalSystemMediaTransportControlsSessionManager`，不读取账号、Cookie、歌词、缩略图、音频或私人文件，不控制或注入播放器。读取会话事件，并每 5 秒验证当前状态；事件只唤醒独立线程，最多每 250ms 合并一次。仅对相邻两次 Playing 观测间的单调时间累计，最长 7.5 秒；切歌、暂停、退出、锁屏、休眠、时钟跳变和接口失败均重建时间基线。检测时长不代表实际可听时长；间断边缘可能保守少计。

进度跳转不作为播放时长。只有已观测到接近歌曲结尾后回到开头，才识别同曲循环并重置有效次数。SMTC 无稳定歌曲 ID，也不标注 seek / loop；极短歌曲、两次观测之间完成的循环、跳到结尾再回到开头等情况不能可靠区分，不宣称精确。保留原始标题、歌手、专辑和接口提供的时长；规范化组合键包含曲序和时长，缺少身份字段时限定会话，避免跨播放器误合并。同一歌曲恢复播放不会重复计次；未达阈值的连续计时在暂停/拖动后重新开始。

SQLite schema 8 → 9 仅新增 `music_minutes` 表及索引，保留原数据；升级前沿用 Online Backup 快照。仅保存每分钟歌曲聚合，不保存逐秒轨迹。音乐数据进入现有原子批量事务、带 revision 的恢复日志与加密 WebDAV 归档；默认沿用历史长期保留，可通过数据页清理全部统计。异常退出最多丢失尚未刷新的内存统计，不补算退出期间的时长；重启会话重新达到阈值才能新增次数。

验证程序：`music_smtc_probe.exe`（仅报告字段可用性，不输出歌曲名称）；`music_smtc_probe.exe --monitor` 进行 15 秒只读内存采集及进程 CPU/内存测量。当前环境已发现 QQ 音乐会话并成功读取标题/歌手/状态/时间线；网易云未发现会话。真实切歌、循环、拖动、多客户端、电源切换仍需对应客户端验收；模拟会话测试覆盖这些状态机行为。支持 Windows 10 1809+；不提供 SMTC 的客户端版本显示无会话，不回退扫描进程/窗口。

Music is opt-in and uses read-only Windows SMTC on an independent worker. The main capture switch must also be on. Only detected Playing intervals count; audible listening is not asserted. Pauses, lock, suspend, gaps and failed observations reset timing. Threshold qualification is per continuous run; an already qualified playback is not counted again after pause. Loops require observed end-to-start progress; SMTC cannot distinguish all seeks or loops and exposes no stable song ID. Missing identity metadata prevents cross-session merging. Aggregates share SQLite transactions, recovery receipts, CSV export, confirmed data clearing and encrypted WebDAV backup. Existing records are preserved by additive schema 9 migration. QQ session fields were detected locally; NetEase and complete real-client playback scenarios remain unverified. No music records are included in the repository.

构建依赖：先执行 `tools/bootstrap-music.ps1`，固定 C++/WinRT 2.0.240405.15 和 SDK Contracts 10.0.26100.3916 生成头文件，校验 SHA-256；无需安装 SDK 或更改系统设置。该脚本也由 `bootstrap-deps.ps1` 调用。

参考：[SMTC session manager](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager)、[session API](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession)。
