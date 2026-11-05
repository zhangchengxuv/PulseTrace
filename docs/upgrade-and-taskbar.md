# PulseTrace 0.3.0-rc1

本版为 Windows x64 候选版本。正式程序不启动 HTTP 服务，不修改键鼠采集算法、网络计量口径或既有用户数据。

## 使用

- 日期筛选采用蓝色自定义日历。年月日与小时/分钟先编辑草稿，确认后提交；取消、Esc 或外部点击丢弃草稿。选择器 DOM 独立于统计刷新，分钟精度、本地时间显示、UTC 分钟查询。夏令时不存在的时间拒绝提交；重复时刻采用 JavaScript 的较早时刻，不提供两次重复时刻的独立选择。
- 设置 → 任务栏网速：嵌入、独立悬浮、仅托盘。默认尝试真正的任务栏子 HWND，只使用所选物理接口的既有速率快照。暂停、未连接、数据过期与采集错误不会继续显示旧速率。悬浮模式明确为独立窗口，可按 Ctrl 拖动。
- 嵌入使用独立 Win32 helper，避免跨进程 SetParent 的 DPI 行为影响主程序。只移动自己的 HWND，不注入 Explorer、不缩放系统窗口、不另开 WebView2。UI Automation 只读取占用区域矩形，不保存应用名称。无法证明有空位时回退托盘；Explorer/显示器变化触发重新定位，SetParent 重试有界。
- 设置 → 软件更新：默认手动检查、稳定通道、双源自动选择；启动检查需主动启用。先选最新有效版本，再比较同版本大小和 SHA-256，再按响应选择来源。只允许同版本、同大小、同哈希的备用下载。错误信息和部分来源失效均可查看。
- 本项目未配置可信的 PulseTrace 发布签名身份。下载核对大小、SHA-256、x64 PE 后仅提供**手动安装**，不会自动执行 EXE。SHA-256 不等同于发布者身份验证。安装前查看发布来源，并从托盘完全退出旧版。无网络不影响离线统计。

## 安装与数据

安装包：`PulseTrace-Setup-v0.3.0-rc1-x64.exe`；便携包：`PulseTrace-v0.3.0-rc1-x64.zip`。

- 便携版默认 `exe/data`，完整解压后运行；显式 `--data-dir` 仍优先。
- 安装版默认 `%LOCALAPPDATA%\Programs\PulseTrace`，数据默认 `%LOCALAPPDATA%\PulseTrace\data`。安装器的 `installed.ini` 指向用户选择的绝对数据目录。数据不作为程序资源打包或覆盖。
- 便携导入在安装器中主动指定旧 `data`，目标必须为空。先安全关闭对应 EXE，再使用 SQLite Online Backup 复制主数据库和月库；保留原目录，不自动合并两份历史。失败的目标副本保留诊断，重试应选择新的空目录。
- 升级/重装先通过注册窗口消息请求**该安装路径**的进程安全退出，等待最多 60 秒，不强杀。更早版本不能理解该消息时，请手动从托盘退出。数据库保存失败时升级停止。
- 升级前数据备份位于 `%LOCALAPPDATA%\PulseTrace-UpgradeBackups\时间戳`；设置页“备份数据库”位于当前数据目录的 `backups/manual-时间戳`。运行中手动备份包含已提交数据，未落盘实时聚合不在其中。安装前关闭后的备份包含最终提交数据。
- SQLite `user_version` 支持顺序 v1→v2→v3，升级前 Online Backup，事务内验证与回滚；拒绝更高 schema。月库沿用相同数据库机制。配置 v0→v1 保留未知字段，补充默认值并留存旧文件；新增兼容字段不更改现有配置版本。高版本配置拒绝覆盖。
- 普通卸载删除程序/快捷方式及匹配本安装 EXE 的启动项，**保留统计数据**。自动开启监测与 Windows 登录自启动均默认关闭。
- 恢复旧版前先关闭所有相关实例，将升级备份恢复到单独目录，使用兼容版本显式指定该目录。不要让旧程序直接写更高 schema 的数据库；不提供未经验证的自动降级或自动数据回滚。

## 构建与发布

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Debug
.\tools\bootstrap-inno.ps1
.\tools\package.ps1
.\tools\build-installer.ps1
.\tools\test-installer.ps1
.\tools\test-package.ps1
.\tools\publish-dual.ps1   # 只输出计划，不推送/建 Release/上传
```

唯一版本源为 `cmake/version.cmake`；CMake 生成 C++/RC 头与 `version.json`，Settings、文件名和安装器从中派生。Inno 编译器以固定 ZIP 哈希及其工具发布者 Authenticode 校验，仅解压到工作区；工具证书不是 PulseTrace 发布身份。

远程地址：GitHub `zhangchengxuv/PulseTrace`，Gitee `zhangchenxuv/pulse-trace`。客户端没有令牌。GitHub Releases 的 assets 与 Gitee 独立 `attach_files` 分开解析。HTTPS 跳转仅允许固定项目来源及代码列出的下载 CDN；新域名会拒绝并报告，不自动放宽。

远程写入需要显式 `tools/publish-dual.ps1 -Publish`、干净且已标记的提交、绑定提交/树及安装包哈希的验证报告，以及发布进程环境中对应来源的 API 令牌。可用 `-ReleaseDirectory` 指定已验证附件目录，用 `-Sources github` 或 `-Sources gitee` 单独发布/重试，避免一个来源缺少凭据阻塞另一个。脚本不创建提交、不强推、不覆盖已发布附件。两源分别记录分支/标签 SHA 与附件结果；Gitee 无 GitHub 式 draft，本脚本先创建预发布，完整附件核验后再按版本设置通道。部分成功时保留回执，不宣称两源均成功。Git 仓库可推送不代表 Release API 令牌有效。

## 验证边界

实际结果见 `development-status.md`。真实任务栏父子关系已在当前 Windows 11 /125% 验证；100/150%、多屏、Explorer 重启、自动隐藏及不同任务栏排列仍需实机人工验收。几何单测不能代替这些实机结果。

当前 GitHub API 实测 HTTP 403，Gitee API 200 且无 Release。模拟更新覆盖协议、校验、取消与错误路径，不能代替两源真实新版本升级。未上传本轮 Release；无发布证书，所以自动执行安装流程被明确禁用。

物理 IPv4/IPv6 数据继续保留不完整观测与未完成独立抓包验收标识，不宣称校园网计费级精确。其余系统版本、24/72 小时、真实高频输入和吞吐/延迟对照仍属人工验收。

技术依据：[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)（研究其任务栏机制，无代码移植、无 MFC 依赖）、[SetParent](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setparent)、[WinHTTP 选项](https://learn.microsoft.com/windows/win32/winhttp/option-flags)、[WinVerifyTrust](https://learn.microsoft.com/windows/win32/api/wintrust/nf-wintrust-winverifytrust)、[SQLite Backup API](https://sqlite.org/backup.html)、[Gitee V5 Swagger](https://gitee.com/api/v5/swagger_doc)、[Inno Setup](https://jrsoftware.org/ishelp/)。
