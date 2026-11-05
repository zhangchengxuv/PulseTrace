# 任务栏逐像素透明（2026-11-05）

## 根因与实现

旧 `taskbar_widget.cpp::paint` 用 `FillRect` 将整个矩形填成深蓝/浅灰，布局代码又调用 `SetLayeredWindowAttributes(...,255,LWA_ALPHA)`。这只启用了整窗合成，背景仍不透明，与任务栏下方材质及 TranslucentTB 的设置无关。

现在使用缓存的两张 32 位、自上而下 DIB：GDI 在黑底蒙版上以 `ANTIALIASED_QUALITY` 生成灰度字形；按覆盖率生成预乘 BGRA，再由 `UpdateLayeredWindow + ULW_ALPHA + AC_SRC_ALPHA` 一次提交完整小画面。默认背景像素是 `(0,0,0,0)`；实心文字 Alpha 为 255，抗锯齿边缘保留覆盖率。不使用 ClearType 彩色子像素，不通过全窗降透明度减弱文字，也不与 `SetLayeredWindowAttributes` 混用。

- 保留 `Shell_TrayWnd` 子窗口、无激活、独立 native helper、原有布局/重连/菜单与真实 NetworkMonitor 快照。没有改变系统任务栏其他窗口尺寸，没有修改 TranslucentTB 配置或调用其内部接口。
- 透明 padding 通过原生 layered hit testing 和 `HTTRANSPARENT` 透传；文字/箭头区域保留点击。没有添加整窗 `WS_EX_TRANSPARENT`。
- 保留 Segoe UI Variable Text / Segoe UI 回退；下载、上传箭头用品牌蓝色，普通文字跟随系统任务栏深浅主题。浅色主题的箭头使用较深蓝色以保持辨识度。
- 字体、内存 DC、DIB 在尺寸/DPI 不变时复用；一秒读取一次已有快照，仅内容/主题/布局变化时重绘。原有小范围可见性复核继续保留。
- 设置增加“完全透明 / 淡色背景（回退）”，默认透明；淡色背景 Alpha 为 32/255，实心文字仍为 255。没有模拟独立 Acrylic。旧配置自动补充该默认项，不改数据库结构或监测算法。
- IPC 协议升级为 3，使主程序与 helper 对新增背景参数、Alpha 质量计数进行一致校验；发布/复制运行文件时必须保持两者配套。

API 依据：[Microsoft UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow)、[SetLayeredWindowAttributes 的混用限制](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setlayeredwindowattributes)。本实现独立编写，不引入 TranslucentTB 代码或依赖。

## 实际验证

当前 Windows 11、125% DPI，TranslucentTB 正在运行。保持其现有配置，没有为测试切换模式或写入配置文件。

| 项目 | 实际结果 |
| --- | --- |
| Release / Debug | 两种配置构建成功，各 13/13 CTest 通过 |
| 离屏绘制/资源 | 148 项几何、IPC、Alpha、缓存与释放检查；96/120/144/192 DPI、深浅色、透明/淡色背景及 500 次重复绘制 |
| 真实任务栏父窗口 | `Shell_TrayWnd` 父子关系、可见性、屏幕文字采样 16/16 通过 |
| 真正透明 | 将自有组件短暂隐藏，读取真实下方任务栏，再恢复并比较；18/18 padding 样本一致，非伪装背景色 |
| Alpha 质量 | 暂停画面 7,062 个 Alpha=0 像素、312 个不透明字形像素、411 个抗锯齿像素 |
| 命中测试 | padding 命中下方窗口，字形命中组件；没有整窗鼠标穿透 |
| 交互与生命周期 | 点击打开主界面、右键菜单、关闭到托盘后继续显示、安全退出均通过 |
| 真实网速 | 被动读取真实物理接口快照，两次采样速率发生变化；没有生成公网测试流量。具体值与原始截图仅保留本地 |
| 回退背景 | 背景 Alpha=32，实心文字仍不透明，真实屏幕 16/16 文字匹配；800×600 英语设置页自检通过 |
| 配置兼容 | 默认透明、非法背景值拒绝、保存后重启恢复、切换时保留同一采集 session 与暂停状态 |
| 双语 | 379 个资源，2,228 项资源检查及 6 项 widget 状态检查通过 |

