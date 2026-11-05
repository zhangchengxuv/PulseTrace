# PulseTrace 开发状态

当前：**0.3.0-rc2 / Windows x64 候选版**（2026-11-05）。本轮修复任务栏实际不可见、中英双语和安装目录/data 统一。已按功能整理源码提交；双仓库同步状态以实际远程 SHA 核对结果为准，本轮不创建新 Release。

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
