# 数据库设计

DDL 以 [DatabaseSchema](DatabaseSchema.md) 为准（含表协作图）。颜色、布局、导出 fps 见 [架构设计](ArchitectureDesign.md)。事件字段见 [InputEvent](InputEvent.md)。

一份 `data.db`，和 exe 同目录。录制本体是 `frame_data`：一行一帧，`frame` + `blob`，内存攒批写入。

## 原则

1. **编码在库，用户可注册。** 码本同时描述数字键和线性轴。预置只是种子。
2. **采集出 InputEvent，落盘写帧。** Recorder 按 `frameIndex` 归并后写入 `frame_data`。一帧内 down 又 up，检查器看到的是帧末状态。
3. **素材与皮肤分离。** 回放/导出时再选 Profile。
4. **录制库界面是产品。** 列表、检查、改名、标签、删除、导入导出走 UI。
5. **元数据可改，帧少改。** 显示名、标签、marker 备注可改；改 JSON 颜色不影响 `frame_data`。

## 表怎么用

### `key_codes`

同一套 `key_id` 给浮层、检查器和帧解码。`value_kind`：

| value_kind | 含义 | 每帧存什么 | 典型来源 |
| --- | --- | --- | --- |
| `digital` | 按下/松开 | blob 前半 bitset | 键盘、鼠标键、手柄面键 |
| `analog` | 归一化连续值 | blob 后半 float32 | 摇杆、扳机、鼠标 dx/dy |

`range_min` / `range_max` 是编码约定（扳机 `0..1`，摇杆 `-1..1`）。死区和颜色放 JSON。

| origin | 谁写入 |
| --- | --- |
| `builtin` | 首次建库种子 |
| `capture` | 遇到未知控件时的占位行 |
| `user` | 码本页注册或监听绑定 |

预置行不能删，不能改 `key_id` / `value_kind`。码本页：新增、绑原生码、改标签和线性范围。码不进 Profile。

本场通道下标由 `RecLayout` + `sessions.device_bits` 计算，不按会话再抄一份码本。

### `sessions` / `frame_data`

开录写入 `fps`、`device_bits`。`device_bits`：bit0 键盘、bit1 鼠标、bit2 XInput。鼠标位移是 analog 通道 `mouse-dx` / `mouse-dy`（帧增量）。

Profile 不进库，会话没有 `profile_id`。

### `markers`

帧级标记。`name` 录制时写入；`note` 事后在录制库改。控制热键不进按键流。

### 标签

`sessions.tags`：逗号分隔文本。

## 录制库界面与表

| 界面动作 | 涉及 |
| --- | --- |
| 打开码本 / 注册 | `key_codes` INSERT `origin='user'` |
| 监听绑定原生码 | UPDATE `native_*` |
| 认领捕获占位行 | 改 `key_id` / 标签 / 范围 |
| 打开列表 | `sessions` + markers 计数 |
| 搜索 | `display_name` / `note` |
| 按标签筛选 | `sessions.tags` |
| 改名 / 说明 / 标签 | `UPDATE sessions` |
| 打开检查器 | `device_bits` + `RecLayout` + `frame_data` + `markers` |
| 改 marker 备注 | `markers.note` |
| 删除录制 | `DELETE FROM sessions`（级联） |
| 回放 / 导出 | 读帧；Profile 来自当前 JSON |

## 帧 blob

只录键盘 / 鼠标 / XInput。认一场用 `sessions.id`。

| 区 | 内容 | 宽度 |
| --- | --- | --- |
| 前半 | digital bitset，小端 bit | `ceil(N / 8)` |
| 后半 | analog `float32` | `4 * M` |

`N`、`M` 由 `RecLayout` 按 `device_bits` 算出。`FrameBatchWriter` 攒一批再一次事务写入。blob 里不放 VK / HID / Profile。

## 备份

一个会话一个 `.pcrec`：magic / version、`sessions` 标量、用到的 `key_codes` 快照、`frame_data`、markers。导入按 `key_id` 对齐本机码本，没有则插入。备份不是 Profile。

## 常用查询

```sql
SELECT id, display_name, start_time, end_time, fps, total_frames
FROM sessions
ORDER BY start_time DESC;

SELECT * FROM sessions
WHERE ',' || tags || ',' LIKE '%,aim,%';

SELECT frame, name, note FROM markers
WHERE session_id = ? ORDER BY frame;

DELETE FROM sessions WHERE id = ?;
```

## 写入

- 开录：`fps`、`device_bits`，`end_time` 为 NULL
- 进行中：批量 `INSERT frame_data`
- 停录：`end_time`、`total_frames`
- 改名 / 标签 / 备注：只改 `sessions` / `markers`
- WAL；工作线程排队写

## 与 JSON

库：`key_codes`、`sessions`、`frame_data`、`markers`。

JSON：`overlay`、`export`、热键、app-config。

回放：`device_bits` + `RecLayout` 还原 `key_id`，再用当前 Profile 决定怎么画。素材有、布局没有：检查器可见，浮层不画。布局有、素材没有：数字键 idle，轴为 0。

## C++ 类型

| SQLite | C++20 / Qt |
| --- | --- |
| INTEGER | `qint64` / `int` |
| REAL | `double` |
| TEXT | `QString` |
| BLOB | `QByteArray` |
