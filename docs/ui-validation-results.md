# 蓝色 UI 更新：验证与文件清单

> 以下为蓝色 UI 阶段记录；ANSI 104、Arctic、schema v3 与物理 IP 分族的最新结果见 [本轮记录](ansi-arctic-network-validation.md)。

日期：2026-11-04，Windows 11 x64，实际窗口 DPI=120（125%），系统 WebView2 Runtime，便携 LLVM-MinGW Release / Debug。

## 修改范围

- `web/desktop/index.html`：正式入口、图标、加载空态、状态可访问标签、独立时间范围容器。
- `web/desktop/style.css`、`product.css`：五色板变量、深浅主题、卡片/表格/输入/导航、容器响应式、图表与科学图例。
- `web/desktop/product.js`：Dashboard 分组、趋势图上传/下载图例与线型、真实数据时间聚合、ResizeObserver、暂停/空态、热力图及色图选择、附着 DOM 自检。
- `web/desktop/visualization.js`、`colormaps.js`：纯颜色/对比/时间合并函数，以及完整官方 LUT。
- `web/desktop/assets/app-icon.svg`、`Matplotlib-LICENSE.txt`：应用矢量图和色图许可。
- `assets/PulseTrace.ico`、`PulseTrace-{16,24,32,48,64,128,256}.png`：多尺寸 Windows 资源及预览。
- `src/desktop/resource.h`、`PulseTrace.rc`、`main.cpp`：资源 101、窗口大小图标/托盘图标、简洁加载文案、800×600 最小尺寸、仅 UI 自检的 DPI 尺寸参数。没有修改核心与 WebView2 生命周期算法。
- `CMakeLists.txt`：ICO 依赖、前端 CTest、旧 HTTP 工具默认关闭但保留显式开发选项。
- `tests/frontend_tests.cjs`、`product_tests.cpp`：颜色/聚合/AA 回归；补充开始与暂停状态反映断言。
- `tools/extract-colormaps.cjs`、`generate-icons.ps1`、`test-icons.ps1`、`test-ui-layout.ps1`：可复现资源生成与真实桌面验证。
- `tools/install.ps1`、`package.ps1`：快捷方式图标、发布设计/测试文档与色图许可。
- `README.md`、`THIRD-PARTY-NOTICES.md`、`docs/desktop-guide.md`、`desktop-validation-results.md`、`development-status.md`、`ui-design.md`、本文件：使用、来源、边界与实测记录。

## 实际通过

| 项目 | 结果 |
| --- | --- |
| 最终 Release / Debug | 各完整构建成功，各 CTest 4/4；监测核心与 SQLite 源码未改 |
| 原鼠标回归 | 56 项检查，300 万条样本重放 |
| 产品回归 | 110 项检查，含 SQLite 查询/事务/异常恢复、范围聚合、首次暂停、显式开始/暂停、独立后台线程与协议校验 |
| 前端检查 | 1082 项；四表完整 256 项、端点/中点、1024 个键帽文字配色、缺测与零区分、≤240 点时间聚合及总量保持、中文编码检查 |
| 对比度 | 所测普通文字/交互配色最低 5.21:1；曲线与输入边界 ≥3:1；四套 LUT 的黑/白键帽文字全部 ≥4.5:1 |
| 初轮布局矩阵 | 800×600、1024×768、1280×800、1600×900、1920×1080 客户区 DIP，各深浅主题；每次附着渲染六页、切换两种主题检查，均退出码 0，无页面整体横向溢出 |
| 图标 | EXE 检出一个图标组，资源 101 在 16/24/32/48/64/128/256 均加载成功；窗口大/小图标显式设置；托盘读取同一资源 |
| 最后 UI 资源测量 | `resource-test-1793774785497.json`，退出码 0，已观察子进程残留 0；主进程 Private Bytes 约 6.08–6.86 MiB，进程组采样峰值约 234.89 MiB。短期阶段测量，不是固定承诺；500 ms 可能漏掉短暂进程，IO 含 IPC |
| ZIP 与安装副本 | 22 个发布文件逐项 SHA-256 校验通过，包含全部本地色图/脚本/图标/许可；无 data、数据库、日志或截图夹带。独立解压后键盘页自检返回 0；带空格目录安装后设置页自检返回 0；临时快捷方式图标引用验证通过 |

首轮输出：`build/ui-validation-1793774346006/`，含十组 Dashboard 截图、其余五页截图和 results.json；`build/frontend-contrast.json` 为颜色配对计算值。自检页面只使用实际核心/数据库数据；空库截图为真实空态。自检不写用户 Run 项、不清理用户数据库。

最终尺寸矩阵复测输出：`build/ui-validation-1793774636413/`，十组和五页全部退出码 0。最后截图审查发现零值图例仍使用中性色而非色图起点，并发现一次 PowerShell→Node 管道的中文编码损坏；均已修正，增加了真实 DOM 零值图例颜色断言及源文本编码检查。修复后 Release/Debug 各 CTest 4/4 再次通过，浅色完整键盘截图 `build/keyboard-final-1793774876549/logs/selftest-keyboard.png` 已实际查看，图例零值为 Viridis 起点色，无记录键为中性虚线。没有以删除测试方式处理问题。

实际截图通过 WebView2 CapturePreview 生成；已查看窄窗口浅色 Dashboard、宽窗口深色 Dashboard、键盘、网络、设置与图标 PNG。完整键盘截图在最终检查时定位热力图区域。显示缩放不能只看截图物理分辨率：800×600 DIP 实际客户端 1000×750 像素，日志记录 DPI 与客户区尺寸。

独立解压夹具：`build/blue-ui-deployment-1793774955542/`；安装夹具：`build/blue install 1793774955542/PulseTrace/`。使用 NoShortcut 安装，没有修改真实开始菜单、自启动或用户监测数据。发布副本再次通过七尺寸 EXE 图标加载检查。文档收尾后重新打包并核对哈希，运行二进制与上述自检相同。

## 尚待核对 / 人工视觉验收

- 100%/150%/200% 及混合 DPI 跨屏、触摸/键盘无障碍操作、屏幕阅读器、真实 Explorer 图标缓存及托盘图标的人工观感尚待检查。
- 实际应用捕获的键位长期分布及四套色图在用户显示器上的辨识效果，需要用户真实数据复核；没有向正式统计写入模拟计数。
- 精确 IPv4/IPv6 分项仍未实现，保持“暂不支持”；本次不修改网络采集口径。
- 长时、硬件真值与其他 Windows 版本限制仍沿用原候选版记录，不能用 UI 自检代替。

## 复现命令

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Debug
node tests/frontend_tests.cjs build/frontend-contrast.json
.\tools\test-ui-layout.ps1
.\tools\test-icons.ps1
.\tools\measure-resources.ps1 -Mode UI -Cycles 2
.\tools\package.ps1
```

Node 只用于开发测试/数据提取；发布程序不需要 Node/Python。CMake 在找到 Node 时注册前端测试；本机已实际执行该测试，没有以缺失运行时跳过。
