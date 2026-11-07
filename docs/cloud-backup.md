# 坚果云 WebDAV 云备份 / WebDAV backup

v0.7.0 的“设置 → 云备份”提供连接测试、立即备份、自动备份、云端列表及确认恢复。默认关闭，默认服务器 `https://dav.jianguoyun.com/dav/`，目录 `PulseTrace/Backups`，自动周期默认一天、最少 60 分钟；不会做多设备实时双向同步。

## 配置与加密

- 使用坚果云账号及**第三方应用密码**，不要填写网页登录密码。另设至少 12 字符的恢复口令；不同设备使用相同口令才能解密相应备份。口令丢失无法恢复，修改口令后旧备份仍需旧口令。
- 账号、应用密码及恢复口令以当前 Windows 用户的 DPAPI 加密文件保存，不进入普通配置、SQLite、日志或云归档。修改服务器/账号时必须重新提供应用密码，防止旧凭据发送到新服务器。
- 主界面及门户均禁用 WebView2 密码自动保存和通用自动填充；运行时不能提供这些保护时不允许输入/保存云凭据。恢复确认使用原生 Windows 对话框，不依赖已禁用的网页脚本对话框。
- 云归档强制使用 AES-256-GCM，随机 128 位盐、96 位 nonce、128 位认证标签，PBKDF2-SHA256 200,000 次派生 256 位密钥。没有未加密上传选项。传输仅允许 HTTPS/TLS 1.2 或更高版本，保留正常证书验证，拒绝自动重定向及 HTTP 降级。
- `.ptb` 为版本化的二进制归档，含版本、时间、文件名、大小及逐文件 SHA-256。单次上限 128 MiB、512 文件；不压缩，超限明确失败。已验证待恢复的解密快照暂存本地，受本地数据目录权限保护；本地现有 SQLite 本身并未因云加密而变为加密数据库。

## 备份范围与行为

通过核心工作队列先刷新已接收的聚合，再使用 SQLite Online Backup API 生成主库和月度库快照。快照期间不在采集线程执行网络操作；归档、加密及 WebDAV 均在独立工作线程执行。临时云快照在归档结束后清理，既有手工备份不受影响。

仅包含 `monitor.db`、月度统计库/JSON 和必要设置白名单。密码、WebView2 会话/Cookie、浏览缓存、图标缓存、日志、未知配置扩展和校园网登录规则不上传。恢复时保留当前未知配置扩展、凭据和校园网规则。

先保存本地加密归档，再逐级 `MKCOL`、以 `If-None-Match: *` 上传唯一名称，最后重新 `GET` 下载核验整个归档哈希。失败保留原数据库和本地加密归档；不覆盖/删除旧的云备份。备份记录最多保存最近 100 条元数据；云端列表最多显示最近 200 个匹配名称。

自动备份尝试时间跨重启保存，手动备份至少间隔一分钟。另持久化 30 分钟请求预算：每次 WebDAV 操作预留两次 HTTP 请求、总预算 500，给认证挑战留余量；目录最多八级。服务端认证失败、429、507、超时、断网、取消及上传后校验失败均报告失败，不循环重试。

## 恢复

1. 点击“读取云端备份”，选择文件并“下载并校验备份”。此步骤不改当前数据库。
2. 校验加密标签、逐文件哈希、允许的相对路径、SQLite `quick_check`、数据库 schema 兼容性及配置格式。
3. 显示备份版本/时间后，输入 `RESTORE` 并再次确认。可取消尚未执行的恢复。
4. 从托盘正常退出并重新启动。程序取得独占数据锁、尚未打开 SQLite 或启动采集时执行恢复；如果还有未恢复的待写聚合则拒绝覆盖，先让原实例完成恢复。
5. 自动创建 `backups/pre-cloud-restore-*`，保留原数据库、月度库、配置及一致性副本。多文件替换具有 DPAPI 保护的持久事务记录；失败回滚，进程中断后下次启动完成回滚或确认已提交状态。恢复后采集默认暂停，需手动启用。

不要删除恢复前安全备份。应用密码和恢复口令不会随统计归档迁移；新电脑需重新配置 WebDAV，并提供对应恢复口令。

等待恢复确认或待重启期间暂停自动备份；可以取消该任务。正常退出、重新配置或完成恢复时清理对应的未使用/已用校验暂存，不删除正常统计或既有备份。

## 验证与限制

合成 WebDAV transport 验证标准方法、认证/空间/频率错误、上传中断、下载校验失败、取消及数据库写入不被网络任务阻塞。真实核心队列/Online Backup 集成、DPAPI、AES-GCM 错口令/篡改、恢复确认、未来 schema 拒绝、路径穿越拒绝、未知配置保留、恢复中断回滚与请求预算均已测试。界面检查覆盖双语、双主题、八类导航及 800×600 DIP 缩放。

未提供真实坚果云账号，因此未声称官方服务端鉴权、上传、空间限制或异机恢复实测通过。没有长时间/大数据量网络及后台运行验收；上限、超时或配额触发后可查看失败记录和本地加密归档。

参考：[坚果云第三方应用授权与频率限制](https://help.jianguoyun.com/?p=2064)、[SQLite Online Backup API](https://sqlite.org/backup.html)、[Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)、[CNG 认证加密](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_authenticated_cipher_mode_info)、[WinHTTP 安全](https://learn.microsoft.com/en-us/windows/win32/winhttp/winhttp-security-considerations)。

## English

Cloud backup is off by default. Use the Jianguoyun third-party app password and a separate recovery passphrase of at least 12 characters. Credentials are protected with per-user Windows DPAPI. Archives require AES-256-GCM with PBKDF2-SHA256 (200,000 iterations), unique salt/nonce, file hashes and version metadata. Keep your recovery passphrase: there is no recovery without it.

Backups use SQLite Online Backup snapshots and a non-sensitive settings allowlist, exclude browser sessions, credentials and caches, and are uploaded using HTTPS WebDAV without redirects. A local encrypted archive is retained and the upload is downloaded again for integrity verification. Limits are 128 MiB/512 files per archive, an automatic interval of at least one hour, one manual backup per minute and a persistent 30-minute request budget. Network work runs outside capture/SQLite threads.

Restore downloads and validates first, then requires typing RESTORE and confirming. It runs on the next startup with the exclusive data lock before opening SQLite, preserves current data in a safety backup and uses a durable rollback journal. Capture remains paused after restoration. Existing local SQLite and temporary validation snapshots are not encrypted by this cloud archive feature.

Controlled tests passed; real Jianguoyun credentials were not available. Live provider behavior, cross-device recovery, long runs and large datasets still require external validation. This feature is backup/restore, not live two-way database synchronization.
