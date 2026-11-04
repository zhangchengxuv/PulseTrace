# ANSI 104、Arctic 与物理接口 IP 分族观测

2026-11-04，Windows 11 x64，125% DPI，LLVM-MinGW / WebView2。当前是候选版本，不将局域网 ICMP 校验等同于全部网络准确性验收。

## 已实现且已验证

- 按参考图的结构实现 ANSI 104：104 个唯一扫描码、61 键主区、独立功能键/导航/倒 T 方向键/17 键小键盘。使用统一 U 坐标，主区 15U、键区间另留 0.35U，Backspace 2U、Tab 1.5U、Caps 1.75U、Enter 2.25U、左右 Shift 2.25/2.75U、Space 6.25U；Num + / Enter 高 2U，Num 0 宽 2U。宽屏居中限宽，窄屏统一缩放至 90% 后仅键盘区域滚动。
- Arctic · Custom Colormap，六节点为用户指定颜色。sRGB→线性 RGB→OKLab 分段连续插值→sRGB，超出色域通道裁剪；无效数值拒绝，零最大值不除零。默认 `sqrt(count/max)`，不修改计数。图例的中点 t=0.5 标为 max/4 次，和实际映射一致。无数据为中性虚线键，有覆盖但零次为色图起点，键帽文字自动择黑/白。
- 点击键帽显示所选范围的次数、占比和扫描码，悬停显示键名/次数；最高频、总量、范围查询和原排名保留。真实 WebView2 自检检查 104 个 DOM 映射、Enter 点击详情和色标起点。图形布局与键帽颜色只读取原 KeyboardMonitor 聚合，没有伪造使用数据。
- `ETW Microsoft-Windows-NDIS-PacketCapture` 独立实时会话和消费线程，当前系统普通权限实际成功。事件 1001/version 0 字段从本机 manifest 核对：MiniportIfIndex、LowerIfIndex、FragmentSize、Fragment、GFT 字段。以 MiniportIfIndex 绑定唯一所选物理 WLAN/Ethernet，TUN/VPN/回环不参与，不和物理接口相加。
- 当前 WLAN 的介质关键字为 Native802.11。解析非加密、非 WDS 的 802.11 Data/QoS + LLC/SNAP；有线实现 Ethernet/VLAN/QinQ。ETW Send/Receive 与本机临时 MAC 方向交叉校验；MAC 不保存。只接受完整 PacketStart+PacketEnd，核对 IPv4 Total Length / IPv6 Payload Length+40。
- SQLite schema v2→v3 增量迁移，保留全部旧数据。新 `protocol_samples` 表存 session、UTC 分钟、接口 LUID、四项 IP 字节、来源、口径、质量、有效/拒收/不支持/丢失计数。旧接口 octet 永远保持分族未知。预编译 UPSERT 与原批量事务共用，不在 ETW 回调写数据库。
- CoreService 只增加必要的网络接入、起止时间门控、终止前收尾和统计合并。暂停与自动错误暂停阻止之后时间的报文累计；停会话后先让 ProcessTrace 排空再关闭消费句柄。Raw Input、KeyboardMonitor、MouseMonitor、WebView2 生命周期算法未重写。
- 网络页/总览使用真实 IP 分族观测、四项速率、比例、四条蓝色趋势、来源和质量历史。实线上传、虚线下载；无有效样本显示未知，不补假零。`reference_validated` 仍为 false：尚未通过独立工具及完整负载对照，页面明确“待对照验证”或“不完整观测”。

## 技术路线重新验证

| 路线 | 协议族/方向/物理接口/字节口径 | 权限、驱动与本机结果 |
| --- | --- | --- |
| IP Helper | 可选物理接口总 octet，不能按族拆分 | 普通权限，保留作总量对照，不估算分族 |
| Kernel-Network / 旧 System Logger | TCP/IP 事件不等同所有物理 IP 报文，也不能直接当计费流量 | 独立 manifest 会话 Start=0、Enable=5；旧内核会话 Start=5。未升权 |
| Microsoft-Windows-TCPIP | 部分逻辑端点/协议事件；未验证所有报文、物理绑定与字节语义 | Start=0、Enable=0；没有将其 size 当物理分族计数 |
| NDIS-PacketCapture | 实际介质帧、MiniportIfIndex、方向、IP 长度字段 | Start/Open/Enable/Stop 均为 0；WLAN 收到真实 IPv4/IPv6。采用此路线。只停止自己创建的命名会话 |
| WFP | 不同层次可能看到逻辑流和包；普通安全事件不是完整字节账本 | 完整包级 callout 需要额外内核组件，未安装或修改过滤策略 |
| Npcap | 可按适配器被动抓包、过滤并统计丢失；同样有卸载限制 | 未安装 npcap/npf 服务和 wpcap.dll。仅下载官方 SDK 1.17 供评估，未使用或分发其 DLL/驱动；没有自动安装驱动 |

