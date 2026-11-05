# PulseTrace 开发状态

## 界面打开时的更新提示

- 首次打开和托盘重开/恢复可见时通过现有异步 UpdateService 检查；复用已有进行中的检查，下载期间不发起新检查，无新采集任务或线程。仅发现新版弹出详情，网络失败不打断界面；隐藏/销毁取消前端轮询并忽略旧响应。
- 同一轮检查只显示一次，立即更新使用原有下载、SHA-256/PE/签名验证；未配置可信签名的安装包仍只允许用户手动安装，不增加静默执行路径。
- Release 19/19 CTest（新增更新提示生命周期测试）通过，9 项并发/隐藏/重开/无更新/离线测试通过；真实 800×600 DIP WebView2 受控元数据弹窗显示、文本转义、尺寸及取消/释放通过，未触发下载或公网更新安装。实际新版本发布后的提示/安装链路仍需随下一次发布验收。

## Network 月度流量热力图

- 新增只在 Network 显示的自然月日历热力图，每列一周、每行星期，支持全部接口字节/IPv4/IPv6及上传/下载/总量。使用已有月度 SQLite、固定归档时区和 SQL 聚合；不将主库、TUN 与物理出口重复相加。
- 不存在的月档只返回缺失日，不创建空数据库；已观察零值、部分时段、丢失和不兼容统计口径分别处理，Tooltip 给出准确 B 值。仅显示已持久化数据，随刷盘更新，缓存避免每次前端刷新重查。
- Release 构建及 18/18 CTest 通过；新增闰年/日历位置/跨月/重试去重/缺失及零值/部分覆盖测试。真实 WebView2 Network、双语双主题七页布局及两轮托盘关闭重开通过，截图留在 build/traffic-calendar-ui-*。物理 IP 采集独立对照限制保持原状。

## 全部数据默认范围

- 六个统计页面首次进入均选择全部数据，页面间保留各自已选范围；月度累计默认全部月份。from=0 作为受验证的全部历史查询标记，自定义范围仍限制 366 天，查询结果容量保护不变。
- 根据保存历史跨度自动选择分钟/小时/日粒度，应用使用时长在 SQL 端聚合并正确叠加未刷盘快照；未修改 Schema、原始计数、保留策略或采集逻辑。
- Release 18/18 CTest 通过，新增旧历史范围、协议验证、应用使用聚合和图表粒度边界测试。实际 --self-test-picker 完成 61 秒静置、实时刷新、月份/时分修改、暂停恢复、取消/确认/Esc 和两轮关闭重开。首次验证暴露新增自检局部变量遮蔽，已修复并完整重测通过。最终 Debug 回归随本轮后续记录。

## 鼠标可视化

- 新增二维 SVG 鼠标与真实点击占比排行，复用 Arctic 连续映射；Mouse4=后退、Mouse5=前进，未修改 Raw Input。零次数与无观测分别渲染。两主题和中英即时切换均使用同一数据源。
- 新增聚合/占比/缺失状态测试；Release 构建及 18/18 CTest 通过，实际 WebView2 鼠标页、七页双语双主题无溢出、托盘关闭重开自检通过。截图仅留在忽略的 build/mouse-visual-verified-*，未提交。
- 后续：全部数据默认范围、Network 月度日历热力图、界面打开更新提示和极简启动。

## v0.3.0-rc3 双源发布