最后仅将数字紧邻箭头左对齐；随后已重建 Release/Debug、复测 148 项绘制检查，并重新执行真实活动网速的 Alpha/命中/截图验收。没有重复不受该排版调整影响的数据库和网络长测。

本地证据（不进入 Git 或 Release）：

- 旧背景：`build/widget-alpha-baseline/evidence/taskbar-screen.png`。
- 新透明、交互及 TranslucentTB 运行状态：`build/widget-alpha-final-1f07b6f992354582a065c83fb583a4b3`；`evidence/taskbar-context.png` 提供组件及系统托盘上下文。
- 最终真实活动画面：`build/widget-alpha-live-final-29882647515d4933b492470d0d77d31d/evidence`。
- 真实接口变化验证：`build/widget-alpha-real-network-112a514064e240da83986e9a177e34a3/result.json`。
- 回退背景与英语设置：`build/widget-alpha-subtle-c6027e57b25941eabb81261d584a10f2`。
- 全部 CTest：`build/widget-alpha-ctest-release-final.log`、`build/widget-alpha-ctest-debug-final.log`。

## 短期后台资源观察

顺序测量旧 RC2 与新程序，各约 13 秒，默认暂停，无 WebView2 UI。只统计对应测试进程树，测量脚本自身开销不计入应用。

| helper 指标 | 旧 RC2 | 透明绘制 |
| --- | ---: | ---: |
| Private Bytes 峰值 | 2.79 MiB | 2.86 MiB |
| Working Set 峰值 | 17.48 MiB | 17.54 MiB |
| 采样区间 CPU 时间增量 | 0.015625 s | 0 s（计数器未分辨增量） |
| 首/末进程句柄数 | 187 / 187 | 187 / 187 |
| 退出后已观察子进程残留 | 0 | 0 |

新增约 0.07 MiB 私有内存与缓存 DIB 相符；这些短期数字不代表固定性能保证，CPU 计数器为 0 也不等于零开销。GDI 资源另外由 500 次绘制、DPI/尺寸重建和销毁检查验证。原始记录见 `build/widget-alpha-performance.json` 及其中两个报告路径。

首次资源采样遇到测量脚本将报告错误写往 EXE/data 的问题，未产生有效报告；已修正为所选测试数据目录，重新顺序测量。失败日志仍保留，未用于上表。

## 人工验收与限制

- 当前 TranslucentTB 会话验证通过，不能代替 Normal、Clear、Acrylic 的逐项模式验收。保留当前配置，没有自动切换用户模式。
- [TranslucentTB 官方说明](https://github.com/TranslucentTB/TranslucentTB#features) 将 Blur 限于 Windows 10 和 Windows 11 build 22000；当前系统并非该构建，不能承诺该模式可用。
- 其他显示器与系统 DPI、Windows 10、实际切换 Windows 深浅色、Explorer 重启、自动隐藏任务栏及长时间运行仍需人工验收。离屏四档 DPI/深浅色通过不能替代这些实机组合。
- 人工步骤：保持 PulseTrace 启动且暂停，分别在 TranslucentTB 自有界面选择可用模式，检查原生组件背景与任务栏连续；开始监测查看速率变化，点击字形打开窗口、右键菜单、关闭到托盘，最后从托盘完全退出。切换 Windows 主题/DPI 或重启 Explorer 仅由用户主动执行。

## 版本边界

上一项 GitHub `v0.3.0-rc2` Release 已发布并核验 4 个附件，提交为 `fd84970`；Gitee 同标签已同步，但 Release API 返回 401，尚未创建发行版。本轮透明修复属于后续源码提交，未覆盖那个已发布版本的附件，也未自动创建新 Release。新代码运行入口为 `build/llvm-release/PulseTrace.exe`，应使用同目录配套的 `PulseTraceWidget.exe`、WebView2Loader 和本地资源。
