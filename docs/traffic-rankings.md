# 应用与连接域名流量排行

## 使用与边界

Network 页新增应用/网站、来源、IPv4/IPv6、上传/下载/总量/名称排序、搜索、前 5/10/20/全部（每次展开 100 项）、占比条和 Tooltip。复用顶部整分钟 `[from,to)` 时间范围，支持中英文。占比在搜索/数量筛选前计算；实时速率来自当前样本，暂停、失联或过期时不可用。

在排行下的“采集与隐私设置”中显式启用来源。**Windows、Mihomo、域名历史默认都关闭，主监测仍默认暂停。** 域名可单独停记、清除；排行保留期独立设置为 1–365 天，按小时清理，不影响物理接口月档。关闭域名开关保留历史；清除须先暂停整个监测并确认。

| 来源 | 计量内容 | 限制 |
| --- | --- | --- |
| Windows 进程 ETW | Kernel-Network TCP/UDP send/receive 的 payload PID、size，IPv4/IPv6 | 逻辑进程流量，不能绑定物理出口；不计 connect/retransmit 事件。当前账户启用返回 5，未验收原生字节准确性 |
| Mihomo `/connections` | upload/download 累计计数的已观测增量 | 逻辑连接载荷，包含 HTTP 等上层协议字节；不是物理 IP 包或校园网计费。目标协议族可能处于 TUN/fake-IP 阶段 |
| 既有物理接口统计 | 原 IP Helper / NDIS 统计 | 完全保留，不与两个排行榜来源相加 |

按规范化的 EXE 路径聚合多个进程，不以 PID 保存历史。ETW 用进程创建/退出时间验证事件生命周期；无法核实的 PID、已退出或受保护进程归入未知。仅有进程名时标记身份不确定；共享 svchost 不猜测具体服务。本地 EXE 图标可提取时使用图标，否则使用名称标识，不联网取图标。

“网站”实际为 Mihomo `metadata.host` 报告的连接域名，不是经验证的浏览器历史。仅做 IDNA/大小写规范化，不使用反向 DNS、不猜测 CDN 归属。空白、不合法域名、IP 和 URL 均归入未知目标。原始 IP、五元组不入库。

## 采样质量与安全

- 两秒采样。初始快照作为基线，排除启用前的累计量。后续新 ID 只有 RFC3339Nano `start` 证实发生在上次成功采样之后时，才从零计算；其他首次观测仍建立基线。Mihomo 空闲的 `connections:null` 是合法空集合。
- 逐方向计算 `max(0,current-previous)`。重置、ID/身份变化、全局回退、失联、超过 10 秒间隔和连接消失均有处理；无效文档不部分提交。
- **短连接可能完全漏过，连接消失时不能补回最后一次采样后的尾量。** 断线后仍存在的连接可恢复累计差值，但期间时间分布未知。增量计入采样结束分钟，不能承诺任意分钟边界的完整、精确量。
- Mihomo 均标记采样不完整，附加缺口、重置、未知归属和容量标志。ETW 有 StartTrace/EnableTrace/ProcessTrace 和丢失/拒绝/队列溢出诊断。当前没有自动提权的常驻辅助进程，主程序无需无条件管理员权限。
- 限制文档 8 MiB、16,384 条活跃连接、32,800 行内存聚合；超限折叠/丢弃标记质量问题，不称为完整统计。
- 只访问用户填写的回环 HTTP(S) origin 和端口；localhost 固定走回环。不扫描端口，不读取 Clash 配置/凭据，不设置系统代理。只 GET `/connections`，禁重定向、自动认证和 Cookie，HTTPS 不绕过证书验证。有请求超时和响应容量限制。
- 密钥由用户填写，以当前用户 DPAPI 加密保存到 `data/mihomo.secret`，不进入配置、排行、日志或导出。移除密钥是独立操作；不把加密文件当作可跨用户移植的凭据。
- 请求在独立线程，暂停/配置变化丢弃进行中样本。输入回调和 UI 线程不下载、不写库；禁用时线程等待配置事件。原 NetworkMonitor、Raw Input 算法和 WebView2 生命周期保持。

## 存储与升级

数据库仍为统一数据目录的 `monitor.db`。Schema v4 新增 `traffic_rankings`：session、UTC 分钟、source、kind、稳定身份、协议族、名称/路径/归属、上下行与质量位。只存分钟聚合，不存连接 ID、逐连接时间戳、URL、参数、Cookie、内容或载荷。

复用 SQLite Online Backup 和事务迁移，v1/v2/v3 顺序升级到 v4，保留既有计数、网络事件、协议族和月档。绝对快照 UPSERT 支持重复写入/失败重试；历史与当前内存快照不重复累加。存储失败暂停采集并保留待写数据。旧软件不支持 v4 时应拒绝写入；回退使用迁移前备份，不直接降级数据库。原 CSV 格式未改，完整排行可用数据库备份保存。

