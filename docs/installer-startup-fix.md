# 安装器启动时 app 常量未初始化（2026-11-05）

## 根因与复现

直接启动旧 RC2 安装器，不传 `/DIR`，在欢迎页出现之前报错 `Runtime error (at 50:91)`。诊断副本仅增加日志，得到 `TRACE ShouldSkipPage PageID=1 before app expansion`，随后立即出现同一内部错误。调用位置为 `ShouldSkipPage` 内无条件执行的 `GetIniString(..., ExpandConstant('{app}\installed.ini'))`；检查欢迎页时安装目录尚未初始化。

此前自动安装测试均提供 `/DIR` 并使用静默模式，路径提前可用，掩盖了直接双击启动的错误。现新增不提供 `/DIR` 的真实向导回归，覆盖中文和英语。原错误截图、诊断副本和日志仅保存在 `build/installer-app-constant`。

## 生命周期修复

- 默认路径仍由独立 `DefaultInstallPath` 计算：D 盘存在时使用 `D:\Program Files\PulseTrace`，否则回退当前用户的 Programs 目录；不读取尚不存在的向导或 `{app}`。
- 向导创建全部控件后设置 `WizardReady`。向导期间由 `SelectedInstallPath` 调用 `WizardDirValue()`；数据路径统一为 `DataDirectoryFor(安装路径)`，没有第二个数据目录选择页。
- `ShouldSkipPage` 遇到欢迎页等无关页面直接返回，不读安装标记、不展开应用目录。旧数据检测只服务于导入页面，并在返回重选目录后重新计算自动来源；保留用户明确指定的便携导入来源。
- `PrepareToInstall` 保存最终目录 `PreparedApp`，后续安装步骤使用同一快照。若升级解锁程序目录，另存 `UnlockedApp`；取消/失败清理及准备重试只恢复该目录，不读取向导或展开 `{app}`。重新准备时清除过期备份路径。
- 卸载的 `{app}` 来自已安装的卸载记录，可在 `InitializeUninstall` 使用；保留原有安全退出和历史数据保留逻辑。没有改动 SQLite、采集算法或数据库迁移实现。

官方依据：[WizardDirValue](https://jrsoftware.org/ishelp/topic_isxfunc_wizarddirvalue.htm) 与 [事件生命周期](https://jrsoftware.org/ishelp/topic_scriptevents.htm)。特别是 `DeinitializeSetup` 即使在安装前取消也会调用。

## 实际 Windows 验证

当前 Windows 11、普通账户、125% DPI。Computer Use 本机管道不可连接，重试与重置均失败；使用项目原生 Win32 探针操作其自行启动的安装器窗口，通过实际控件和窗口截图验证，未声称使用人工鼠标完成验收。

| 验证项 | 实际结果 |
| --- | --- |
| 旧安装器直接启动 | 复现相同 `50:91` 错误；诊断确认欢迎页的 `ShouldSkipPage` 调用 |
| 修复版中英文欢迎页 | 正常显示；直接取消正常退出，无清理异常 |
| 默认目录、返回、重选 | 两种语言均通过；默认 D 路径正确，data 预览跟随重选目录 |
| 就绪页取消 | 两种语言均正常退出，未生成监测数据库 |
| 自定义路径实际安装 | 两种语言通过，data 可写、程序 EXE 对普通用户只读 |
| 安装后启动及 SQLite | 安装程序内 WebView 自检成功；测试库升级后历史与月度数据校验通过 |
| 运行中覆盖升级 | 安全等待旧测试实例退出；配置、历史与归档保留 |
| 卸载及重装 | 两种语言均验证数据哈希不变，重装恢复原历史 |
| RC1 外部数据迁移 | 空目标、非空目标保护均通过，来源保留，中英偏好和更新源保留 |
| 默认 D 目录实际安装 | **未完成**；当前账户无写权限，实际向导提前显示本地化权限提示并可取消；未自动提升或修改父目录权限 |

本次均使用独立生成的测试数据库；没有以用户真实数据库执行迁移、删除或卸载实验。中英文向导截图在 `build/installer-wizard-c8846fbc047b49eeb37b60c61dadce53`；默认路径拒绝截图在 `build/installer-app-constant/default-permission`。安装与升级记录分别在 `build/installer-validation-3c5ed9ac6c6a4a35a065b74eaa2a1e96` 和 `build/installer-validation-9a1dfe95d549440eb27b272ec99f354a`，旧版迁移记录在 `build/legacy-layout-542b957eaa144e63b1b02ba2da348ee2`、`build/legacy-layout-d0d2575e7eca4e74aea07c5ee8902f6c`。

Release / Debug 构建及各自 **13/13 CTest** 全部通过；本轮修改的 PowerShell 脚本语法检查通过。安装器打包的四个运行时二进制 SHA-256 与当前 Release 构建一致。测试结束后，测试安装的卸载注册项已移除，测试数据和日志保留于 build，不提交仓库。

## 构建与复测

```powershell
.\tools\build.ps1 -Toolchain PortableLLVM -Configuration Release
.\tools\build-installer.ps1 -OutputDirectory build/release/installer-startup-fix
$setup = 'build/release/installer-startup-fix/PulseTrace-Setup-v0.3.0-rc2-x64.exe'
.\tools\test-installer-wizard.ps1 -InstallerPath $setup
.\tools\test-installer.ps1 -InstallerPath $setup -Language english
.\tools\test-installer.ps1 -InstallerPath $setup -Language chinesesimp
.\tools\test-legacy-layout.ps1 -InstallerPath $setup
```

隔离测试在检测到已有正式 Inno 注册安装时拒绝替换其注册信息。正式发布准备流程已加入无 `/DIR` 的向导测试，避免只靠静默测试放行。修复包保持项目版本 `0.3.0-rc2`，单独输出，原包及复现证据保留；本次不创建远程 Release。

新安装包：`build/release/installer-startup-fix/PulseTrace-Setup-v0.3.0-rc2-x64.exe`，旁边附 `.sha256`。SHA-256：`58ba68681468e246e3ea7a552bd49a784a442de638d9ef7954865517604e9479`。

待人工验收：经用户明确提升权限后安装默认 D 目录、D 盘不存在的真实系统、其他 Windows/DPI，以及不同凭据提升的账户组合。未建立可信发布签名，安装包仍适用既有候选版限制。
