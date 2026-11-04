# PulseTrace 开发状态

当前版本：0.2.0-rc2，Windows x64 候选版。源码、已完成的功能及以下实际测试记录为准，不将候选版称为完整最终验收完成。

## 已完成

- Win32 + 本地 WebView2、受限 JSON 消息桥、六页前端、系统托盘与后台核心，启动默认暂停。
- 原生主题标题栏、最小化/最大化/关闭、拖动与同色缩放边缘；关闭默认释放 WebView2，后台监测继续。
- 键盘/鼠标 Raw Input 聚合；ANSI 104 布局、Arctic OKLab 连续热力图、范围查询。
- 网络接口总字节/速率、连接历史、ETW NDIS 物理接口 IPv4/IPv6 分族观测及数据质量诊断，不叠加 TUN 与物理接口。
- SQLite schema v3 增量迁移、批量事务、崩溃恢复、导出；自然月独立 SQLite / JSON 集合、六项累计、历史补档，月库不受分钟明细保留天数影响。
- 应用图标、多尺寸资源、安装脚本与干净暂存目录打包，运行数据不进入源码或 ZIP。

## 实际构建与测试

- LLVM-MinGW Release / Debug 构建成功，各 7/7 CTest 通过。
- 前端 18,849 项，原鼠标 56 项，报文 152 项，产品 65 项，月度归档 19 项；真实 Core→主库→月库数据一致。
- 800×600、1024×768、1280×800、1600×900、1920×1080 WebView 客户区 DIP × 深浅主题，六页渲染与无整体横向溢出通过；当前 DPI 为 125%，不代表所有 DPI。
- 原生按钮/拖动与缩放命中、关闭到托盘、后台心跳、重开通过；截图实际查看。WebView2 退出资源复测已观察子进程残留为 0。
- 月末/年末/闰年、幂等写入、保留清理、重启恢复、JSON 一致性、非法月份/路径及明确清空通过；独立安装前后数据库/配置保留和旧网络历史补档通过。
- 局域 IPv4/IPv6 Echo 校验通过；未主动产生大量公网测试流量。

## 已知问题和未验收项

- 一般流量有长度异常拒收，IP 分族仅为已通过头部检查的真实观测子集，标记 incomplete_observation / reference_validated=false。不能称完整精确物理出口流量或校园网计费。
- 独立 Wireshark 抓包对照、有线实机、TUN 开关/代理/DIRECT A-B、高吞吐/时延、24/72 小时、其他 Windows 和 DPI/跨屏、真实 104 键与高速 HID 仍待验收。
- Computer Use 本机管道不可连接；实际鼠标点击验收未执行。内部原生命令和截图已验证，不冒充人工操作。
- Windows 11 最大化按钮悬停 Snap Layout 弹层未实现；MSVC 未在本机执行。

## 文件与后续任务

- 正式入口：web/desktop/index.html。旧 HTTP 报告工具仅在 PULSETRACE_BUILD_HTTP_REPORT=ON 时构建，不作为桌面依赖。
- 数据：exe/data/monitor.db；长期月库与 JSON：exe/data/network-months/YYYY-MM.sqlite3 / YYYY-MM.json。自定义目录沿用 --data-dir。
- 发布：build/release/PulseTrace-0.2.0-rc2-win-x64.zip。使用前解压完整包；升级先从托盘完全退出，再安装到原目录，保留 data。
- 发布前清理后再次完成 Release/Debug 7/7、33 文件哈希、独立解压/安装自检、图标与配置保留复核。tools/test-package.ps1 可复现部署验证，tools/publish-release.ps1 仅在显式调用时发布固定仓库的预发布版。
- 发布清理移除未引用的验证 JS、生成预览、一次性脚本和重复部署副本，保留源码、依赖工具、最终测试记录、截图与本机数据；build/data 不提交 Git。
- 下一项开发：取得受控 LAN 对端及独立抓包真值，处理卸载/捕获分段覆盖，完成上述实机验收。没有安装驱动、改代理/路由/防火墙或自动启用自启动。

详情：[月度归档与标题栏](monthly-traffic-and-caption.md)、[ANSI/Arctic 与网络观测](ansi-arctic-network-validation.md)、[网络诊断工具](network-diagnostics.md)。历史阶段记录保留在其他验证文档；临时验证副本可能在发布清理中删除。
