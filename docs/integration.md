# SQLite、查询 API 与 Web 的后续集成契约

> 历史设计稿。当前桌面版已实现原生消息桥与 SQLite schema v2，实际结构见 src/database.cpp；不启动此稿规划的监测 HTTP API。当前状态见 README、development-status.md 和 desktop-validation-results.md。

状态：**设计，未实现、未启动 HTTP 服务**。先完成真实设备准确性与性能验收；之后对接现有项目代码，保留 KeyboardMonitor / NetworkMonitor 各自的生命周期、数据表和接口。当前目录没有这些模块，不能宣称已经验证集成兼容性。

## 批量持久化

1. 输入处理函数只更新 MouseMonitor 内存分钟桶。InputHost 的低频快照任务复制已变化的聚合桶到预分配有界缓冲，不包含设备身份或事件序列。
2. DatabaseManager 独占写连接，每 30 秒或达到聚合批次阈值，使用预编译语句和单个事务写入；关闭时完成最终快照与 flush。
3. 数据目录固定为 exe/data。使用 WAL、明确的同步策略、busy timeout、迁移事务和启动完整性检查。不可写、磁盘满、数据库损坏必须返回可见状态，不在其他目录落盘。
4. 每次启动创建随机会话 ID。每个分钟/来源/按键存储该会话的**累计快照**，不是增量；快照带严格递增 revision，重试相同批次不会重复计数，迟到快照不能覆盖较新快照。
5. 写失败保留未确认快照并退避重试；按键事件线程不等待数据库。达到有界积压上限时明确暂停受影响统计并标出覆盖缺口，不静默丢弃、假称完整或无限增长内存。
6. 正常条件下异常退出可能丢失最近一次已提交事务之后的统计，目标上界约为 30 秒加排队时间；写失败时该窗口可能更长，必须显示最后提交时间。不能承诺只要采用 WAL 就能保存尚未写入的数据。

以下是新增独立表的设计示意，不修改既有表，也不作为本阶段的数据库迁移执行：

```sql
CREATE TABLE mouse_button_minute (
  session_id TEXT NOT NULL,
  utc_minute INTEGER NOT NULL CHECK(utc_minute >= 0),
  source INTEGER NOT NULL CHECK(source BETWEEN 0 AND 4),
  button INTEGER NOT NULL CHECK(button BETWEEN 0 AND 4),
  clicks INTEGER NOT NULL CHECK(clicks >= 0),
  temporal_double_clicks INTEGER NOT NULL CHECK(temporal_double_clicks >= 0),
  revision INTEGER NOT NULL CHECK(revision >= 0),
  PRIMARY KEY(session_id, utc_minute, source, button)
) WITHOUT ROWID;
CREATE INDEX mouse_button_range ON mouse_button_minute(utc_minute, source, button);

CREATE TABLE mouse_wheel_minute (
  session_id TEXT NOT NULL,
  utc_minute INTEGER NOT NULL CHECK(utc_minute >= 0),
  source INTEGER NOT NULL CHECK(source BETWEEN 0 AND 4),
  up_events INTEGER NOT NULL CHECK(up_events >= 0),
  down_events INTEGER NOT NULL CHECK(down_events >= 0),
  up_delta INTEGER NOT NULL CHECK(up_delta >= 0),
  down_delta INTEGER NOT NULL CHECK(down_delta >= 0),
  horizontal_events INTEGER NOT NULL CHECK(horizontal_events >= 0),
  horizontal_delta INTEGER NOT NULL,
  revision INTEGER NOT NULL CHECK(revision >= 0),
  PRIMARY KEY(session_id, utc_minute, source)
) WITHOUT ROWID;
CREATE INDEX mouse_wheel_range ON mouse_wheel_minute(utc_minute, source);
```

例如 button UPSERT 使用 `SET clicks=excluded.clicks, temporal_double_clicks=excluded.temporal_double_clicks, revision=excluded.revision WHERE excluded.revision > mouse_button_minute.revision`，不使用无去重凭据的 `clicks = clicks + excluded.clicks`。同一快照的多个表在同一事务内提交。SQLite 有符号 64 位整数需在绑定前检查范围。

需另建采集覆盖区间与模块状态元数据，区分“采集中且零事件”和“暂停/失败/未覆盖”。元数据只描述模块生命周期，不记录逐事件时间。按时间批量清理历史、日志有界、在线备份和 CSV 导出由数据库模块统一处理，不混入 MouseMonitor。

## 查询语义

计划的读接口：`GET /api/mouse/stats?from=...&to=...&bucket=minute|hour|day&source=all|...`。

- `from` / `to` 接受明确时区的时间，转换为 UTC；使用 `[from,to)`。
- 基础数据只有整分钟，非分钟对齐请求应返回 400 与原因，或者由 UI 在提交前明确提示对齐后的实际范围；禁止默默声称秒级精确。
- 自定义范围内先过滤分钟，再按小时/天汇总；本地日历天需按指定时区转换，返回实际时区和桶边界。
- 返回五键点击、时间双击、上下滚轮事件与累计 delta、水平滚轮、频率趋势、按键分布、来源证据与覆盖状态。
- 频率 = 点击总数 / 明确的覆盖分钟数；暂停、缺失区间不充当零值，部分边界桶标记为部分覆盖。配对不一定全部发生在查询范围内：双击归属第二个 DOWN。
- 返回已持久化数据和内存新鲜度；混合读取时采用会话/桶/revision 协议消除数据库与未写快照重复。
- 大范围采用服务端聚合、行数与范围上限，支持分页 CSV，不返回原始事件。

## Web 统计页

在现有导航中新增“鼠标”页，主题沿用现有深色/浅色设置，无 CDN、外部脚本或模拟正式数据。

| 区域 | 展示内容与规则 |
| --- | --- |
| 范围筛选 | 最近小时、今天、最近七天、自定义；分钟/小时/天分辨率；来源证据筛选 |
| 汇总卡片 | 点击总量、时间双击、覆盖时间、平均点击/分钟、上/下滚轮事件数及累计量 |
| 趋势图 | 点击/分钟或各时间桶点击数；缺失区间断线；刻度标注实际时区 |
| 按键分布 | 左、右、中、Mouse4、Mouse5 的数量和占比；空数据解释状态，不绘制虚构热度 |
| 来源说明 | “有设备句柄”“无设备句柄”“未知”等证据名称，不标成已确认物理设备 |
| 指标帮助 | 时间双击非系统空间双击；滚动事件数非刻度数；120 delta/标准刻度；精度一分钟 |
| 运行状态 | 鼠标、键盘、网络分别展示启停/失败/未覆盖与最后持久化时刻 |

HTTP 仅监听 127.0.0.1:8765；校验 Host 和 Origin，不开放跨域读；变更 API 使用本地会话令牌、防 CSRF 与明确方法约束。启动/暂停鼠标统计不隐式启停键盘或网络；未知输入来源不会被 UI 伪装成可靠分类。

## 集成阶段验收

- 硬件采集测试通过，采集口径由测试记录固定；接入现有 KeyboardMonitor 后复跑其回归测试。
- 事务重试、重复/乱序快照、崩溃恢复、磁盘满、目录只读、忙锁、保留期与时区边界均有独立测试。
- API 汇总与数据库分钟桶一致，UI 频率、分布、空数据和覆盖缺口与 API 一致。
- MouseMonitor 关闭时原键盘、网络行为和吞吐量不变；分别启停、共享输入线程退出与并发压力测试通过。
