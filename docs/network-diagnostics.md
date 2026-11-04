# 网络诊断工具

发布包中位于 `tools/diagnostics/`，主程序不会自动运行这些工具。不需要 Npcap，也不会安装驱动或更改代理、路由、防火墙、自启动。标准 Windows 系统 API；权限不足会输出实际错误码。

- `network_capability_probe.exe`：重新检查三个 manifest 提供程序及旧内核会话的启动/启用权限，仅短暂创建并关闭自己命名的会话；不能用 Enable=0 宣称字节已验证。
- `network_etw_probe.exe`：3 秒被动 NDIS 观察；输出所选物理接口的分族观测、拒收和丢失，无主动测试流量、不写 ETL/PCAP。
- `physical_network_probe.exe --seconds 60`：正式 NetworkMonitor 的被动采样与进程 CPU/Private Bytes。秒数为 1–3600，默认 5；等待定时结束，让会话正常收尾。性能结果仅代表该进程和当时负载。
- `local_echo_validation.exe`：无参数只显示帮助。显式 `--local-echo` 才向当前物理接口的私有 IPv4 网关、IPv6 链路本地网关各发送最多 2 个 16 字节载荷 Echo，不选择公网地址。比较系统 Echo 回复和 ETW 的已知测试包字节；没有网关或回复不代表软件验证通过。

输出是聚合 JSON；可自行重定向到选定文件。不会输出或保存通信载荷、访问内容或完整网络连接，Echo 地址和测试载荷也不写入报告。`*_passed`、`quality`、错误码与丢失字段必须一同阅读。局域 Echo 成功不等于全部 TCP、卸载、TUN/代理、有线或计费口径都通过。

在应用中导出会同时创建普通聚合 CSV 和 `pulsetrace-ip-observation-*.csv`。IP 观测文件包含 source、basis、quality、拒收/丢失与 reference_validated，不能忽略这些字段把观测子集当作完整精确计量。