注意：不能把一次旧内核会话 Access Denied 推广成“所有 ETW 都不可用”。本轮重新验证找到了普通权限可用的 NDIS 路线。

ETW 接收缓冲含提供程序瞬时 Fragment；应用只检查最多 128 字节的 L2/IP 头部，不访问用户载荷、不写 ETL/PCAP、不保留地址或逐包记录。不能宣称已经配置了“内核仅捕获 128 字节”。回调只做过滤、头解析和有界分钟计数。缓冲配置为 2–16×64 KiB，ETW 会产生额外系统开销，不承诺零影响。

## 实际测试结果

| 测试 | 结果 |
| --- | --- |
| Release / Debug | 已实际构建并运行全部 6 组 CTest；最终构建记录见 development-status.md |
| 前端 | 18,849 项检查：104 唯一映射/边界/两两无重叠、相对尺寸、方向键、Arctic 节点/4096 步连续性/色域/文字对比；普通文字配对最低 5.21:1 |
| 原鼠标 | 56 项，300 万条输入重放；算法未改 |
| 产品回归 | 65 项，增加 v2 迁移、旧总量保持未知、分族去重/汇总/质量/保留/清理检查；原 Windows 参数转义断言从 rejects 辅助函数移到 main，仅执行一次且不再可能被异常断言吞掉，因此计数不再含大量重复检查，没有删除断言 |
| 报文与暂停 | 152 项：Ethernet、VLAN、IPv4/IPv6、扩展头长度、方向、802.11、加密/WDS/非法/截断/卸载异常及捕获暂停后数据不变 |
| Core→SQLite | 真实网络分钟样本持久化；暂停后样本不变，内存与重开数据库结果一致（本次输出 real sample buckets=1）。空样本不视为准确性证明 |
| 独立被动 ETW 探针 | 3 秒被动探针收到真实 IPv4/IPv6 报文；EventsLost/RealTimeBuffersLost=0。仅代表该次观察，不作总网卡字节相等声明 |
| 受控 IPv4/IPv6 LAN Echo | 私有 IPv4 网关与 IPv6 链路本地网关，各 2 个 16 字节载荷 Echo。IPv4 上/下各 2 包、88 字节；IPv6 上/下各 2 包、128 字节；与独立请求长度/系统 Echo 成功回复相符，长度偏差 0，事件/缓冲丢失 0。无公网测试流量。Release 路径与 Debug 收尾版本各执行通过 |
| 阶段 60 秒被动资源 | 收尾探针约 60.87s，CPU 0.4375s（约 0.72% 单核等效），Standalone Private Bytes 初始 1.26 / 峰值 3.11 / 最终 2.79 MiB。ETW 丢失 0，但有 288 次长度不一致拒收，质量为 incomplete_observation。不是完整 PulseTrace/WebView2 或系统 ETW 全部开销，也不是高吞吐/长时保证 |
| UI 布局 | 五档 800×600～1920×1080 客户区 DIP×深浅主题、六页真实渲染通过；输出 build/ui-validation-1793779954420。125% DPI 下 800×600 DIP=1000×750 px，键盘内部滚动不算页面溢出 |

已查看实际 Arctic 键盘截图和网络页截图。第一次网络截图为初始化尚无样本，正确显示未知；收尾自检等待真实采集快照后再截图，不植入模拟统计。按参考图进一步将主区、导航区、小键盘之间的独立逻辑间距设为 0.35U，并添加键区间距断言；最终前端检查为 18,849 项。

最终矩阵输出 `build/ui-validation-1793781716595/` 全部通过；清晰键盘和真实网络页截图位于 `build/final-arctic-ui-1793781726736/`。网络截图实际显示 WLAN 的 IPv4/IPv6 字节和速率，同时标出“不完整观测”，没有隐藏拒收。WebView2 资源报告 `resource-test-1793781677387.json` 主程序退出 0、已观察子进程残留 0；测试完成后 `logman query -ets` 未发现 PulseTrace 命名会话。