## 实测（2026-11-05）

以下只用隔离数据库、单元样本或受控回环传输，不发布个人流量记录。

- Release/Debug 均构建成功，各 **15/15 CTest**。排行单测包含 **85 项**：差值、基线、新连接时间、空闲 null、ID/计数重置、断线、连接消失、未知域名、协议族、路径聚合、PID 生命周期、输入/整数溢出校验、真实 EXE 图标、DPAPI、认证失败、拒绝跳转、恢复、暂停、清除、历史、UPSERT 和 v3→v4 在线备份迁移。旧版备份/回滚/未来版本拒绝测试保留。Debug 曾有一次原有“关闭立即隐藏”断言失败，独立和全套复跑通过；原始失败日志保留，不视为已定位修复的生命周期缺陷。
- 普通权限原生探测：StartTrace=0，EnableTraceEx2=5，无事件。没有提升整个应用、安装驱动或宣称 native 字节验收通过。
- **真实 Mihomo Meta v1.19.33 独立实例**，复用已安装二进制；另建专用配置、认证、回环控制器和本地目标。未读取/修改现有 Clash 设置/凭据、TUN、代理或路由。两个 node 进程按同一 EXE 聚合；双向 TCP IPv4/IPv6、报告域名/未知目标和两组 UDP IPv4/IPv6 回显（每组 32×512 B）通过。
- 同轮参考 socket TCP 字节加 UDP 载荷为上传 `295141 B`、下载 `426262 B`；排行观测 `295141 / 385302 B`。下载尾量少 `40960 B`（约 9.61%），已标记不完整。它是该采样时序的实测偏差，不是通用误差界限，也不是计费对照。
- 同轮约 12 秒 provider 探测 CPU 约 0.13% 单核，Private Bytes 初始 1,462,272、结束 2,449,408、采样峰值 2,514,944。它不包含 Mihomo/UI 进程，不作为整个软件资源承诺。
- 十秒纯聚合压力重放：64,735 快照、每快照 32 连接，计数相符；Private Bytes 热身后/结束均 1,212,416，24 聚合行，数据库 36,864 B、WAL 49,472 B。压力循环约 99.4% 单核 CPU，**不是正常两秒采样开销**，不等于长时间泄漏验收。
- 真实 WebView2 Network 页面和两次关闭/重开、后台核心/SQLite 安全退出通过。六页面×中英×深浅色与 ANSI 104、日期选择器回归；Network 增加 app/domain×All/v4/v6 的六组 RPC/DOM 校验。尺寸截图及证据路径见开发状态；截图不提交。

补充 `--multi-app` 实测：两个 Node 进程聚合到一个 EXE 身份，同时 PowerShell 独立为第二个应用，双向 TCP/UDP 和原生 UI 通过。证据保存在 `build/mihomo-ranking-controlled-1793879557540`。

待外部/人工验收：获授权的 ETW 字节参考、浏览器等更多应用/受保护服务、QUIC、现有 Clash TUN 开/关、实际代理出口、独立抓包、高吞吐影响及 24/72 小时运行。不会把这些未验收场景标记为系统全量覆盖。

## 复现命令

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Debug
.\build\llvm-release\application_traffic_probe.exe --etw --seconds 3
.\build\llvm-release\application_traffic_probe.exe --mihomo http://127.0.0.1:9090 --domains --seconds 12
.\build\llvm-release\ranking_benchmark.exe 10
# 显式提供二进制，仅启动独立回环实例；最后一项可省略 UI 验证。
node tools/test-mihomo-rankings.cjs 'D:\Program Files\Clash Verge\verge-mihomo.exe' build/llvm-release/application_traffic_probe.exe build/llvm-release/PulseTrace.exe
# 再增加一个独立的 PowerShell 应用，与两个 Node 进程并行传输。
node tools/test-mihomo-rankings.cjs 'D:\Program Files\Clash Verge\verge-mihomo.exe' build/llvm-release/application_traffic_probe.exe build/llvm-release/PulseTrace.exe --multi-app
```

无参数探测器仅显示帮助，不采集。探测认证可显式使用当前进程 `PULSETRACE_MIHOMO_SECRET`，不要将密钥放进命令行或提交文件。输出含应用路径/域名，应自行保护。专用测试认证和隔离数据留在忽略的 build 中，不进入源码。

依据：[Microsoft TCP/IP ETW PID/size](https://learn.microsoft.com/en-us/windows/win32/etw/tcpip-typegroup1)、[UDP payload PID 与事件头区别](https://learn.microsoft.com/en-us/windows/win32/etw/udpip)、[Mihomo API](https://wiki.metacubex.one/en/api/)、[Tracker 计数/关闭语义](https://github.com/MetaCubeX/mihomo/blob/Meta/tunnel/statistic/tracker.go)。
