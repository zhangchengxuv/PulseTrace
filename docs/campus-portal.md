# 通用校园网门户监控 / Generic Campus Portal Monitoring

## 使用

在“设置 → 监测 → 校园网”启用并保存门户 URL。可额外填写登录重定向域名，每行一个。点击“打开门户并识别”，在独立 WebView2 窗口完成学校登录。页面加载后依次执行有限次数分析，复杂 SPA 可点击“重新识别”。

自动结果只在设置页预览，不立即保存。选择候选并确认后才能形成规则；低置信度、冲突字段或页面改版不会覆盖已有规则。无法可靠识别时，可对每个字段点击“点击网页选择”，再回到门户页面点选对应值。

`http://202.204.48.66` 只作为可选测试占位地址，不是默认门户。本轮没有访问、验证或模拟任何真实高校账号。

## 识别与单位

识别器分析可见文本、标签、表格、相邻 DOM 和 aria/class/id 提示，支持余额、已用、剩余、额度、费用和计费周期的中英文关键词。字节使用 B、KB、MB、GB、TB 与 KiB、MiB、GiB、TiB；金额使用元/¥/RMB/CNY。每个字段包含置信度，重复且接近的候选会标记为歧义。

已确认规则保存字段类型、选择器、单位和置信度。每次扫描重新读取映射值；页面结构变化导致选择器失效时只显示“页面结构可能已变化”，不会用自动候选静默替换原规则。

规则保存在 `data/campus/rules.json`；聚合快照保存在 Schema v7 的 `campus_snapshots` 表。不会保存密码、Cookie、Token、完整页面、URL 查询参数、输入内容或浏览历史。

## 安全边界

- 门户窗口使用独立 WebView2 用户目录 `data/campus-webview2`；网页无法获得 PulseTrace Host Objects 或其他原生特权接口。
- 只允许设置中确认的主门户及登录域名。新窗口、主框架和子框架导航会取消其他来源；所有 WebView2 权限请求默认拒绝。
- 不扫描局域网，不绕过验证码、认证或访问控制，不自动填写登录凭据。
- 页面消息只接受候选和结构化快照。快照再次校验配置来源、数值非负、有限、质量枚举和置信度范围。
- 关闭主窗口到托盘、锁屏或完全退出时释放门户窗口；打开期间锁屏/休眠暂停识别，解锁/唤醒后有限恢复。监测暂停只影响自动刷新，不读取其他模块数据。

## 刷新与限制

门户打开时默认每 5 分钟重扫，可设置 5–1440 分钟。关闭门户窗口后不会在后台长期保留第二个 WebView2；此时需再次打开门户同步。针对公开、稳定的官方查询接口，可在后续 SiteAdapter 中接入轻量 HTTP 查询，避免每次打开网页。

通用识别不保证覆盖所有高校。门户真实数据、本地单价估算和未知数据在界面中分开；估算不会写回门户费用。门户数据不等同于物理网卡观测或校园网计费系统。真实登录、验证码、双因素认证、学校改版、长期刷新和账号计费仍需在用户授权环境中人工验收。

## Validation

The controlled test suite covers a table layout, card/proximity layout and definition-list layout, byte and currency units, ambiguity, stale selectors, rule replay, manual candidates, configured-origin validation and SQLite v6→v7 migration. No real campus service was contacted.
