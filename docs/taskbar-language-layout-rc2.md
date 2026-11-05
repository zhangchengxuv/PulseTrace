# 0.3.0-rc2：任务栏显示、双语与同目录数据

本轮是在 RC1 工作树上增量修改。采集算法、SQLite schema 3、网络统计口径和双源发布地址保持原状。源码已在后续 Git 管理整理中按功能提交；没有创建本版远程 Release。

## 任务栏的实际根因与修复

旧版的 HWND 有效、WS_CHILD/WS_VISIBLE、GetParent、矩形和命中测试均正常；PrintWindow 自身画面也包含文字，但真实任务栏屏幕画面为空。当前 Windows 11 的任务栏组合层没有呈现普通 GDI 子窗口的内容。仅验证父子关系的旧验收不充分，不能作为实际可见的证据。

现以 Windows 10/11 兼容清单声明 layered child 支持，使用独立原生 helper 的 WS_EX_LAYERED 和 255 alpha 合成；仍为 Shell_TrayWnd 的真正子窗口，不改系统窗口尺寸、不注入 Explorer。字体使用系统实际注册的 **Segoe UI Variable Text**；GDI 中笼统的 “Segoe UI Variable” 在本机实际回退宋体，现已核实字体家族并在不可用时回退 Segoe UI。

状态增加独立的可见性、绘制次数和实际屏幕文字匹配。先完成定位、绘制，再核对自己矩形内的文字像素；成功后每 5 秒小范围复核，不逐帧截屏，不写入画面。长期遮挡或不可见时安全回退，不把 WS_CHILD 当作显示成功。暂停显示暂停，异常显示不可用；每秒更新现有线程安全速率快照，没有第二套网络采集器。

实际证据均为本机任务栏屏幕，保存在工作区，不进入发布包：

- 修复前：`build/widget-invisible-baseline/evidence/taskbar-screen.png`，对照同目录 `widget-own-*.png`（自身有字，屏幕无字）。
- 修复后真实速率：`build/widget-real-rate-rc2-final/first/taskbar-screen.png` 与 `second/taskbar-screen.png`；已核对实际物理接口快照与显示随时间变化，没有主动生成公网流量。具体流量值和原始截图仅保存在本地，不提交源码仓库。
- 正确字体：`build/widget-real-rate-final-font/evidence/taskbar-screen.png`。
- 最终检查：`build/widget-visible-rc2-final/logs/widget-validation.json`：任务栏父窗口、可见样式、paint_count>0，16/16 文字采样匹配。关闭 UI、保留核心、重开和完全退出通过。

验证平台为当前 Windows 11、125% DPI。Explorer 重启、其他任务栏美化软件、多显示器、100/150%、真实自动隐藏与居左排列组合仍须人工测试；没有为测试强行重启用户 Explorer 或改变系统配置。

## 双语

- `web/desktop/locales/zh-CN.json` / `en-US.json` 是集中资源源文件，375 个稳定资源 ID；生成前端本地字典和原生 C++ 字典。没有额外运行时和远程翻译请求。
- 覆盖六页、图表、日期时间、提示、更新、托盘菜单和网速菜单。Native 主要错误有本地化标题/说明，详细 API/数据库诊断可保留原始技术文字。接口名称等用户数据及协议/指标 ID 不作翻译。
- 设置提供 system / zh-CN / en-US。原生按 Windows 用户 UI 语言选择中文或英文，其他语言回退英语；设置持久化，切换不重启采集模块。语言切换实测 session、monitoring 和 input_ready 均不变。
- 日期保持分钟精度、本地输入→UTC 查询，独立草稿不受实时刷新影响。中英日历截图在 CTest 原生烟测的 `logs/selftest-toolbar-calendar-zh.png` / `calendar-en.png`。
- 安装器用 Inno 随包的 ChineseSimplified.isl 与 Default.isl，所有自定义页面、错误和完成项均有两套 CustomMessages；卸载沿用安装时选择的语言。升级不覆盖应用已有语言，首次空安装才写入安装语言建议。
- 五种 DIP 窗口尺寸（800×600、1024×768、1280×800、1600×900、1920×1080）的六页×两种语言×深浅主题 DOM 布局检查通过。实际英语 Settings 截图及结果在 `build/bilingual-layout-1793864678485`。

完整资源检查 2,208 项通过。当前系统 UI 为中文；显式英语安装/首启已测，不能把它称为“已在另一台英语 Windows 系统验收”。Computer Use 原生管道本轮仍无法连接；截图来自项目的 Win32/WebView2 验证工具，真实菜单点击和安装向导逐页视觉仍可人工复核。

