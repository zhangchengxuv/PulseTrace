> 历史 MouseMonitor 验证阶段记录；当前桌面产品状态以根目录 README 和 development-status 为准。

# PulseTrace — MouseMonitor 最小验证阶段

> **最新开发方向：C++20 + Win32 + WebView2 桌面应用。** 已完成现有项目检查及[第一阶段最小改动设计](webview2-integration-plan.md)，尚未实现桌面入口、WebView2 或托盘。本轮保留现有采集代码。此前中断的 HTTP 报告页只有 HTML/CSS，缺少 `web/app.js`；相关目标能编译，但页面功能未完成，不能当作已可用的 Dashboard。以下是现有鼠标验证程序的说明。

当前目录最初为空，没有既有 KeyboardMonitor、NetworkMonitor、SQLite 或 Web 代码。本次先落实新增需求中的 **独立鼠标验证程序**，没有生成完整监测产品。采集准确性和真实输入延迟通过硬件验收后，才进入数据库和 Web 集成。

已提供 C++20 / Windows 原生 Raw Input 采集、独立聚合核心、测试、性能报告工具，以及后续集成设计。没有 Hook、输入拦截、输入注入或高频输入状态轮询。

## 已实现的统计口径

- 左、右、中、Mouse4、Mouse5 按下次数；同一已识别设备的重复 DOWN 不重复计数。
- **时间双击**：同一设备、同一来源类别、同一按键的 DOWN → UP → DOWN，两个 DOWN 间隔不超过 `GetDoubleClickTime()`；不重叠配对，三连击计 1 次、四连击计 2 次。第二个 DOWN 所在分钟得到双击计数，两次普通点击仍分别计数。
- 不读取坐标，因此不验证 Windows 标准双击所要求的位置条件。该指标不能等同于应用实际收到的 `WM_*DBLCLK`。设备句柄为空或状态表已满时，保留点击计数，但不推断双击。
- 分别统计垂直滚轮正、负非零事件次数及增量绝对值；保留有符号 16 位原始增量，先汇总再除以 120。`+15 × 8` 是 8 个事件、120 delta、1 个标准刻度。
- 净量 = 上滚量 − 下滚量，总量 = 上滚量 + 下滚量。水平滚轮单列，不混入上下滚动。
- 分钟时间桶，支持内存内分钟、UTC 小时、UTC 天、自定义 `[from_minute,to_minute)` 范围查询。精度为整分钟，不提供任意秒边界的精确历史；默认保留最近有事件分钟之前的 1440 分钟窗口，最多 10080 分钟。
- 设备句柄只用于临时去重和双击状态；报告不保存设备身份、坐标、轨迹、原始包、逐事件时间戳或操作内容。

Raw Input 的设备句柄不能证明物理来源；空句柄也不能证明一定是触控板。当前报告区分 `device_reported` 和 `no_device`，其余来源类别为扩展预留，不把未知数据伪装成可靠的物理/合成/触控板分类。详见 [技术设计](architecture.md)。

## 构建

Windows 10/11，推荐 VS 2022 或更新的 C++ 桌面开发工具和 Windows SDK，CMake 3.24+：

```powershell
cmake -S . -B build/msvc -A x64
cmake --build build/msvc --config Release --parallel
ctest --test-dir build/msvc -C Release --output-on-failure
```

本工作区已将 LLVM-MinGW 20260616（Clang 22.1.8）及 CMake 3.31.8 解压到 `build/tools`，没有修改系统安装或 PATH。可直接复现本次构建：

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Debug
```

如果复制源码到其他机器，便携工具需从 [LLVM-MinGW 官方发布](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260616) 与 [CMake 官方发布](https://github.com/Kitware/CMake/releases/tag/v3.31.8) 手动取得，或使用已安装的 MSVC。第三方构建工具的许可见 [依赖说明](dependencies.md)。MinGW 静态链接运行库，MSVC 使用静态 CRT；实际独立运行兼容性仍需目标机器测试。

## 运行

以下路径使用便携 Release 构建；MSVC 对应路径为 `build/msvc/Release`。

```powershell
# 无参数仅显示帮助，不开始统计
.\build\llvm-release\mouse_probe.exe

# 显式启用鼠标统计，30 秒后退出；也可以 Ctrl+C
.\build\llvm-release\mouse_probe.exe --capture --seconds 30

# 同一消息线程同时接收键盘，用于并发负载验收；仅统计键盘总 DOWN 数
.\build\llvm-release\mouse_probe.exe --capture --seconds 120 --keyboard-count

# 聚合器压力测试：不操作或注入系统鼠标/键盘
.\build\llvm-release\mouse_benchmark.exe --seconds 60

# 外部高速摄像/光电传感器的输入延迟对照测试目标
.\build\llvm-release\mouse_latency_target.exe
```

捕获最长 7 天；超过默认历史窗口时，只保留固定容量的分钟桶以及会话累计计数。`--retention-minutes 10080` 可以保留 7 天分钟桶。

报告只写入 **exe 所在目录的 `data/`**，与当前工作目录无关。目录不可写时明确报错，不另选路径。鼠标采集期间不写统计文件，正常结束后一次性写出聚合 JSON；异常退出会丢失本次会话全部尚未导出的统计，可能留下空报告。验证阶段没有 SQLite，不宣称已具备崩溃恢复。重放基准每秒输出一条资源聚合采样到独立报告，这不在输入回调中执行。

## 测试与状态

Release / Debug 均实际构建并通过 56 项检查，覆盖 300 万条输入样本重放。60 秒 Release 聚合器压力测试计数相符，私有内存未增长；5 秒 Raw Input 空闲烟测成功，但没有收到鼠标包，不能视为硬件采集验收。

[测试方案与命令](validation.md) · [实测结果及未验证项](validation-results.md) · [SQLite / Web 集成契约](integration.md)

真实高频点击丢失率、连续滚轮准确性、操作端到端延迟、键鼠同时高频输入 CPU、24/72 小时运行，以及 Windows 10 / 其他 Windows 11 版本，均仍待验证。SQLite、HTTP API 和 Web 可视化尚未实现，已有独立模块也无法在空工作区进行回归验证。

## 文件职责

| 路径 | 职责 |
| --- | --- |
| `include/pulsetrace/mouse_monitor.hpp`, `src/mouse_monitor.cpp` | 无 Windows 依赖的有界聚合器、双击状态和范围查询 |
| `include/pulsetrace/raw_input_host.hpp`, `src/raw_input_host.cpp` | 唯一的 Raw Input 注册与消息循环；鼠标和可选键盘总量测试共用线程 |
| `src/mouse_probe.cpp`, `src/probe_report.cpp` | 显式启动采集、会话后聚合报告 |
| `tests/mouse_tests.cpp` | 边界、设备隔离、精密滚轮、范围及大量重放计数测试 |
| `tests/mouse_benchmark.cpp`, `tests/mouse_latency_target.cpp` | 资源压力测试、外部延迟测试目标 |
| `tools/` | 构建、实际捕获与独立预期计数比较 |
| `docs/` | 技术依据、分阶段设计、测试方案、实测状态 |

