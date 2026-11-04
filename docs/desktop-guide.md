# PulseTrace 0.2.0-rc1

PulseTrace 是 Windows x64 本地桌面统计应用，使用 C++20、Win32、系统 Evergreen WebView2、Raw Input 和 SQLite。当前交付定位为**候选版本**：可运行功能与外部人工验收分开记录，不宣称完整最终验收通过。

## 启动与使用

解压完整发布包到一个可写目录，运行 `PulseTrace.exe`。不要单独移动 exe；同目录需要 `WebView2Loader.dll`、`web/desktop/` 和许可证文件。

首次启动默认暂停统计，点击右上角“开始监测”后采集。界面含总览、网络、键盘、鼠标、历史和设置六页；所有统计来自本机核心与 SQLite，没有正式使用数据模拟模式。无记录时显示零值或空状态。

- 关闭主窗口默认隐藏到托盘，停止并释放 WebView2 页面，后台核心继续运行。
- 点击托盘图标或“打开 PulseTrace”重新创建页面，读取最新聚合数据。
- 托盘右键可开始、暂停、打开今日统计/设置、完全退出。
- 完全退出先停止采集和提交数据库，再等待 WebView2 进程组结束，删除托盘图标。
- 这是桌面程序，不依赖 `http://127.0.0.1:8765`。该端口被其他程序占用不影响桌面版本。

程序使用系统已有 Evergreen WebView2 Runtime，不打包 Chromium，不需要 Node.js 或 Python 后台。缺 Runtime 时显示原生诊断窗口与错误码，可安装微软官方 Runtime 后重开程序；不自动安装或更改系统安全设置。

## 安装

可以直接便携运行；也可显式运行发布包中的安装脚本：

```powershell
.\tools\install.ps1 -Destination "$env:LOCALAPPDATA\Programs\PulseTrace"
```

安装器复制运行文件并创建开始菜单快捷方式，不启用开机启动或监测。`-NoShortcut` 可只复制运行文件。更新前从托盘完全退出；安装器保留已有 data。

开机自启动仅在设置页主动勾选时修改当前用户 Run 项；与“下次启动恢复监测”是两个独立设置。后者默认关闭。开发测试不会擅自启用自启动。

## 数据与隐私

默认路径取 exe 的绝对目录，不取工作目录：

```text
data/
  monitor.db          SQLite 聚合统计与连接观察历史
  monitor.db-wal      SQLite 自己管理，运行时不要单独移动或删除
  monitor.db-shm
  config.json         用户设置
  window.json         窗口位置与尺寸
  webview2/           本应用 WebView2 用户数据
  logs/               有界运行日志与显式自检产物
```

目录不可写时显示错误并让用户手动选目录，不静默写到其他位置。也可明确指定：

```powershell
.\PulseTrace.exe --data-dir "D:\PulseTraceData"
```

设置页显示实际数据目录，支持范围 CSV 导出、保留期与持久化周期。清空统计必须先暂停并输入 `DELETE`；不会删除配置或系统 Runtime。备份前完全退出，再复制 data/monitor.db；不要在运行中仅拷贝主数据库忽略 WAL。

默认每 30 秒批量持久化，可设置为 5–300 秒；正常退出做最终提交。突然断电/强制终止可能丢失最近未提交批次，但已提交事务保持一致。磁盘忙或写失败时暂停采集并保留待重试聚合，退避重试，不在输入回调写数据库；失败退出会要求明确选择是否丢弃未写数据。严重数据库异常保留原文件并显示诊断，不自动覆盖数据库。

键鼠只保存分钟聚合计数，不保存文本、按键顺序、可还原文本的逐键时间戳、鼠标坐标、轨迹、剪贴板或窗口文本。输入设备句柄只在内存中做短期去重。Raw Input 无法识别所有密码输入；敏感操作前应主动暂停。锁屏通知触发输入暂停和临时状态清理，仍须在目标系统人工验证锁屏/解锁行为。

## 统计口径

**键盘**按物理扫描码与 E0/E1 扩展位隔离键位，左右修饰键、主键区/小键盘可区分；按住产生的重复 MAKE 单列，不重复算物理按下。Pause 无通常 BREAK 的情况和 PrintScreen 的伪 Shift 报告单独处理。热力图是 ANSI 位置示意，字母标签不是实际输入内容；其他布局、媒体键和设备固件差异需结合扫描码排名及实机验收。

**鼠标**支持五键、时间双击、垂直/水平滚轮。时间双击是同设备同按键 DOWN→UP→DOWN、两个 DOWN 不超过系统双击时限，不重叠配对；不验证位置，因此不是 Windows 标准空间双击。普通两次点击仍分别计数。空句柄不推断双击。

