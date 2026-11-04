# 融合标题栏与自然月流量集合（0.2.0-rc2）

## 使用方式与文件路径

总览、网络和历史页新增“已使用流量”区域：累计上传/下载接口总量，IPv4 上传/下载和 IPv6 上传/下载四项 IP 观测。可独立选择本自然月、历史月份、全部已保存月份，不受上方短期时间范围影响。每项同时显示换算单位和原始字节数。

数据默认在 **PulseTrace.exe 所在目录**：

```text
data/monitor.db                         原有分钟明细与输入统计，保留原配置
data/network-months/calendar.json       首次归档选定的 Windows 时区
data/network-months/2026-11.sqlite3     2026 年 11 月网络统计集合
data/network-months/2026-11.json        同一集合的可读汇总
data/network-months/2026-12.sqlite3     下一自然月独立创建
data/network-months/2026-12.json
```

界面仅提供“打开文件夹”，不再显示绝对路径。如果手动指定了 `--data-dir`，以上文件位于选定数据目录。未产生网络样本的月份不创建空集合，界面显示“暂无数据”。JSON 是自动更新的汇总，SQLite 是完整聚合记录；运行中 SQLite 可能另有 `-wal`/`-shm` 文件，备份应先从托盘完全退出应用或使用 SQLite 备份工具。

默认新包路径为 `build/release/PulseTrace-0.2.0-rc2-win-x64/`。发布包不包含本机监测数据。升级保留历史的做法：先在旧版托盘中完全退出，再运行 rc2 解压包的 `tools/install.ps1 -Destination <原安装目录>`。安装器保留 `data/`；下次启动会把主库仍保留的网络历史补入相应月份。已被旧保留策略删除的历史无法恢复，不估算补齐。直接在另一目录启动新包会使用该目录自己的新数据。

## 统计与保存口径

- 接口总量来自已选接口 octet 增量，只累计监测启用期间。不会把多个网卡或 TUN 与物理接口简单相加。暂停期间不新增用量，不读取网卡启动以来的全部计数冒充软件已监测用量。
- IPv4/IPv6 保留上一版 ETW NDIS 的真实观测及拒收/丢失诊断；与接口总量属于不同口径和采样边界，不相加、不能保证相等。当前仍存在长度异常拒收，明确为“不完整观测”，`reference_validated=false`，不等同校园网计费。
- 按首次创建归档时的 Windows 命名时区划分自然月，时区写在 calendar.json。重启沿用该时区，不因系统显示时区变化而把旧集合重新分组。使用系统历史时区转换；时间桶是 UTC 整分钟，跨月接口采样增量按采样所在分钟归属，不宣称亚分钟计费精度。
- 独立月库复用受验证的 SQLite schema v3、事务和预编译 UPSERT；主库结构和已有数据不重建。归档键包含 session、minute、module/metric（IP 质量表另含接口和来源），重复提交及启动补档不重复累加。
- 仅归档网络字节聚合及质量元数据，不复制键盘/鼠标数据、数据包载荷、地址或访问内容。
- 保留天数现在明确是**分钟明细保留天数**。小时清理只作用于原主库，月库和 JSON 长期保留。用户主动“清空聚合统计”会一并删除月度集合，确认文案已明确说明。
- 归档在原后台批量持久化周期内执行，默认 30 秒，可配置 5–300 秒；界面标明仅显示已提交批次及最近提交时间。采集回调不写文件。先提交主库，再提交月库，最后原子替换 JSON；启动在清理前流式补档、重新生成 JSON，恢复主库已提交但月库/JSON 尚未完成的批次。归档失败沿用现有暂停、报错和重试机制。
- JSON 作为小型汇总缓存供页面读取，避免每三秒扫描整月 SQLite 明细。月度文件随使用增长，不承诺固定磁盘占用。
- 事件/缓冲丢失诊断取已报告计数的最大值，避免将每分钟重复报告的会话累计数相加；不是逐包去重后的月度丢失总数。

## 标题栏

