# RC3 沉浸式界面与验证

2026-11-04，Windows 11 x64，当前桌面 DPI 120（125%）。参考为本轮对话附图：已核对三列两行卡片、两幅趋势图、深蓝配色和间距；没有工作区原始参考图文件，未做逐像素差分。

## 窗口与资源

- 标题栏使用真实 Windows 非客户区和系统按钮，保留 WS_OVERLAPPEDWINDOW。DwmDefWindowProc / DefWindowProc 处理命中与原生操作；不使用自绘按钮冒充系统 Snap。
- DWM 请求深色、标题/文本/边框色、DWMWCP_ROUND 和系统背景。Windows 不支持的属性返回失败时保留标准窗口。最大化和贴靠时由系统决定圆角；没有设置非矩形窗口区域。
- 原生按钮采用用户允许的稳定方案，与正文同色，保留独立且紧凑的原生标题区。没有覆盖按钮或让 WebView 接管非客户区。
- WebView 使用不透明背景，并在支持的 ControllerOptions 中提前设置颜色，旧 Runtime 路径仍在 Controller 创建后设置。请求系统背景成功不代表不透明 WebView 后面能看到 Mica。
- 读取系统透明效果、动画和高对比度偏好；变化通知触发更新。不会修改这些系统设置。
- 最小化通知网页停止刷新；关闭到托盘释放控制器和页面；独立核心、SQLite 批量写入保持原架构。重开重新加载真实统计。

微软依据：[DWM 窗口框架](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)、[原生圆角及系统限制](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners)。

## 页面与统计

- 配色和圆角统一为 CSS Tokens。主内容最大 1480 CSS px；KPI 根据内容宽度切换三/二/一列；侧栏 216/72/58px。图表有固定高度，键盘可在组件内部滚动。
- Sticky 顶部合并标题、采集状态、控制按钮和时间筛选。CSS backdrop-filter 模糊底层内容，前景文字不加 filter。IntersectionObserver 只切换滚动状态类，ResizeObserver 只处理工具栏/画布尺寸。禁用透明效果和不支持背景模糊时使用不透明背景。
- 六卡片：键盘真实趋势、鼠标按键占比、上下滚轮事件占比、监测覆盖时长、实时上下行、IPv4/IPv6 观测占比。图标按青蓝/薄荷/琥珀/紫区分语义，不引入持续动画。
- 监测时长不冒充活跃时长，不生成无可靠分母的活跃百分比。上期比较要求等长区间和相近监测覆盖，缺少可比数据时不显示涨跌。
- 输入图为双色聚合柱状图，最多 96 个时间桶，聚合保留次数。网络图每线最多 240 点，族颜色和上下行线型同时区分；缺测不补零。实时迷你图最多保留 40 条实际采样。
- 历史/网络/设置等其余页面沿用统一 Tokens；ANSI 104 映射、Arctic OKLab 色图和平方根归一化未改。月度累计仍可展开，文件夹入口保留，不重新显示冗长绝对路径。
- 不新增逐键时间序列、鼠标轨迹或假活动列表。IPv4/IPv6 仍标记待验证/不完整观测，不能当作完整精确计费。

## 已执行

- LLVM-MinGW Release、Debug 各 7/7 CTest；前端 18,872 项，最小文本/交互对比度 5.21:1；鼠标 56、报文/采集生命周期 152、产品 68、月度归档 19 项。
- SQLite 历史/事务中断恢复、键鼠聚合、开始/暂停、网络持久化、月度归档、JSON 协议回归通过。此次没有修改采集算法或数据库 schema。
- 五种 WebView 客户区 DIP：800×600、1024×768、1280×800、1600×900、1920×1080，分别深/浅主题，六页渲染、104 键唯一映射、对比度及无文档横向溢出检查通过。另保存五个非总览页面截图。
- 原生最小化/最大化/还原、标题栏双击、HTCAPTION/HTRIGHT/HTMAXBUTTON=9、关闭到托盘、核心心跳、重开及安全退出通过。实际最小化后执行受限自检脚本，确认 nativeVisible=false、refreshTimer=0。
- 最终 RC3 构建再次完成 20 个页面生命周期（19 次重开）；只终止专用测试实例浏览器 PID 的异常恢复测试也通过。
- ZIP 38 个文件哈希、独立解压启动、含空格路径安装后启动、配置和数据保留、16/24/32/48/64/128/256 原生图标均通过；包内没有统计数据库、日志、抓包、测试截图或旧原型 app.js。
- Dashboard 真实 WebView 截图包含顶部、滚动中、关闭透明效果三个状态；自动检查 Sticky 顶部位置、图表没有盖住工具栏、前景无模糊及不透明回退。截图为真实 WebView 渲染，窗口图另外叠加原生 PrintWindow 非客户区。
- `ui_validation_snapshot` 通过只读 SQLite 连接及 backup API 为本机数据建立隔离测试副本；仅修改副本，暂停采集，用于真实统计可视化。私有统计、副本和截图不进入发布包。

布局记录：`build/ui-validation-1793802923732/results.json`。另测 400×600 DIP 窄窗口，单列与折叠筛选通过；已查看六页及深浅主题、原生标题栏、真实统计和滚动状态截图。自检文件和日志保留在各测试目录 `logs/`。

## 资源测量

