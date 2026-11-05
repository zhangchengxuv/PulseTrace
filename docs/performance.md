# README 性能与素材核验 / Measurement and asset provenance

## 测量范围

2026-11-05，在 Windows 11 Pro 10.0.26300、AMD Ryzen 5 5600、12 逻辑核、31.92 GiB 可见物理内存、125% DPI、WebView2 156.0.4314.58 上测量。对象为 `14f4f1f1296f2b63e0b58120f0752a2b6cae0cd1` 的 0.3.0-rc2 **Release 配置开发构建**，不是把此前发布的 RC2 附件当作含全部 main 功能的新发布。

主 EXE SHA-256：`0dd4b7157f8cec35f303b118c72584d38f81ed4788f56c7c63cd285d3c89f73d`。三组测试使用同一 EXE；此次只调整文档、许可及许可分发，不改监测核心。

使用真实 Inno 安装器安装到独立工作区目录，确认此前没有本项目安装注册项，未替换已有安装。新 LICENSE 随安装器进入程序目录。测试结束安全退出并卸载测试安装，保留隔离数据；未改自启动、代理、Clash 或系统防火墙。

## 方法与结果

使用现有 `tools/measure-resources.ps1`，三组依次进行，各 45 秒；请求每 500 ms 采样，CIM 枚举使实际间隔约 0.6–0.7 秒。舍弃前 10 秒和 40 秒之后的样本，统计区间约 29 秒：后台/窗口/任务栏分别 47/45/47 个样本。CPU = 所有对应进程在窗口内的 CPU 时间增量 / 墙钟时间 / 12 ×100%，不是单核百分比。

根据 PID、创建时间和父子关系限定进程树，包含主程序、widget helper；界面组额外包含所有观察到的 WebView2 子进程，不包括系统其他 Edge/Clash/测量脚本进程。三个实例均启用键鼠和网络监测；无主动输入或流量生成，Mihomo/进程排行来源及启动更新检查关闭，持久化周期 30 秒。后台/窗口组为 tray 模式，任务栏组为 embedded 模式。进程正常退出，三组均无剩余已观察子进程。

| 状态 | CPU 均值 | Private Bytes 均值 / 峰值 MiB | Working Set 均值 / 峰值 MiB |
| --- | ---: | ---: | ---: |
| 后台，仅托盘 | 0.031330% | 8.296 / 8.527 | 34.583 / 34.777 |
| 界面打开，含 WebView2 | 0.094673% | 261.070 / 270.453 | 456.773 / 465.176 |
| 后台 + 任务栏 | 0.048900% | 9.378 / 9.738 | 40.468 / 40.820 |

任务栏 helper 自身：Private Bytes 平均/峰值 2.810/2.840 MiB，Working Set 17.730/17.750 MiB；没有分辨到 CPU 时间增量。**这不证明零开销。** tray 模式也保留 helper（Private Bytes 平均约 1.75 MiB），所以 helper 整个进程内存不是启用显示前后的增量。

Private Bytes 是私有提交内存，不等于当前驻留物理内存；Working Set 是驻留页，进程求和会重复计算共享页。不要将两者相加，也不要以安装包大小推断内存。短测只有一个设备/一次运行，不是长测、性能上限或所有采集选项的结果；没有测量高吞吐/长时间增长。

## 安装与运行磁盘

按文件逻辑大小求和，不是 NTFS 分配簇、物理写入流量或计费数据量。排除测量脚本自己输出的 `resource-test-*.json`。系统 Evergreen Runtime 单独安装，不计入程序文件。

| 对象 | 字节数 | MiB |
| --- | ---: | ---: |
| 实际安装程序文件，含卸载器、DLL、Web 和许可，排除 data | 18,233,526 | 17.39 |
| 后台组 data，45 秒后 | 100,378 | 0.10 |
| 界面组 data，45 秒后 | 11,237,066 | 10.72 |
| 其中 WebView2 profile/cache | 11,136,478 | 10.62 |
| 任务栏组 data，45 秒后 | 100,373 | 0.10 |

原始资源报告、安装日志、汇总和独立数据在忽略的 `build/readme-review-1793880016626`；不提交个人计数、配置、路径、PID 清单、数据库或日志。这里只发布资源汇总。与历史结果不同的 Windows/Runtime/配置不能直接比较为优化收益。

复现：为三个独立可写目录设置 `capture_on_launch=true`、关闭两个 ranking 来源和启动更新检查，分别设置 taskbar_mode 为 tray/tray/embedded，然后运行：

```powershell
.\tools\measure-resources.ps1 -Mode Core -CoreSeconds 45 -DataDirectory <background-test-directory>
.\tools\measure-resources.ps1 -Mode Window -CoreSeconds 45 -DataDirectory <window-test-directory>
.\tools\measure-resources.ps1 -Mode Core -CoreSeconds 45 -DataDirectory <taskbar-test-directory>
```

## 截图、模型与许可核验

- 四图来自同一当前 Release 配置部署的真实 Win32/WebView2 页面及真正嵌入的 widget，不是 HTML 原型。页面 1600×900 DIP、125% DPI，PNG 均 2018×1172 像素（含原生框架）；任务栏局部保持原尺寸。未用 AI 修图，未合成统计、改图或填充热力图数据。使用新建空目录、默认暂停，人工检查没有路径、域名、IP、输入记录和其他窗口信息。
- 源素材及各页自检记录保存在上述 build 目录的 screens 子目录。只提交已审查的四个 PNG，不提交整桌面/整任务栏或周边窗口。
- 本开发会话运行说明明确标识 Codex 基于 GPT-6；仓库没有完整的逐轮历史模型档案，因此只列这个可确认名称，不把 Codex 当模型、不推测其他型号。AI 声明不表示 OpenAI 拥有、维护、审核或认可本项目。
- 原仓库没有根 LICENSE；在用户明确授权优先 MIT 后，为项目自有材料增加 MIT。核对既有第三方声明、静态依赖和上游 TrafficMonitor 三个主要实现文件，未发现 Anti-996 代码移植；自有 helper/共享内存/绘制结构独立。第三方加载器、运行库与色图继续保留原许可，MIT 不替换它们。见 [第三方声明](../THIRD-PARTY-NOTICES.md)。
- 下载地址取自实际 Git remotes。GitHub API 此轮返回 403，沿用先前已核验的 Release 地址；Gitee 公共 Release API 此轮为空列表，不宣称 Gitee 已有安装包。未在本轮发布/替换任何 Release 附件。

English summary: measurements use the current x64 Release development build, three sequential 45-second runs and seconds 10–40 after warm-up. CPU is normalized over 12 logical processors. Process-tree totals include WebView2; summed Working Sets double-count shared pages. Disk figures are file-size sums, excluding system Runtime and measurement reports. Screenshots use real paused/empty-data windows. Only GPT-6 is confirmed by this session's runtime instructions; no complete historical model inventory exists. Root MIT covers project-owned material only; third-party licenses remain unchanged.