## 目录与权限

新安装默认 `D:\Program Files\PulseTrace`；D 不存在时明确提示并建议 `%LOCALAPPDATA%\Programs\PulseTrace`。升级优先保留已有安装位置。只选择一次程序目录，数据固定为 `<安装目录>\data`，无独立数据目录页；安装向导显示数据位置预览。

统一 `DataDirectoryManager` 从 EXE 绝对目录定位。显式 `--data-dir` 仍可用于已授权的诊断/便携场景；带旧版外部指针的安装标记不静默改成空数据库，会要求通过安装器迁移。

路径检查在复制前验证可用卷、持久 ACL、专用目录和写入能力。**当前普通账户实际不能在 D:\Program Files 创建 PulseTrace**，已验证安全拒绝；默认 D 路径的实际落盘需要用户明确提升安装权限，或选择可写位置。没有修改 D:\Program Files 的 ACL，也没有让主程序长期管理员运行。

权限只作用于用户选择的 PulseTrace 安装树：SYSTEM/Administrators 保留管理权限，原登录用户对程序 EXE/DLL只读执行、对 data 可修改，无 Everyone 全控或所有用户数据库写权限。Inno 的卸载 .dat 记录必须可写，属于单文件必要例外，不授予 EXE 修改权限。升级/卸载时由安装器临时解锁本安装树，再完成替换或删除。使用原用户进程获取 SID；不同凭据提升时无法可靠识别原账户则停止，不能猜测并授予管理员账户的数据权限。

已有且确实属于本安装路径的自启动项只修正旧数据目录参数或在卸载时删除；没有现有项则不会创建。不将更换更新源、语言或目录当作启用开机启动的授权。

## 迁移与保留

检测旧 installed.ini 或旧 LocalAppData 数据，显示迁移选项；目标已有库时提供明确选择，默认保留目标，不合并和覆盖。通过窗口消息安全退出旧实例；对独立源使用 Restart Manager **查询**仍在用的进程并请求安全退出，不调用强制关闭。SQLite 主库和月库均用 Online Backup，迁移前留下备份、原目录保留；数据 schema 仍为 3。

升级备份现在位于 data/backups；迁移源的预迁移备份留在源/backups。程序更新不会将数据库作为资源覆盖。普通卸载保留 data，重装会恢复读取，现有语言、网速模式、更新源、保留期及启动采集偏好均保留。

实际记录：

- 英文安装/导入/运行中升级/卸载：`build/installer-validation-0c8957d210cb4d729ccef8fa2f215dba`。
- 最终维护程序英文回归：`build/installer-validation-d72bb47c02d1486bb6b46872131957c8`。
- 中文安装、EXE 不可写/data 可写、运行中升级、卸载及重装：`build/installer-validation-dd5256cbedad4abface5135ca2148673`。
- RC1 外部数据→同目录数据（空目标）：`build/legacy-layout-32de1bb8205c46c79881104bc8a8fd94`。
- RC1 外部数据和目标都有数据（保留目标）：`build/legacy-layout-325aad051eda40c6b2c70512c9f2dbb1`。
- 新中文/英文安装语言建议、800×600 首启、默认暂停：`build/fresh-language-validation-rc2.json`。

以上只使用生成的测试数据。历史键盘 42、上传 1000/下载 2000 等受控值、月度累计及用户设置验证一致；卸载前后数据文件哈希一致，原数据源保留。

## 构建和未验收项

Release/Debug 实际构建，全部 **13/13 CTest** 通过：新增双语检查、37 项路径/迁移/备份/本地化检查，原有键鼠、网络、月库、协议、历史、WebView2 生命周期继续通过。随后仅调整字体和像素复核开销，重新构建两种配置并通过真实可见性及安装程序回归，没有为文档重复长测。

最终包见 `build/release/PulseTrace-Setup-v0.3.0-rc2-x64.exe` 与 `PulseTrace-v0.3.0-rc2-x64.zip`，旁附 SHA-256。构建命令沿用 tools/build.ps1、package.ps1、build-installer.ps1；资源修改后可执行 tools/generate-locales.cjs，CMake 在有 Node 开发工具时也会按依赖生成，运行时不依赖 Node。

仍未最终验收：受保护 D 默认目录的提升安装、另一台英语系统、D 不存在的真实机器、Explorer/自动隐藏/多屏/DPI 组合、逐项菜单视觉和长期运行。无可信发布签名，在线下载仍不自动执行 EXE；物理 IPv4/IPv6 仍保留未完成独立对照验收标识。未发布远程 Release，未宣称所有外部验收已经完成。