移除标准浅色非客户标题区，保留 WS_THICKFRAME / 最大化等窗口能力。原生 36 DIP 自绘标题区匹配深浅蓝主题，三个 owner-draw Button 支持可访问名称与焦点；按钮发送标准系统命令。保留拖动、双击标题区最大化、边缘缩放、Alt+F4 及既有关闭到托盘策略。Win11 外框颜色抑制失败时保持可用，Windows 10 不依赖该新属性。

WebView2 Bounds 从标题区下方开始，不覆盖原生按钮；正常窗口另留同色 6 DIP 原生缩放边缘，避免浏览器子窗口截走缩放命中，最大化时去除该留边。自检实际检查子窗口未覆盖右侧边缘及 HTRIGHT 命中。异常时的原生诊断文本保留。RawInputHost、键鼠算法及后台核心不依赖新标题区。Windows 11 悬停最大化的 Snap Layout 弹层未实现，不声称与系统标题栏完全等效。

## 实际验证与边界

- Release / Debug 完整构建，各 7/7 CTest 通过；前端 18,849 项，原鼠标 56 项，报文 152 项，产品 65 项；新增月度 19 项及真实 Core→主库→月库一致性。
- 月度检查覆盖本地月末/年末、闰年、重复提交、更新、多个会话、全月份总量、旧库补档、主库已提交后的恢复、保留清理不影响月库、JSON/SQLite 一致、拒收/质量保留、非法月份/路径拒绝、清空范围。
- 原生自检通过拖动命中、最小化、最大化/还原、关闭按钮到托盘、后台心跳及重新打开。窗口图像为本应用 PrintWindow 原生标题区与实际 WebView2 CapturePreview 的合成，不是设计稿。已查看深色原生窗口及真实月度数据截图。
- 独立普通权限被动采集后重启读档通过；接口总量与四项 IP 观测均进入月库和 JSON，保留不完整质量标记。没有主动产生公网测试流量。详细本机捕获数值不随源码公开。
- 新月度测试初次失败是测试读取 JSON 的句柄未关闭，已修复作用域。原生图像自检曾因 CapturePreview 完成回调内输出流仍被持有而失败，已移到随后消息周期再读取；未绕过失败测试。
- Computer Use 两次返回 native pipe unavailable，未执行基于真实鼠标的 UI 点击。内部原生命令、真实渲染与截图可以验证当前运行路径；其他 Windows/DPI、跨屏拖动/缩放、辅助功能仍需人工检查。原有网络卸载/独立抓包/有线/TUN 专项和长时限制未因新增累计而消失。

## 修改文件

新增 `monthly_traffic.hpp/.cpp`、`caption_bar.hpp/.cpp`、`monthly_tests.cpp`、`web/desktop/monthly-view.js`；扩展 `database.hpp/.cpp`（网络流式恢复与汇总）、`core_service.hpp/.cpp`（批量月度归档/查询/清空）、`protocol.cpp`（受限月份与目录消息）、`src/desktop/main.cpp`、`webview_host.cpp`、版本资源、CMake、真实网络回归测试、桌面 product.js/product.css/index.html、打包脚本及 README/进度/本说明。没有修改键鼠聚合算法或网络包分类算法。

依据：[Microsoft 自定义窗口边框](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)、[DWM 窗口属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)。

## 最终发布复核

最终发布复核：Release/Debug 各 7/7，最终五档 DIP×深浅主题及六页检查为 build/ui-validation-1793793537983/，原生右侧缩放边缘未被 WebView 子窗口覆盖的实际检查通过。资源报告 build/llvm-release/data/resource-test-1793793591088.json：退出 0，已观察 WebView2 子进程残留 0。

清理后的 rc2 ZIP 共 33 个清单文件，SHA-256 全部相符；没有夹带运行数据，移除了未引用的桌面验证脚本。独立解压网络页、带空格路径安装设置页均退出 0，七档图标加载通过。安装前后预存数据库/配置保留，旧网络历史补档测试通过。最终程序与 ZIP 位于 build/release/PulseTrace-0.2.0-rc2-win-x64。临时部署副本已在发布清理中移除，可用 tools/test-package.ps1 重新验证。