- 发布源提交 `4469489a6b13a7608731ce017bfe0d301c82b3d9`，两端 `v0.3.0-rc3` 标签均指向该提交；[GitHub](https://github.com/zhangchengxuv/PulseTrace/releases/tag/v0.3.0-rc3) 与 [Gitee](https://gitee.com/zhangchenxuv/pulse-trace/releases/tag/v0.3.0-rc3) 均为候选版，安装 EXE、便携 ZIP 及两个 SHA-256 附件已上传。GitHub 服务器 digest 匹配；Gitee 四个附件已独立下载核对 SHA-256。
- Release/Debug 各 18/18 CTest，中英真实向导启动/返回/取消、安装/运行中升级/卸载保留/重装、旧版独立目录迁移（含非空目标保护）、55 文件便携清单、解压运行和七种 EXE 图标尺寸通过。绑定源树及附件哈希的记录在 `build/release/v0.3.0-rc3-public/validation-v0.3.0-rc3.json`。
- 发布测试发现旧版夹具只降低主库版本、却残留新表及 v5 月档；已修正所有受控数据库的真实 v1 结构，并将验证改为只读，保持源库哈希不变。故障记录保留；一个暂停且数据库已无占用的隔离旧测试进程在一致性备份后清理，用户实际运行实例未操作。最终完整发布验证通过，未放宽生产迁移保护。
- GitHub 一度返回服务端 commit_refs_rules 拒绝，有限重试后正常推送；Gitee multipart 表单内的令牌不被上传端点识别，改用已实测的 OAuth query 鉴权完成续传。发布工具同步修复鉴权并对异常日志脱敏。凭据仅在进程内使用，未保存到源码、配置或文件。
- 安装包 SHA-256 `b0642ce7fa1c2db999900c6da73e46e6fbf1e48d1686d6e0d770323007d235e5`；ZIP `758c6d25a25369c16f99e753b4a01a58801ae65589ef965f2f26503e4efa0442`。安装身份签名、其他系统/DPI、多屏、真实锁屏/睡眠/长期运行和网络独立对照仍待验收，详见 [发行说明](releases/v0.3.0-rc3.md)。

## 托盘注册恢复（2026-11-05）

- 回归曾实测“Tray unavailable; keeping window visible”，同一时段观察到 Explorer 重启。代码只有一次 NIM_ADD，失败后不重试；重复注册也会把原成功状态改为失败。现在先 MODIFY、后 ADD，缺失时每五秒重试，关闭到托盘前立即尝试恢复；退出阶段不重新添加图标。没有改变监测、数据路径或窗口关闭的安全回退。
- Release/Debug 实际构建并各通过 **18/18 CTest**（`build/tray-recovery-{build,ctest}-{release,debug}.log`）。原生自检真正删除本实例图标并恢复、重复注册，再验证隐藏/浏览器退出/重开及完全退出；没有重启 Explorer 或删除其他程序图标。
- 当前已完成静态交互精简、Pearl Mist、应用使用统计、已测格式化优化与 DPI 重绘修复、托盘恢复。剩余为外部验收/权限：受控前台激活、真实锁屏/睡眠/注销与多屏/其他 DPI、长时间和真实输入硬件、ETW 全进程权限、独立物理流量对照、可信安装签名及 Gitee Release API。普通 Git 同步正常；本轮不创建 Tag、发布 Release 或覆盖已有安装附件。

## 性能与 DPI 接续（2026-11-05）

- 测量后保留双语数字格式缓存：20,000 次格式化七轮中位数 504.27→15.73 ms，校验和相同。共享轻量尺寸观察器处理 DPR 更新、隐藏/恢复和待执行帧回收；不改变采样、统计或图表数据。第一版统一 RAF 延迟方案未验证收益，已撤回。
- 实际执行前/后/最终五场景资源测量、窗口交替与单项复测。最终窗口 CPU 0.104%、Private Bytes 246.27 MiB（含 WebView2）；前值 0.113%、242.31 MiB。短测波动明显，不宣称整体 CPU/内存降低或物理磁盘优化；滚动 RAF p95 8.4→8.5 ms，不等于 GPU FPS。数据、方法及所有限制见 [performance-continuation.md](performance-continuation.md)。
- Release/Debug 构建、18 项 CTest 回归；Release 曾因 Explorer 同时重启而托盘不可用，独立复测通过，保留失败日志。最终 40 项渲染检查及前端相关 8 项在两种配置再次通过；退出无观测子进程残留。托盘注册恢复已在后续独立修复，见上。

## 应用使用统计（2026-11-05）

- 新增独立 AppUsageMonitor：前台 WinEvent、会话/电源通知和每秒检查；单调时钟结算，输入空闲阈值默认 120 秒，停留/活跃分列。创建时间与存活进程句柄检查、同 EXE 聚合、未知归属、排除应用、暂停/锁屏/睡眠与异常时钟缺口处理。只保留分钟聚合，无标题、文档、URL 或切换日志。
- Schema v5 Online Backup + 事务迁移，旧数据/月档/未知配置保留；批量 UPSERT、独立保留期、CSV、全局清除与退出重试。双语使用统计页、Top 10/分页全部、活跃/停留排序、日/小时明细、今日和最近七天趋势；时间筛选新增本周/本月。设置支持启停、阈值、排除列表及图标。
- Release/Debug 实际构建及各 17/17 CTest 通过（`build/usage-{build,ctest}-{release,debug}.log`）；52 项新增时间模型/存储/真实桥接检查、8 项前端聚合检查、2,763 项双语检查。旧排名迁移夹具曾保留不属于 v3 的新表导致失败，已修正夹具后完整复测通过，没有放宽实际迁移保护。
- 原生七页×两语言×两主题、使用页布局、日期编辑、关闭到托盘/浏览器释放/重开通过；1024×768、1280×800 DIP 截图已检查，真实前台应用和图标确实显示（隔离 `build/usage-live-ui-1793892192022`）。不公开这些记录或截图。
- 外部限制：受控双应用工具被 Windows 拒绝前台激活，失败保留，未宣称完整归属验收。真实锁屏/休眠/注销、多进程/PID 复用压力及长时间资源仍待人工验证。详细行为、复测工具和限制见 [app-usage.md](app-usage.md)。接续性能任务先建立同条件基准，不发布未经验证的安装包。

## Pearl Mist（2026-11-05）

- 现有 `light` 偏好升级为 Pearl Mist 奶白/浅紫灰主题，`dark` 保留 Deep Ocean；无需配置迁移或改数据库。主题选择即时生效并单独持久化，跟随系统逻辑保留；原生标题栏和 WebView2 背景同步。任务栏材质逻辑未修改。
- 图表、日期控件、表格和所有页面复用主题变量，Arctic 算法不变；文字/交互最小对比度 4.69:1（18,870 项前端检查），2,608 项双语检查通过。
- Release/Debug 构建和各 15/15 CTest 通过。两语言×深浅主题六页原生布局及切换不改变监测 session 检查通过；实际 1280×800 DIP/125% DPI 截图已查看，位于 `build/pearl-ui-1793891439155/{light,dark}/logs/selftest-window-dashboard.png`，不提交私人测试目录。更多 DPI、Windows 系统主题实际切换仍待人工验收。

## 恢复开发与待办（2026-11-05）

- 已核对干净工作区与实际源码：透明任务栏、双语、日期选择器、安装目录迁移、流量排行均已实现；不重复开发。独立抓包、更多 DPI/系统、长时间硬件验收及 Gitee Release API 权限仍属外部限制。
- 静态界面文字采用组件级不可选择样式，输入/日期编辑、下拉框、表格、代码和显式可复制区域保留选择；删除顶部英文副标题与侧栏隐私短句的节点及专用留白，详细隐私说明保留。
- Release 增量构建、现有 15 项 CTest（删除副标题后同步移除其失效色彩 token 断言，失败项复测通过）；原生 800×600 DIP WebView2 自检通过两语言/深浅主题六页布局、编辑选择、日期点击与输入、选择控件及关闭/释放/重开。证据 `build/static-ui-1793891260275`，不提交截图或数据。真实鼠标拖选及原生下拉弹出视觉仍需人工检查。
- 此段为恢复时核对结果：当时 AppUsageMonitor 和 Pearl Mist 尚缺，现已接续实现，见上。剩余性能优化需先建立同条件基准，禁止把已有资源测量当优化成果。

## README、许可与实测（2026-11-05）

- 中英 README 同步精简为七部分，增加 Codex/GPT-6 可核实说明、自有材料 MIT 与第三方许可范围；补齐 MinGW-w64 静态运行库的原始声明及便携/安装分发入口。未改 C++ 核心，复用 `14f4f1f` 的 Release/Debug 各 15/15 CTest；本轮实际编译 Inno、隔离安装、校验许可文件并安全卸载，不发布新 Release。
- 三组真实 45 秒 Release 测量：整机 CPU 0.031%/0.095%/0.049%，Private Bytes 8.30/261.07/9.38 MiB，分别为后台托盘/打开界面含子进程/后台任务栏。工作集分列，未混用。安装程序文件实际 17.39 MiB，首次 UI 后隔离 data 10.72 MiB。细节及边界见 [performance.md](performance.md)。
- 三张原生页面统一 1600×900 DIP 与一张真实嵌入组件局部截图，均为当前 Release 构建的空目录暂停状态，已检查无个人内容；用户本轮明确要求发布这些审查过的截图。原始运行数据/日志留在忽略的 `build/readme-review-1793880016626`，只提交资源汇总和四张 PNG。中英相对链接、数值一致性、PNG 元数据与许可分发检查见 `tests/readme_docs_tests.cjs`。

## 最新开发：应用与连接域名流量排行（2026-11-05）

- Network 页加入中英双榜、来源/协议族/排序/搜索/前 5、10、20、全部及当前采样速率；只用真实数据，无演示填充。Mihomo 与域名记录均显式启用；Windows 进程 ETW 为可选来源。原物理接口统计独立，不累加三个来源。
- 后台两秒回环 API 采样、DPAPI 凭据、暂停与在途样本失效、PID 生命周期检查、稳定 EXE 聚合、未知域名、重置/缺口/关闭尾量质量标记；Schema v4 在线备份迁移和批量 UPSERT，保留旧数据及月档。清除同步丢弃对应待重试快照，防止退出时恢复已清除的数据。
- Release/Debug 实际构建、各 15/15 CTest，通过 85 项排行模型/数据库/回环 HTTP 检查、16 项前端筛选检查、2,598 项双语资源检查；全部原键鼠、物理网络、月档、更新、WebView2 回归保留。最终日志 `build/ranking-ctest-{release,debug}-verified.log`。此前 Debug 的原生“关闭立即隐藏”断言有一次失败，独立及全套复跑均通过；保留 `build/ranking-ctest-debug-final.log` 和 `ranking-debug-desktop-recheck.log`，未改写生命周期来隐藏失败，仍需留意真实窗口交互时序。
- 已安装的 Mihomo v1.19.33 独立回环实例验证 TCP/UDP IPv4/IPv6、两个 node 进程同路径聚合、DIRECT、域名与未知目标；没有读写现有 Clash 配置/凭据或更改 TUN/代理/路由。真实 Network 页两次释放重开、安全退出通过。TCP+UDP 参考 295141/426262 B，观测 295141/385302 B，下载未观测尾量 40960 B；保持采样不完整，未宣称完整准确。
- 六页面×两语言×深浅色、排行 app/domain×All/v4/v6 RPC/DOM、五种尺寸 800×600、1024×768、1280×800、1600×900、1920×1080 DIP（当前 125% DPI）及额外浅色 Network 窗口均通过。截图/结果：`build/ranking-ui-final-1793878909665`；真实采样 UI：`build/mihomo-ranking-controlled-1793878599476/ui/logs`；UDP 证据：`build/mihomo-ranking-controlled-1793878741399/summary.json`。截图、凭据、数据不提交。
- 十秒 64,735 快照×32 连接压力重放计数相符，热身后/结束 Private Bytes 均 1,212,416；约 12 秒独立 provider CPU 0.13% 单核、Private Bytes 峰值 2,514,944。范围和口径见[使用与实测](traffic-rankings.md)，不把压力 CPU 或短测当长期后台承诺。
- 补充真实双应用：Node 的两个进程保持一个身份，同时 PowerShell 独立为第二个身份；TCP/UDP 双向传输和原生双榜查询/关闭重开通过。证据 `build/mihomo-ranking-controlled-1793879557540/summary.json`，脚本支持 `--multi-app`。
- 外部限制：当前 ETW StartTrace=0、EnableTraceEx2=5（拒绝访问）；没有自动提权辅助服务，不宣称 Windows 全进程字节采集验收。更多应用/共享服务、QUIC、现有 Clash TUN 开关/实际代理出口、独立抓包、高吞吐和 24/72 小时仍待验收。未扫描控制端口、读取第三方凭据或自动产生公网测试流量。
- 本轮只交付开发构建 `build/llvm-release/PulseTrace.exe` 和源码提交双仓同步，不创建 Tag/Release，不覆盖已发布 RC2 附件。后续任务优先处理已获授权环境中的 ETW 采集参考与短连接尾量能力，避免将 Mihomo 采样下界当物理计费。

当前：**0.3.0-rc2 / Windows x64 候选版**（2026-11-05）。任务栏实际可见性、中英双语、安装目录/data 统一和安装器启动修复已按功能提交。用户已明确授权本版双源 Release；实际发布结果以发布回执和远程附件核验为准。

## 最新：透明任务栏修复

- 移除整面背景填充与窗口级透明度，改为缓存的预乘 BGRA/ULW_ALPHA；默认背景 Alpha=0，文字独立抗锯齿和不透明度。保留真正任务栏子窗口、字形点击和右键菜单；透明 padding 透传。
- 中英设置增加可选 Alpha=32 淡色回退，旧配置默认完全透明；配置切换不重启采集。NetworkMonitor、SQLite 和 WebView2 生命周期没有修改。
- Release/Debug 构建及各 13/13 CTest，148 项图形/IPC/GDI 检查通过；最终排版微调后另行重建两种配置、复测 148 项及真实活动网速窗口。TranslucentTB 运行时背景 18/18 对照一致，文字 16/16 匹配，打开/菜单/托盘/退出通过。
- 本轮只提交同步源码，不覆盖已发布 RC2 附件。真实截图、资源数值与人工模式/DPI/Explorer 验收范围见 [透明任务栏验证](taskbar-alpha-validation.md)。

## 上一项发布结果

- GitHub 已发布 [v0.3.0-rc2](https://github.com/zhangchengxuv/PulseTrace/releases/tag/v0.3.0-rc2)，预发布状态，4 个安装/便携/校验附件均核验成功。
- 发布提交 `fd84970e9c3fc642b75c96352b0bb885447da271`；GitHub/Gitee 的 `v0.3.0-rc2` 标签均指向它。Gitee Git 推送成功，但 Release API 凭据验证返回 401；没有创建 Gitee Release，也没有将凭据写入仓库。
- 发布文件与回执在 `build/release/v0.3.0-rc2-public`。当前透明修复的源码在该发布提交之后，不属于上述附件。

## 双源发布准备

- 发布包含已验证的安装器启动修复；不会使用旧的错误安装包。安装器、ZIP 和校验文件单独汇集在 `build/release/v0.3.0-rc2-public`，排除用户数据和原始测试证据。
- 发布脚本可指定附件目录、独立重试一个来源；缺失某一来源的 API 凭据不阻止另一来源完成。仍要求干净源码、对应标签、绑定提交的测试记录和附件哈希，不覆盖已发布版本。
- 发布计划新增 12 项隔离检查，通过来源选择、自定义附件目录、校验和拒绝、无 Git 引用修改等验证。产品代码和安装脚本与 `3e0f92f` 一致，复用该提交的 Release/Debug、向导和迁移验收；重新校验本次 ZIP 和公开附件哈希。
- 发布身份和外部环境限制继续写入版本说明；候选版不宣称计费级网络准确性或全部 Windows/权限组合验收通过。

## 安装器启动修复（2026-11-05）

- 复现旧包无 `/DIR` 启动的 `50:91` 异常；日志定位 `ShouldSkipPage(PageID=1)` 在欢迎页前无条件展开 `{app}`。此前带 `/DIR` 的静默测试没有覆盖该入口。
- 区分默认目录计算、向导当前目录和准备安装后的目录快照；取消恢复使用保存的解锁目录。保留 D 默认目录、data 子目录、中英向导及一致性迁移，不修改监测核心或用户数据库。
- 修复包重新编译，中英真实向导启动/返回/重选/取消，以及中英安装/升级/卸载保留/重装、旧版外部数据迁移均通过。默认 D 路径普通账户权限拒绝已实测；提升安装仍未验收。
- Release / Debug 构建与各自 13/13 CTest 通过；安装包四个运行时二进制与当前 Release 哈希一致。所有测试仅操作隔离数据，测试安装注册已清理。
- 新包：`build/release/installer-startup-fix/PulseTrace-Setup-v0.3.0-rc2-x64.exe`。详见 [根因、测试与复测命令](installer-startup-fix.md)。后续正式发布准备会先执行无 `/DIR` 的真实向导回归。

## Git 工作流程（长期要求）

- 规则保存在根目录 `AGENTS.md`。每项独立修改验证后，使用英文 Conventional Commit，推送同一 SHA 到现有 GitHub/Gitee 分支。
- `tools/sync-remotes.ps1` 默认只读；显式 `-Push -ExpectedCommit <SHA>` 执行普通推送并核对两个实际远程 SHA。遇到分叉或认证失败保留已有成功结果，不强推、不改写历史。
- 同步脚本已有 27 项隔离测试，涵盖空仓库、远程分叉、部分失败、有限网络重试及远程 SHA 不符。`.gitignore` 排除个人数据、日志、WebView2 配置、凭据和发布二进制。
- 本轮整理此前已验证的代码；新增拆分提交仅做必要的独立构建检查，复用既有长时间、截图与安装回归记录。
- 整理后 Release / Debug 增量构建通过，各 **13/13 CTest**；更新服务、任务栏组件和安装迁移的独立暂存快照也实际构建通过。日志位于 `build/git-final-*` 与 `build/git-review-*`，不提交源码仓库。

## 当前结果

- 已复现根因：普通 GDI 子窗口自身能绘字，但不出现在 Windows 11 任务栏合成画面。使用 layered child、兼容清单和正确的 Segoe UI Variable Text 字体后，真实任务栏可见；最终文字采样 16/16 匹配。状态不再把 SetParent 等同于显示成功。仍是真正 Shell_TrayWnd 子窗口，安全回退悬浮或托盘。
- 实际上传/下载数字已在两张任务栏截图中变化；读取现有 NetworkMonitor 快照，没有模拟或另起采集。暂停显示暂停，不显示虚假零速率。可见性检查只读取自己的小矩形，成功后每 5 秒复核，不保存画面。
- 375 个中英资源，2,208 项覆盖检查；Web 六页、日历、更新、托盘、网速菜单和 Native 主要错误本地化。Settings 语言切换保留同一 session 和采集状态，不重启核心；配置跨版本保留。
- 安装器/卸载器 English + 简体中文，全部自定义步骤本地化。默认 D:\Program Files\PulseTrace，升级保留原路径；用户只选一次安装目录，data 固定跟随。D 不可用时有替代默认路径，复制前检查文件系统和权限。
- 仅对本安装树设置 ACL：程序只读执行、原用户 data 可修改；Inno .dat 卸载记录为必要的单文件写入例外。没有改父 Program Files ACL，没有 Everyone 全控或自动启用自启动。只迁移原有且属于该安装路径的启动项数据参数。
- 旧版外部目录有一致性备份、写入进程安全退出、迁移/验证及原目录保留；目标非空默认保留目标，不合并覆盖。普通卸载保留 data；重装恢复历史。所有测试使用隔离数据。

## 实际验证

- Release / Debug 均完整构建并通过 **13/13 CTest**。包括原有键鼠、网络、协议、月库和 WebView2，以及 37 项路径/迁移/备份/原生语言检查、49 项更新检查、2,208 项双语检查。随后仅修改字体、像素读取开销和安装维护细节，已重新构建两种配置并执行对应可见性/安装回归；不为文档重跑长测。
- Native WebView 自检覆盖六页×两种语言×深浅主题，语言切换前后 session/monitoring/input_ready 保持。中英日历实际截图保留在 build/llvm-release/data/tests/desktop-smoke/logs。
- 五种 DIP 尺寸均通过：800×600、1024×768、1280×800、1600×900、1920×1080；结果与英语 Settings 截图在 build/bilingual-layout-1793864678485。
- 修复前后真实任务栏证据、最终 16/16 核验在 build/widget-invisible-baseline、widget-real-rate-rc2-final、widget-real-rate-final-font、widget-visible-rc2-final。旧版“WS_CHILD 验证通过”现明确作为不充分的历史记录。
- 中英安装/升级/卸载、旧便携导入、程序 EXE 不可写而 data 可写、卸载保留和重装恢复通过。中文记录 build/installer-validation-dd5256cbedad4abface5135ca2148673，最终维护程序英文记录 build/installer-validation-d72bb47c02d1486bb6b46872131957c8。
- RC1 外部目录安全迁移、已有目标保护通过：build/legacy-layout-32de1bb8205c46c79881104bc8a8fd94 / legacy-layout-325aad051eda40c6b2c70512c9f2dbb1；中文安装升级保留原有英语语言、gitee 来源、网速模式、监测启动偏好。
- 全新中英安装/800×600 首启/默认暂停/安装语言建议通过：build/fresh-language-validation-rc2.json。
- 最终 helper 的约 12 秒暂停后台观测：Private Bytes 峰值 2.81 MiB、Working Set 17.64 MiB、CPU 累计增量 0.046875 秒，21 个采样；退出后观察子进程残留 0，无 WebView2 子进程。原始记录 build/llvm-release/data/resource-test-1793865402259.json；不等于长期性能保证。
- 既有日历 61 秒防刷新关闭测试已通过并复用，本轮未重复。新增本地化日历和语言切换检查通过。

## 外部限制与待人工验收

- 当前普通账户对 D:\Program Files 无创建权限，已实测安装前安全拒绝，**默认 D 路径的实际提升安装未验收**。需要用户明确提升安装器或选择可写路径，不会修改整个父目录权限。
- D 不存在的真实机器、另一台英语 Windows、不同凭据提升的实际账户组合、Explorer 重启、自动隐藏/居左/多屏/其他 DPI、逐项右键菜单视觉和长期运行尚待人工验收。Computer Use 管道本轮不可连接，截图使用项目原生验证工具。
- 未配置可信发布签名，更新下载依旧提供手动安装，不自动执行未知 EXE；两源真实新版发布/下载仍待发布条件。IP 分族继续标记未完成独立抓包对照，不声称计费级准确。
- 本轮失败记录保留：旧不可见基线；Inno 缺失辅助函数/字符串类型编译修正；卸载记录权限不足（已收窄例外修复）；英语网络错误断言大小写差异（已按相同语义忽略大小写，保留原断言）；开发截图 probe 的只读映射原子读取异常（已修复，未进入正式包）。

## 交付

- build/release/PulseTrace-v0.3.0-rc2-x64/PulseTrace.exe
- build/release/PulseTrace-Setup-v0.3.0-rc2-x64.exe
- build/release/PulseTrace-v0.3.0-rc2-x64.zip
- 包旁 SHA-256、包内文件清单。未含用户数据库、缓存、开发工具链或独立测试 EXE。
- 详情：[本轮根因与验证](taskbar-language-layout-rc2.md)。当前仍是候选版，不宣称全部外部验收完成。

## 历史记录：0.3.0-rc1
当前：**0.3.0-rc1 / Windows x64 候选版**，2026-11-05。代码、Release/Debug 和新安装器已实际构建。没有自动提交、推送或发布本轮 GitHub/Gitee Release。

## 本轮完成

- 自定义蓝色日期时间 dialog；查询范围与草稿分离，统计刷新不重建日期节点。确认、取消、Esc、外部点击、年月日、时分、键盘导航、窗口边缘定位和分钟级本地/UTC 转换。
- 独立原生 `PulseTraceWidget.exe`：真实 WS_CHILD 任务栏嵌入、物理接口既有速率、暂停/错误/过期显示、独立悬浮与托盘回退、受限 IPC、无额外 Chromium。主窗口隐藏后保留，完全退出时销毁。默认尝试嵌入，空间不可证明时回退。
- 版本唯一源 `cmake/version.cmake`；CMake 生成 RC/C++ 版本头与 JSON，前端和打包读取相同版本。
- SQLite 顺序事务迁移至现有 schema 3、Online Backup、迁移前备份/收据、失败回滚及未来版本拒绝。配置 v0→v1 保留未知字段；新增更新/任务栏设置兼容默认值。设置页提供备份、数据目录和迁移状态。
- GitHub/Gitee 分离的 Release 适配、异步双源检查、SemVer 比较、通道/来源设置、同版大小与 SHA-256 核对、网络诊断、进度、取消、匹配版本的备用下载。客户端不包含令牌。
- 每用户 Inno EXE 安装器、便携 ZIP、主动旧数据导入、数据与程序目录隔离、运行中升级请求安全退出、升级前一致性备份、普通卸载保留数据。不强杀、不自动安装 Runtime 或启用自启动。
- `publish-dual.ps1` 默认只输出计划；显式发布才执行非强制双 remote 推送、提交/标签验证与两源附件核验。旧发布入口改为同样的安全默认。`prepare-release.ps1` 为未来已提交的干净源码生成绑定提交/树/哈希的验证报告。

## 本轮实际测试

- **Release：12/12 CTest**。包括前端 18,872、日历/时间 91、鼠标 56（300 万样本）、迁移 30、更新 49、任务栏几何/IPC 20、月度归档 19、报文与生命周期 152、产品 68 项检查，以及核心网络与 WebView2 生命周期。
- **Debug：完整 12/12 CTest 通过**；后续更新来源排序、导入配置校验和设置页小修后重新构建，并针对受影响的资源加载/迁移/更新/WebView2 4 项回归全部通过。没有为纯文档/许可改动重跑完整套件。
- 既有成功记录复用：`build/picker-60s-fixed` 已验证真实 WebView 日历停留 61 秒，期间后台刷新、月份、小时、分钟、监测切换、取消/确认/Esc；本轮未重复该已通过的长测。
- 既有成功记录复用：`build/widget-native-content-scope/logs/widget-validation.json` 的 `mode=embedded`、`parent=Shell_TrayWnd`、WS_CHILD/NOACTIVATE 检查及关闭/重开/退出通过。当前 Windows 11，实际 DPI=125%。这是真实子窗口，不是贴边悬浮伪装。
- 实际 EXE 安装/导入旧 schema→启动 Settings→运行中覆盖升级→卸载通过：`build/installer-validation-a50b63f2f1a5422286bc90f8db4fd7a1/result.json`。旧便携源未改，配置未知字段/保留期保留，键盘 42、上传 1000/下载 2000 的受控测试记录及月度累计一致；卸载后的数据文件哈希不变。测试数据均为独立副本。
- 便携 ZIP 解压到带空格路径运行，800×600 DIP Settings、六页/深浅主题 DOM、清单哈希、无运行数据和七种图标尺寸通过：`build/release/package-validation-v0.3.0-rc1.json`。此前其余四种尺寸的已通过记录未重复。
- 当前系统真实更新检查：GitHub HTTP 403，Gitee HTTP 200、空 Release 列表；保留部分来源不可用诊断，没有伪造新版本。实际大文件更新/远程上传未执行。
- 新 helper 后台暂停采样约 12.62 秒、21 个样本：主进程峰值 Private Bytes 6.00 MiB，helper 2.63 MiB / Working Set 17.12 MiB；helper CPU 累计增量在进程计时精度内为 0，不能解释为绝对零开销。未观察到 WebView2 子进程，退出后已观察子进程残留 0。原始记录 `build/llvm-release/data/resource-test-1793817587240.json`。不是长期性能保证。
- PowerShell 脚本解析与 `git diff --check` 通过；两个 remote 保持原地址。发布脚本仅 dry-run，未取得或写入令牌。

## 修复过程中保留的失败记录

- 首次前端引用缺少 script 结束标签，导致原生烟测超时；已修复，并新增本地脚本/样式资源加载检查，WebView 回归通过。
- Inno 首次把 prerelease 字符串用于数字资源字段，编译拒绝；现已分离数字字段和文本版本。
- 安装测试最初误选损坏迁移故障库，安装器安全中止；改用专门生成的受控旧格式数据库，未取消故障测试。
- 可选导入页曾用必填目录组件，空值重装失败；改为可空输入后通过。
- 运行中升级会正常更新月 JSON 的时间戳及路径元数据，不能把这当成历史内容损失；测试改为核对历史实际数值、配置与月累计，并在退出后对卸载前后数据做逐文件哈希检查。失败记录仍保留在 `build/installer-validation-*`。
- 更早的任务栏 UIA 自身 provider 查询故障记录见 `build/widget-native-first` / `widget-native-stable`，已有内容子树修复与通过记录，未再重复验证。

## 交付位置

- Release 程序：`build/release/PulseTrace-v0.3.0-rc1-x64/PulseTrace.exe`。
- 安装包：`build/release/PulseTrace-Setup-v0.3.0-rc1-x64.exe`。
- 便携包：`build/release/PulseTrace-v0.3.0-rc1-x64.zip`。
- 两包旁有 `.sha256`，便携目录内有逐文件 `SHA256SUMS.txt`；运行目录不包含开发工具链、独立测试 EXE 或用户数据库。
- 数据路径、备份恢复与构建命令见 [升级与任务栏说明](upgrade-and-taskbar.md)。

## 未完成最终验收 / 外部条件

- **没有可信的 PulseTrace 发布签名证书**：下载完成仅核对一致性，自动执行安装器明确禁用，提供手动安装回退；不能宣称无人值守在线升级已验收。
- 两源尚无本轮正式安装包，GitHub 当前 API 受限。真实 GitHub/Gitee 新版下载、Gitee 附件 CDN、远程发布权限与安装失败后的端到端更新恢复仍需实际发布后验收。脚本不因推送成功就认为附件发布成功。
- 任务栏 100/150% 实机、多屏、Explorer 重启、自动隐藏、居左/居中等组合未执行，不改用户系统设置来制造场景；保留安全回退。当前 125% 子窗口通过不能代替全部兼容性。
- 日期控件已通过当前时区与自动 DST 测试；不同系统辅助功能、键盘可访问性全覆盖、跨显示器 DPI 和视觉细节仍需人工检查。
- 未配置签名的安装包可能触发 Windows SmartScreen；未进行签名发布身份或绕过安全设置。
- IPv4/IPv6 保留 incomplete_observation/reference_validated=false；未完成 Wireshark 对照、全部 TUN/代理/DIRECT/网卡组合及完整精确计费验收。键鼠高频硬件、24/72 小时、Windows 10、MSVC 仍待外部验收。
- 当前源码已按功能提交。下一次显式发布前先核对双仓库分支、审阅并标记版本，再运行 `prepare-release.ps1` 和 `publish-dual.ps1 -Publish`。不强推，不自动发布。

## 历史记录：0.2.0-rc3
当前版本：**0.2.0-rc3 / Windows x64 候选版**。本轮已完成沉浸式原生窗口与 Dashboard 重设计；外部实机验收仍未全部完成。保留公开 RC2 和既有用户数据。

## 本轮完成

- 真实 Windows 原生标题栏/按钮与同色页面、深浅主题、激活/失焦配色；DWM 圆角及背景请求。保留标准非客户区、缩放框和系统最大化按钮 HTMAXBUTTON。
- Sticky 工具栏、CSS 实时背景模糊、滚动状态、透明效果/动画/高对比度回退。仅背景模糊，前景清晰，无逐帧 JS 模糊。
- 六卡片 Dashboard、真实迷你趋势/分布、输入双色柱状图、IPv4/IPv6 四条网络曲线及质量提示、连接与时段摘要；三/二/一列和窄导航。ANSI 104 / Arctic、其余五页、月度数据及文件夹入口保留。
- 最小化停止网页刷新定时器；关闭释放 WebView、核心继续；重开恢复查询。没有改键鼠采集、网络计量算法或 SQLite schema。CoreService 仅更新版本号。
- 自检截图/滚动指标命令仅允许显式 --self-test 使用，固定路径、参数白名单，普通运行拒绝。私有真实数据库只读备份到隔离目录用于可视化，不进入包。

## 实际验证

- 最终 Release / Debug 构建成功，各 **7/7 CTest**；前端 **18,872**、鼠标 56、报文/采集生命周期 152、产品 68、月度归档 19 项通过。前端受测文字/交互最小对比度 5.21:1。
- 800×600、1024×768、1280×800、1600×900、1920×1080 客户区 DIP × 深浅主题，六页 DOM、104 键映射、无文档横向溢出通过；另测 400×600。当前桌面 DPI 125%，不代表所有 DPI。六页和真实统计、滚动/回退截图已实际查看。
- 原生最小化/最大化/还原、双击、拖动/边缘/最大化命中、最小化 refreshTimer=0、托盘关闭及重开通过；最终构建 20 次 UI 生命周期及单实例浏览器异常恢复通过。后台核心心跳持续，SQLite 安全退出。
- 串行约 12 秒暂停状态资源测量：普通窗口组峰值 Private 为 RC2 199.13→RC3 214.20 MiB；后台核心 RC3 5.68 MiB、WebView 子进程 0。20 次生命周期退出后观测到的残留子进程 0，不宣称固定内存保证或长期泄漏验收。
- 数据库历史、事务异常恢复、暂停/恢复、网络持久化、自然月归档和协议回归通过。原有局域 IPv4/IPv6 Echo 验证记录保留。
- 发布包已独立验证 38 文件 SHA-256、解压运行、含空格路径安装、保留配置/数据和七尺寸图标。最终结果以 `build/release/package-validation-rc3.json` 为准。

完整实测和文件清单：[沉浸式 Dashboard](immersive-dashboard-validation.md)。资源原始 JSON、布局与截图记录保留在 build，禁止夹带到发布 ZIP。

## 已有产品功能

- Win32 + 本地 WebView2、受限 JSON 消息桥、六页前端、托盘与独立后台核心；默认暂停，用户主动启用后采集。
- Raw Input 键鼠聚合；ANSI 104、Arctic OKLab 连续热力图、时间范围查询，无输入内容/轨迹记录。
- IP Helper 单接口总量/速率、连接历史、ETW NDIS 物理接口 IPv4/IPv6 分族观测与质量诊断，不叠加 TUN 和物理网卡。
- SQLite v3 批量事务、导出、自然月独立 SQLite/JSON 长期归档；月库不受分钟保留期影响；配置、自启动选项、多尺寸图标和显式安装脚本。

## 未验收与外部条件

- Computer Use 本机管道无法连接。真实鼠标拖动、悬停 Snap 弹层、Win+方向键、跨屏/混合 DPI、其他 Windows/DPI、系统透明/高对比度切换仍需人工验收。原生 HTMAXBUTTON 已测，不把它当作 Snap 弹层实际展示成功。
- DWM 背景请求不代表不透明 WebView 显示 Mica；圆角由系统按最大化/贴靠状态决定，没有用 CSS 或窗口区域模拟原生圆角。
- 一般流量存在长度/卸载拒收，IP 分族仍为 incomplete_observation、reference_validated=false；未完成独立抓包/完整精确计费验收。
- Wireshark 对照、WLAN/Ethernet、TUN 开关/代理/DIRECT、高吞吐/延迟、高速真实 HID、24/72 小时、MSVC、真实登录自启动场景未完成。没有自动安装驱动、改 Clash/代理/路由/防火墙或启用自启动。
- 参考图来自对话，没有工作区原始图片文件；未宣称逐像素匹配。短期 RAF 测量不等于 GPU 呈现 FPS，低端设备仍需实际滚动检查。

## 发布与继续开发

- 正式入口 `web/desktop/index.html`；不依赖 HTTP。旧 HTTP 报告工具只由 PULSETRACE_BUILD_HTTP_REPORT=ON 显式构建。
- 新包：`build/release/PulseTrace-0.2.0-rc3-win-x64.zip`，主程序位于同名目录。解压完整包运行；升级先从托盘完全退出，安装脚本保留 data。
- 默认数据：exe/data/monitor.db、exe/data/network-months/YYYY-MM.sqlite3 与 YYYY-MM.json。用户指定的 --data-dir 继续有效。
- 本轮按用户要求清理测试浏览器缓存并提交、推送及发布 RC3。清理了 48 个已完成测试的临时浏览器配置目录，约 563 MiB；所有源码、依赖、运行数据、测试数据库、日志和截图保留。`tools/cleanup-build.ps1` 默认为预览，显式 -Apply 执行。
- [RC3 发布位置](https://github.com/zhangchengxuv/PulseTrace/releases/tag/v0.2.0-rc3)。发布脚本校验干净源码、远程 main/标记和验证报告中的提交/树/ZIP 哈希一致，仅上传 ZIP、校验和及脱敏验证报告。最终发布回执在 build/release/github-release-rc3.json，不上传本机统计。
- 下一项：按上面的外部条件完成原生交互、其他 Windows/DPI、抓包对照与长时间验证。优先保留真实质量状态，不用估算补齐缺测。
