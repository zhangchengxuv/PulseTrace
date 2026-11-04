# PulseTrace 开发状态

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