收尾修正包括：无效/虚拟接口不会沿用旧物理接口的 available；未知 SNAP/PPPoE/MPLS 等封装标为未支持；只有拒收、没有有效 IP 的分钟不绘制零曲线；网络辅助 API 读取失败时显式报错并停用过期速率（真实 DOM 错误状态自检通过）。原接口总速率和 NLM 提示保留。导出生成原聚合 CSV 和独立的 `pulsetrace-ip-observation-*.csv`，后者带来源、口径、质量、拒收、丢失和 `reference_validated=false`，回归验证质量信息没有丢失。

一次收尾构建因并行自检正在运行、EXE 被占用而链接失败；等待该自检正常完成后重建成功，未提升权限、未强杀用户进程。其余最终构建以实际 6/6 CTest 输出为准。

测试产物位于 build/ndis-probe.json、physical-probe.json、physical-probe-reasons.json、physical-passive-60s.json、physical-passive-final-60s.json、local-echo-validation.json、local-echo-validation-final.json，以及各 data/tests/ 与 UI 截图目录；不进入发布 ZIP。发布包的 tools/diagnostics/ 附带四个独立诊断 EXE；它们不会随主程序自动启动。

## 已实现但未完整验收 / 明确未支持

- 一般负载中实际出现长度与帧长不一致，可能与卸载/聚合有关。严格拒收并标记不完整；显示的是已通过校验的观测子集，不能称完整物理出口精确总量。ETW 的零丢失不代表解析覆盖率 100%。
- 捕获分段、不完整 PacketStart/End、加密 802.11、WDS、A-MSDU、未知链路/事件版本、IPv6 jumbogram 等不猜测长度。IPv6 零 Payload Length 仅接受明确 No Next Header 的合法空包；其余列为未支持。
- Ethernet 有线链路当时未连接；只有解析单元测试，不能声称有线实机校验通过。
- 当前 Meta TUN 保持用户原状态，真实 WLAN 观察与 LAN Echo 在此环境执行；未改 Clash/路由/代理。未完成 TUN 开关、特定代理与 DIRECT、IPv4 目标经 IPv6 代理等逐项 A/B 对照。
- 本机未找到 Wireshark/dumpcap 和 Npcap，独立可信抓包对照未执行；不会擅自安装驱动来完成该项。
- 高吞吐与时延变化需受控 LAN 对端，当前没有提供；24/72 小时、跨网切换、其他 Windows 版本、其他 DPI/跨屏、104 个真实硬件键逐个验收仍待进行。
- 系统时间大幅调整、ETW 时钟边界、网卡卸载/虚拟化、硬件层重复传输等仍需更多真值对照；不等同校园网账单。

## 命令

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Debug
.\build\llvm-release\network_capability_probe.exe
.\build\llvm-release\network_etw_probe.exe
.\build\llvm-release\physical_network_probe.exe --seconds 60
# 仅私有/链路本地网关，最多每族 2 次小包；不向公网发送测试流量
.\build\llvm-release\local_echo_validation.exe --local-echo
.\tools\test-ui-layout.ps1
.\tools\measure-resources.ps1 -Mode UI -Cycles 2
.\tools\package.ps1
```

## 修改文件

新增前端：keyboard-layout.js、keyboard-view.js、keyboard.css、protocol-view.js；修改 visualization.js、product.js、index.html、主题变量和前端测试。新增 packet_accounting、ndis_capture 的头/源文件；增量连接 network_monitor、core_service、database 的头/源文件和 CMake。新增 packet_tests、core_network_smoke、network_etw_probe、physical_network_probe、local_echo_validation，扩展 network_capability_probe 与 product_tests。README、desktop-guide、development-status、本记录与 package.ps1 更新。沿用上轮图标、RawInputHost 和 WebView2 生命周期实现。

## 技术依据

- [OKLab 原作者公式](https://bottosson.github.io/posts/oklab/)
- [EnableTraceEx2 权限与关键字语义](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-enabletraceex2)
- [ProcessTrace 排空和停止语义](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-processtrace)
- [Microsoft NDIS PacketCapture 提供程序说明](https://github.com/MicrosoftDocs/message-analyzer-docs-pr/blob/main/message-analyzer-guide/microsoft-windows-ndis-packetcapture-provider.md)
- [Npcap 卸载/权限限制](https://npcap.com/guide/npcap-users-guide.html)

Final deployment verification: 32 manifest files SHA-256 matched; ZIP contained no runtime monitoring data. Extracted keyboard page and installed network page (path with spaces) returned 0 at 800x600 DIP. All seven embedded icon sizes loaded. Final layout matrix: ui-validation-1793786221616, 10/10 sizes/themes, all six page renderers. Tested extracted copy: build\arctic-deployment-1793786398036; installed copy: build\arctic install 1793786398036\PulseTrace.
