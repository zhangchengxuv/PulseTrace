# 通用校园网门户监控 / Generic Campus Portal Monitoring

## 使用

在“设置 → 监测 → 校园网”的基础设置中启用并填写门户 URL，修改自动保存并反馈结果。点击“打开门户并识别”，在独立 WebView2 窗口完成学校登录。页面加载后依次执行有限次数分析，复杂 SPA 可在高级设置中点击“重新识别”。登录重定向域名也在高级设置中，每行一个。

自动结果预填在概览下方，只在设置页预览，点击“确认识别结果”后才能形成规则。低置信度、冲突字段或页面改版不会覆盖已有规则。无法可靠识别时，展开默认折叠的高级设置，选择候选或点击“点击网页选择”，再回到门户页面点选对应值。调整计费参数不会隐式确认网页候选。

“点击网页选择”会打开或恢复 PulseTrace 内置门户窗口，复用已登录页面。网页仍在加载时等待就绪，再准备最近选择的字段；按钮下方显示打开、需登录、就绪或失败提示。完成登录后选择数值；关闭门户会取消点选，避免拦截后续正常网页点击。点选期间自动刷新不会打断操作。

设置分为基础设置、校园网概览、计费设置和高级设置。用户无需编写公式、选择器或字段模式；旧版已保存的手动值及安全公式继续兼容。计费只填写免费额度、单价、GB/GiB 和账期；免费额度优先消耗、账户余额可用于流量消费均需明确勾选确认，程序内置计算。参数留空则使用网页值。

门户地址由用户配置，不内置学校地址。2026-11-06 已在用户指定的实际门户进行隔离只读测试，结果与限制见[实测记录](campus-navigation-validation.md#实际门户只读测试2026-11-06)。

## 识别与单位

识别器分析可见文本、标签、表格、相邻 DOM 和 aria/class/id 提示，支持余额、已用、剩余、额度、费用和计费周期的中英文关键词。名称和数值分列时，也会检查同一视觉行右侧的临近可见数值，作为较低置信度候选供用户确认；确认后保存数值元素的选择器，后续无需重新推断布局。含义不明确的“流量”不会直接映射为已用或剩余流量。字节使用 B、KB、MB、GB、TB 与 KiB、MiB、GiB、TiB；金额使用元/¥/RMB/CNY。每个字段包含置信度，重复且接近的候选会标记为歧义。

已确认规则保存字段类型、模式、选择器、单位、置信度和计算表达式。每次扫描只重新读取自动/映射字段；页面结构变化导致选择器失效时只显示“页面结构可能已变化”，不会用自动候选静默替换原规则。

## 可用流量计算

计算值始终使用字节作为内部单位，手动输入支持 B、KB、MB、GB、KiB、MiB、GiB。字段可以按以下顺序取值：

1. 网页实时值。
2. 用户手动值。
3. 安全公式值。
4. 明确的未知状态。

公式仅接受数字、字段名、加减乘除、括号及 `min`、`max`、`abs`，不接受脚本或系统调用。可用变量包括全部已配置字段以及 `GiB`、`GB`、`MiB`、`MB`。

门户直接提供免费剩余流量时优先使用。否则在免费总额度与已用免费流量都存在时计算 `max(0, 免费总额度 − 已用免费流量)`。普通套餐额度、总已用和普通剩余不能替代免费字段。资金可使用流量为 `可用于流量消费的余额 ÷ 流量单价 × 1 GiB`；单价为零或缺失时显示无法计算。总可用流量仅在免费剩余与资金可使用流量都可计算时求和。

门户真实费用、本地单价估算和余额保持独立，估算仅根据付费已用流量，不会再次从账户余额扣除。免费额度进度只在额度与使用量来自网页或用户确认的手动值时显示；公式和估算不会冒充真实门户进度。启用账期重置的手动值会记录确认账期，新账期或无法确认账期时显示无法计算。

规则保存在 `data/campus/rules.json`；聚合快照保存在 Schema v7 的 `campus_snapshots` 表。不会保存密码、Cookie、Token、完整页面、URL 查询参数、输入内容或浏览历史。

## 安全边界

- 门户窗口使用独立 WebView2 用户目录 `data/campus-webview2`；网页无法获得 PulseTrace Host Objects 或其他原生特权接口。
- 只允许设置中确认的主门户及登录域名。新窗口、主框架和子框架导航会取消其他来源；所有 WebView2 权限请求默认拒绝。
- 不扫描局域网，不绕过验证码、认证或访问控制，不自动填写登录凭据。
- 页面消息只接受候选和结构化快照。快照再次校验配置来源、数值非负、有限、质量枚举和置信度范围。
- 校园网开关单独控制门户同步。关闭门户窗口或主窗口到托盘后隐藏已授权门户，继续定时同步；禁用校园网、收紧地址/域名范围或退出应用时释放旧门户。后台查询不读取其他监测模块的数据。
- 锁屏/休眠暂停识别并丢弃旧扫描结果；解锁与唤醒均完成后重新请求门户，不把休眠前缓存当作新同步。浏览器进程异常后在下一次同步重新创建，认证失效保留最后有效快照。
- WebView2 管理隔离浏览器会话；应用不把密码、Cookie、Token 写入配置、统计 SQLite 或日志，并明确禁用密码自动保存和通用表单自动填充。相关设置依据 [WebView2 Settings4 文档](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2settings4)。

## 刷新与限制

首次启用时打开门户完成登录和结果确认。之后默认每 5 分钟通过已配置的无查询参数地址重新请求页面，再识别数据，可设置 5–1440 分钟；不会重放登录 POST。确认新规则后可立即重新识别，无需重开窗口。登录页、隐藏字段、导航前返回的旧扫描和失效规则均不能覆盖最后有效快照。

已启用并保存规则时，重启应用会自动恢复隐藏门户，不弹出登录窗口；认证失效时在界面显示需重新登录。关闭门户或主界面到托盘仍同步；停用开关或完全退出停止同步。通用 DOM 回退需要保留一个隔离 WebView2，会占用相应浏览器资源；默认未启用校园网时不创建它。针对公开、稳定的官方查询接口，可在后续 SiteAdapter 中接入轻量 HTTP 查询，避免保留浏览器。

通用识别不保证覆盖所有高校。门户真实数据、本地单价估算和未知数据在界面中分开；估算不会写回门户费用。门户数据不等同于物理网卡观测或校园网计费系统。真实登录、验证码、双因素认证、学校改版、长期刷新和账号计费仍需在用户授权环境中人工验收。

## Validation

The default controlled test suite contacts loopback fixtures only. It covers tables, cards, definition lists, separate visual columns, byte and currency units, ambiguity, stale selectors, rule replay, manual candidates, manual/formula fields, safe formulas, cycle reset, zero-price handling, navigation atomic replacement and configured-origin validation.

An explicitly authorized real portal can be checked separately with `build/llvm-release/campus_portal_native_tests.exe --live-url <URL>` (Debug is also supported). This opt-in test uses a fresh isolated WebView2 profile, arms and cancels all 11 pickers, and replays candidate selectors in memory. It does not click page controls, submit forms, save rules or open the user database; output contains field names and test status, without account values or raw page diagnostics. A visible password form returns exit code 2 and requires local login outside this test. Passing this check does not validate billing semantics, manual value clicks or long-term authentication recovery.