滚轮事件与 delta 分开：`+15 × 8` 是 8 个事件、120 delta、1 个标准刻度；先累加再换算，不逐条截断。正负量分别保留，水平独立。来源仅按 Raw Input 证据说明，有句柄不证明物理设备，空句柄不证明触控板；不声称全部物理/合成/触控板输入可靠三分。

**时间范围**使用 `[from,to)` 整分钟边界；界面输入本地时间，传输和存储 UTC。分钟/小时/天汇总中小时和天按 UTC 边界，不能把 UTC 天冒充本地日历天。“今天”范围以本地零点转换为 UTC。当前查询上限 366 天、100000 个聚合指标行，超限需选更粗粒度。历史页每次呈现 100 行，网络观察历史最多返回最近 500 条。

**网络**提供 IP Helper 单接口收发 octet 与速率、物理/虚拟标志、已配置地址族、Windows NLM 系统级 IPv4/IPv6 互联网连通性提示，以及连接/断开/接口选择观察历史。接口列表与计数低频采样约 1 秒；短暂切换可能无法捕捉，记录时间是观察时刻，初始状态不伪装成实际连接发生时间。

自动选择首个活动物理接口，也可在设置中明确选物理网卡或 Mihomo TUN。不会把所有接口相加。接口总量不等于应用净荷、物理介质全部开销或校园网账单。NLM 提示是系统级状态，不证明指定接口对任意地址都可达。

**IPv4/IPv6 上传下载分项当前未支持，界面明确显示未知。** 普通权限接口 octet 没有准确分族字节字段；不按 IP 地址、包数或比例猜测。物理出口、代理原始流量、TUN 内层和封装外层必须分开。精确分项需要独立验证的可选 ETW/包观察后端；本版本不安装驱动、不拦截或修改数据包、不申请管理员权限，不更改 Clash、代理或防火墙。

## 构建

工作区已有的便携 LLVM-MinGW 与 CMake：

```powershell
.\tools\bootstrap-deps.ps1
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\build\llvm-release\PulseTrace.exe
```

固定依赖为 WebView2 SDK 1.0.4314.35、nlohmann/json 3.12.0、SQLite 3.54.1。bootstrap 只在 build/deps 下载官方文件并校验固定 SHA-256；SQLite 还独立比对过官方公布 SHA3-256，Loader 的 Microsoft Authenticode 签名实测有效。构建不会安装驱动或更改全局 PATH。

已有 MSVC + Windows SDK + CMake 时：

```powershell
cmake -S . -B build/msvc -A x64
cmake --build build/msvc --config Release --parallel
ctest --test-dir build/msvc -C Release --output-on-failure
```

MSVC 构建尚未在本机执行。`-DPULSETRACE_BUILD_DESKTOP=OFF` 可只构建原有独立验证工具。保留了 mouse_probe、mouse_benchmark、mouse_latency_target；HTTP 报告原型不作为桌面依赖。

## 测试与交付状态

```powershell
ctest --test-dir build/llvm-release --output-on-failure
.\build\llvm-release\PulseTrace.exe --self-test --cycles 20
.\tools\measure-resources.ps1 -Mode UI -Cycles 2
.\tools\measure-resources.ps1 -Mode Core -CoreSeconds 30
```

自检会创建自己的聚合数据库/日志并正常退出；`--data-dir` 可指向专用测试目录。自检不伪造物理输入，也不启用自启动。`--core-seconds` 是定时后台诊断模式，默认暂停采集；不会创建 WebView2。

已实际完成 Release 编译、鼠标原有检查、产品键盘/SQLite/协议/线程测试、事务中异常退出恢复、20 次桌面开关循环和进程组释放测量。详细数值、失败后修复记录及最后部署测试以 `docs/development-status.md` 和 `docs/desktop-validation-results.md` 为准。

Windows 10/其他 Windows 11、MSVC、真实高频 HID 丢失率、端到端输入延迟、高速下载吞吐量、Clash TUN 开关对照、24/72 小时运行，以及自启动真实登录场景仍待人工验收。不得把进程内重放、零包烟测或短期内存测量当成这些测试通过。

## 项目模块

```text
include/pulsetrace/  独立聚合、输入宿主、键盘、数据库、网络、CoreService、协议
src/                各模块实现
src/desktop/        Win32 生命周期、托盘、WebView2 与原生消息桥
web/desktop/        六页桌面前端与本地样式
tests/              鼠标、产品集成、崩溃恢复测试和性能工具
tools/              固定依赖、构建、测量、打包与用户主动运行的安装器
docs/               当前进度、实测结果、设计与历史验证说明
build/              构建、依赖和候选发布包（不提交 Git）
```