串行、全新测试目录、默认暂停、约 12 秒短测。Window 模式通过现有定时退出及首次重开诊断打开普通页面，不执行 --self-test 截图/遍历。CPU 为 4–11 秒区间内相邻采样差值、单核百分比，不是整机任务管理器百分比；500ms 级采样可能漏掉短命进程。初次尝试遗漏旧版定时参数隐含的后台行为，因此没有浏览器的 Window 报告已作废；修正后脚本要求实际观测到 WebView 子进程。

| 场景 | 主进程峰值 Private MiB | WebView 子进程峰值合计 MiB | 组峰值 Private MiB | 组峰值 Working Set MiB | 组 CPU 单核 % |
| --- | ---: | ---: | ---: | ---: | ---: |
| RC2 普通窗口 | 12.35 | 188.32 | 199.13 | 405.08 | 2.43 |
| RC3 普通窗口 | 7.41 | 206.79 | 214.20 | 402.20 | 2.17 |
| RC2 仅后台核心 | 6.06 | 0 | 6.06 | 22.40 | 0.23 |
| RC3 仅后台核心 | 5.68 | 0 | 5.68 | 20.65 | 0.46 |

普通窗口组线程峰值 224→213、组句柄峰值 3632→3488；后台核心线程均为 9，句柄 247→238。短测启动、缓存和系统调度有波动，不能据此宣称 CPU/内存必然降低；新版普通窗口组 Private 峰值增加约 15 MiB。Working Set 相加包含共享页，不等于独占物理内存。IO 写传输包括 IPC，未当作磁盘实写量。

原始有效报告：

- RC2 窗口/核心：`build/release/PulseTrace-0.2.0-rc2-win-x64/data/resource-test-1793803373530.json`、`resource-test-1793803442356.json`。
- RC3 窗口/核心：`build/llvm-release/data/resource-test-1793803400785.json`、`resource-test-1793803180277.json`。
- RC3 20 次生命周期：`build/llvm-release/data/resource-test-1793803570266.json`，采样覆盖 63.08 秒。截图、布局遍历及重新初始化使组 Private 峰值达到 387.89 MiB，此值不与普通窗口待机混用。45 个仅剩主进程的采样中 Private 为 15.93–16.18 MiB；完全退出后观测到的残留子进程为 0。不能宣称从未出现短暂残留，也不能视为长期内存泄漏验收。

一次暖重开的 open→NavigationCompleted 为 RC2 387ms、RC3 392ms；RC3 的 19 次重开为 371–440ms，不包含之后统计查询及图表完成时间。

1280×800 DIP 的 36 个 RAF 回调：旧页面平均 7.89ms / P95 8.40ms，新页面开启模糊平均 8.15ms / P95 8.50ms。新页面内容更紧凑，滚动距离 207px，旧页面 320px，且旧页面采样时原生标题栏已切换，因此不是严格的整应用 A/B。真实统计副本开启模糊为 8.26ms / P95 8.40ms。均为程序化滚动回调间隔，不冒充呈现 FPS 或真实滚轮延迟；未观测到本次短测中明显卡顿，不保证低端 GPU 表现。

汇总：`build/immersive-metrics-summary.json`。所有有效测量退出码为 0、退出后观测到的残留子进程为 0。CPU 主进程/子进程、线程、句柄及 IO 原始采样均保留在上述报告。

## 复现

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Debug
.\tools\test-ui-layout.ps1
.\tools\measure-resources.ps1 -Mode Window -CoreSeconds 12 -DataDirectory build/resource-window
.\tools\measure-resources.ps1 -Mode Core -CoreSeconds 12 -DataDirectory build/resource-core
.\tools\package.ps1
.\tools\test-package.ps1
```

## 未验收

- Computer Use 本机管道不可连接；真实鼠标拖动、最大化按钮悬停 Snap 弹层、Win+方向键、跨屏混合 DPI、其他 DPI 和 Windows 10 需人工验证。原生命中测试不等于完成这些交互验收。
- 原生圆角请求和截图已检查；PrintWindow 不能代替 DWM 合成外轮廓及贴靠状态实机观察。透明效果关闭路径已测，系统高对比度/减少动画切换尚需实机操作。
- 短期 RAF 测量只反映程序化滚动回调间隔，不等于 GPU 呈现帧率、真实滚轮体验或长期性能。吞吐量/时延、真实高速 HID、独立抓包对照、TUN A/B 和 24/72 小时稳定性继续列为外部验收。
- MSVC 未在本机执行。便携包不包含完整 Chromium、HTTP 服务或 Node 后台；目标机需要系统 WebView2 Runtime。

## 修改文件

- 原生：`include/pulsetrace/desktop/{caption_bar,webview_host}.hpp`、`src/desktop/{caption_bar,main,webview_host}.cpp`。
- 界面：`web/desktop/{index.html,style.css,immersive.css,product.js,dashboard-data.js,dashboard.js,ui-validation.js}`。
- 自检：`src/protocol.cpp`、`tests/{frontend_tests.cjs,product_tests.cpp,ui_validation_snapshot.cpp}`、`CMakeLists.txt`。截图命令仅 --self-test 可用，路径固定，正常应用拒绝此命令。
- 版本/发布：`src/core_service.cpp`（仅版本）、`src/desktop/PulseTrace.rc`、`tools/{measure-resources,package,test-package,publish-release}.ps1` 及 README/本文/进度文档。
