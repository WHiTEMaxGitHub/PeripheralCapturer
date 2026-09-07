# 数据库设计

SQLite 存按键编码、录制素材、录制库元数据。颜色、布局、导出 fps 不进库，见 [架构设计 · JSON 配置](ArchitectureDesign.md)。

## 原则

1. **编码在库，且必须能由用户注册。** 码本同时覆盖非线性（按下/松开）和线性（手柄轴、扳机、压感等连续量）。预置只是种子；用户要能自己加码、绑原生控件、改范围和标签。没有这层，SQLite 只是一张写死的对照表，优势就没了。`key_id`、值类型、原生码、会话下标以数据库为准。Profile 只决定颜色和导出 fps。
2. **素材与皮肤分离。** 回放或导出时再选 Profile。会话没有必填 `profile_id`。
3. **录制库界面是产品。** 列表、搜索、检查、改名、标签、删除、导入导出备份都必须走 UI。禁止只暴露一张 SQL 表。
4. **帧存状态。** `state_blob` 按 `format_version` 用 C++ 解码器解释。不做 Profile `field_mapping` 字节偏移。
5. **元数据可改，帧少改。** 显示名、说明、标签、marker 备注可随时改。改 JSON 颜色或导出 fps 不改 `key_codes` 和已录帧。

## 表结构

### key_codes（全局码本）

本项目要录的不只是键盘鼠标那种非线性开关，还必须能录线性输入设备：手柄摇杆、扳机、方向盘、压感等连续量。码本从一开始就要能描述这两种通道，不能把轴当成「以后再加的特殊键」。

同一套 `key_id` 给浮层、检查器、帧数据用。区别在 `value_kind`：

| value_kind | 含义 | 每帧存什么 | 典型来源 |
| --- | --- | --- | --- |
| `digital` | 非线性，只有按下/松开 | `session_keys` + `state_blob` bitset | 键盘、鼠标键、手柄面键 |
| `analog` | 线性，连续归一化值 | `session_axes` + `axis_samples` | 摇杆轴、扳机、压感 |

`range_min` / `range_max` 是编码约定，不是皮肤。常见：扳机 `0..1`，摇杆轴 `-1..1`。死区、显示条颜色放 JSON。

码本有三条写入路径，后两条是数据库存在的理由：

| origin | 谁写入 | 典型用途 |
| --- | --- | --- |
| `builtin` | 首次建库种子 | WASD、常用鼠标键、通用手柄面键/轴 |
| `capture` | 捕获遇到未知控件 | 先能录下来，用户事后可认领、改 id、改范围 |
| `user` | 码本界面手动注册或监听绑定 | 自定义设备、冷门 HID、虚拟键、自定轴范围 |

用户必须能在界面里：新增一行、填 `key_id`、选 digital/analog、绑原生码（可「按一下/拧一下」监听）、改标签和线性范围、删除未被会话引用的自定义行。预置行不能删，不能改 `key_id` / `value_kind`；标签可以改。已被 `session_keys` / `session_axes` 引用的行：禁止改 `key_id`、`value_kind`、线性范围，以免旧素材解不开。

```sql
CREATE TABLE key_codes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    key_id TEXT NOT NULL UNIQUE,
    kind TEXT NOT NULL,
    value_kind TEXT NOT NULL,
    range_min REAL,
    range_max REAL,
    native_usage_page INTEGER,
    native_usage INTEGER,
    native_vk INTEGER,
    default_label TEXT NOT NULL,
    origin TEXT NOT NULL DEFAULT 'user',
    notes TEXT,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);
```

| 字段 | 说明 |
| --- | --- |
| `key_id` | 稳定字符串，全库唯一。用户自定义须校验：非空、小写、字母数字和 `-`。`Profile.rows[].id` 只能引用已存在的值 |
| `kind` | `keyboard` / `mouse` / `gamepad` / `other`。自定义冷门设备用 `other` |
| `value_kind` | `digital` 或 `analog`。手柄面键是 `gamepad` + `digital`，扳机是 `gamepad` + `analog` |
| `range_min` / `range_max` | 仅 `analog` 有意义。捕获按此归一化。用户注册轴时必填且 `min < max` |
| `native_*` | 仅 Windows 原始码，可空（尚未绑定）。`native_vk` 为 Win32 虚拟键；Usage Page/Usage 为 Windows HID。监听绑定时写入。同一原生码不应绑两条有效记录 |
| `default_label` | 检查器默认名；浮层显示名由 JSON 的 `label` 覆盖 |
| `origin` | `builtin` / `capture` / `user`。升级种子只插入缺失的 builtin，不覆盖 user/capture |
| `notes` | 用户备注，如「右侧踏板」 |

```sql
INSERT INTO key_codes
    (key_id, kind, value_kind, range_min, range_max, native_vk, default_label)
VALUES
    ('w', 'keyboard', 'digital', NULL, NULL, 0x57, 'W'),
    ('shift-left', 'keyboard', 'digital', NULL, NULL, 0xA0, 'Shift'),
    ('mouse-left', 'mouse', 'digital', NULL, NULL, 0x01, 'LMB'),
    ('pad-a', 'gamepad', 'digital', NULL, NULL, NULL, 'A'),
    ('pad-lt', 'gamepad', 'analog', 0, 1, NULL, 'LT'),
    ('pad-lx', 'gamepad', 'analog', -1, 1, NULL, 'Left X');
```

建库时插入 builtin 种子。捕获未知控件插入 `origin='capture'` 的临时行（`key_id` 可用 `unk-vk-xx` 这类可改名占位）。用户在码本页把它改成稳定 id，或事先注册好再录。码不进 Profile。

KeyCodeRepository 对 UI 暴露：`list` / `create` / `update` / `remove` / `bindNative` / `findByNative`。删除前检查会话引用。

### sessions

### sessions

一次录制一行，录制库列表的主表。

```sql
CREATE TABLE sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    display_name TEXT NOT NULL,
    start_time INTEGER NOT NULL,
    end_time INTEGER,
    fps INTEGER NOT NULL DEFAULT 60,
    total_frames INTEGER NOT NULL DEFAULT 0,
    format_version INTEGER NOT NULL DEFAULT 1,
    profile_name_snapshot TEXT,
    note TEXT,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);
```

| 字段 | 说明 |
| --- | --- |
| `display_name` | 列表显示名，界面可改 |
| `start_time` / `end_time` | Unix 毫秒；录制中 `end_time` 为 NULL |
| `fps` | 录制采样率。开录时写入，改 JSON 导出 fps 不影响它 |
| `total_frames` | 提交后回填 |
| `format_version` | 帧二进制版本，对应 C++ 解码器 |
| `profile_name_snapshot` | 可选，开录时的皮肤名，不是外键 |
| `note` | 说明，界面可改 |

不要：`codec_id` / `profile_id` 外键，不要和 JSON Profile 强制关联。

### frame_data

按压缩后的 run 存，不是每个显示帧一行。连续相同按键状态写成一条，用 `run_len` 表示持续帧数。

```sql
CREATE TABLE frame_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    start_frame INTEGER NOT NULL,
    run_len INTEGER NOT NULL,
    state_blob BLOB NOT NULL,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, start_frame)
);
```

定位第 N 帧：

```sql
SELECT start_frame, run_len, state_blob
FROM frame_data
WHERE session_id = ? AND start_frame <= ?
ORDER BY start_frame DESC
LIMIT 1;
```

再确认 `N < start_frame + run_len`。

若实现初期先按每帧一行落地，v0.3 必须换成 run，否则 120/240fps 空闲段会把库撑满。

### session_keys

该次录制用到的编码子集，以及它们在本会话 bitset 里的下标。下标从 0 连续，和 `state_blob` 对齐。

```sql
CREATE TABLE session_keys (
    session_id INTEGER NOT NULL,
    key_index INTEGER NOT NULL,
    key_code_id INTEGER NOT NULL,
    PRIMARY KEY (session_id, key_index),
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    FOREIGN KEY (key_code_id) REFERENCES key_codes(id)
);
```

只收录 `value_kind = digital` 的码。解码：`key_index` → `key_codes.key_id` → 当前 Profile.rows 找颜色和标签。

### session_axes / axis_samples（线性通道）

线性控件不进 bitset。会话里出现过的轴单独建下标，每帧记归一化值。这是需求，不是 v1 之后的附件。

```sql
CREATE TABLE session_axes (
    session_id INTEGER NOT NULL,
    axis_index INTEGER NOT NULL,
    key_code_id INTEGER NOT NULL,
    PRIMARY KEY (session_id, axis_index),
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    FOREIGN KEY (key_code_id) REFERENCES key_codes(id)
);

CREATE TABLE axis_samples (
    session_id INTEGER NOT NULL,
    start_frame INTEGER NOT NULL,
    run_len INTEGER NOT NULL,
    values_blob BLOB NOT NULL,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, start_frame)
);
```

`session_axes` 只收录 `value_kind = analog` 的码，`axis_index` 从 0 连续。`values_blob` 按该会话轴顺序存 `float32`（或后续版本约定）。连续相同向量可 RLE，写法对齐 `frame_data`。

鼠标相对位移（delta）也是连续量，但语义是「这一帧的增量」而不是绝对轴位，实现时可以走同一套 analog 通道（例如 `mouse-dx` / `mouse-dy`，范围按设备或另行约定），不要另起第三套编码。

### markers

同步点等帧级标记。控制热键本身不进按键流。

```sql
CREATE TABLE markers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    frame_index INTEGER NOT NULL,
    name TEXT NOT NULL,
    note TEXT NOT NULL DEFAULT '',
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    UNIQUE(session_id, frame_index, name)
);
```

`name` 录制时写入（如 `sync`）。`note` 事后在录制库编辑，对应旧项目 sidecar 的 markerNotes。

### tags / session_tags

```sql
CREATE TABLE tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    tag TEXT NOT NULL UNIQUE
);

CREATE TABLE session_tags (
    session_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,
    PRIMARY KEY (session_id, tag_id),
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
    FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
);
```

### 不建的表

| 旧设想 | 现在怎么做 |
| --- | --- |
| `codecs` | `sessions.format_version` + C++ 解码器 |
| `profiles` | JSON 文件（颜色、布局、导出 fps） |

## 录制库界面与表

这些界面是数据库的产品外壳，不是可选项。码本编辑页和录制库同级，都是库的用户入口。

| 界面动作 | 涉及表 |
| --- | --- |
| 打开码本 / 注册新键或轴 | `key_codes` INSERT `origin='user'` |
| 监听绑定原生码 | 捕获一次 → UPDATE `native_*` |
| 认领捕获占位行 | 改 `key_id` / 标签 / 范围，`origin` 可改为 `user`（仅未被引用时改 id） |
| 打开列表 | `sessions` + tags + markers 计数 |
| 搜索名称/说明 | `sessions.display_name` / `note` |
| 按标签筛选 | `session_tags` |
| 改显示名/说明 | `UPDATE sessions` |
| 改标签 | `session_tags` |
| 打开检查器 | `session_keys` / `session_axes` + `key_codes` + `frame_data` / `axis_samples` + `markers` |
| 改 marker 备注 | `markers.note` |
| 删除录制 | `DELETE FROM sessions`（级联） |
| 导出/导入备份 | 上述表读写到备份文件 |
| 回放/导出 | 读帧；Profile 来自当前 JSON |

## 帧二进制

v1（`format_version = 1`）两条通道，同一帧号对齐：

- **数字**：`state_blob` 为 bitset，长度 `ceil(session_keys 数量 / 8)`，小端 bit，表示哪些 `key_index` 按下。
- **线性**：`values_blob` 为按 `axis_index` 排列的 `float32`，已按 `key_codes.range_min/max` 归一化。

会话头信息在 `sessions` / `session_keys` / `session_axes`，不进每一行。blob 里不放原始 VK、HID 裸值、Profile 字段映射。

实现可以先打通数字通道，但表和码本必须一次建齐线性字段，避免以后把轴硬塞进 bitset。

## 备份文件

数据库不是文件系统，备份用来恢复「拖着走」的手感。一个会话一个文件，例如 `.pcrec`：

- Magic / version
- sessions 标量字段
- 用到的 `key_codes` 快照（含 `value_kind` / 范围 / `native_*`）
- `session_keys` / `session_axes`
- frame runs / axis runs
- markers（含 note）
- tags

导入时按 `key_id` 对齐本机 `key_codes`，没有则插入。备份不等于 Profile。换机器后用本地 JSON 打开同一份素材即可。

## 常用查询

录制库列表：

```sql
SELECT
    s.id,
    s.display_name,
    s.start_time,
    s.end_time,
    s.fps,
    s.total_frames,
    (SELECT COUNT(*) FROM markers m WHERE m.session_id = s.id) AS marker_count
FROM sessions s
ORDER BY s.start_time DESC;
```

按标签：

```sql
SELECT s.*
FROM sessions s
JOIN session_tags st ON st.session_id = s.id
JOIN tags t ON t.id = st.tag_id
WHERE t.tag = 'aim'
ORDER BY s.start_time DESC;
```

检查器标记：

```sql
SELECT frame_index, name, note
FROM markers
WHERE session_id = ?
ORDER BY frame_index;
```

删除一次录制：

```sql
DELETE FROM sessions WHERE id = ?;
```

## 写入约定

- 开录：`INSERT sessions`，`end_time` 为 NULL
- 进行中：内存攒 runs，批量 `INSERT frame_data`；可定期更新 `total_frames`
- 停录：写剩余 runs，更新 `end_time` / `total_frames` / `updated_at`
- 改名、改标签、改备注：只碰元数据表，禁止重写 `frame_data`
- WAL；工作线程排队写。失败回滚时，保留已提交前缀或整段丢弃，二选一并在 UI 提示

## 与 JSON 的关系

数据库管：`key_codes`（含线性范围）、`session_keys` / `session_axes`、`frame_data` / `axis_samples`、`markers`、`sessions.fps`。

JSON 管：`overlay.rows` / `style`、`export.fps` / 格式 / 文件名模板、热键与静默录制（app-config）。

库里可以记 `profile_name_snapshot`（纯文本，可空）。

回放/导出：

1. 从库取数字帧和线性帧（下标 → `key_codes.key_id`）
2. 用当前 Profile 决定画哪些键/轴、什么颜色、导出多少 fps
3. 素材有、布局没有：检查器可见，浮层不画
4. 布局有、素材没有：数字键 idle，线性轴按 0（或该码中性点，如摇杆 0）
5. 改 JSON 颜色或 `export.fps`：立刻影响预览/导出，不改库里的编码、范围和帧

## 扩展示例

开录：

```sql
INSERT INTO sessions (display_name, start_time, fps, format_version, profile_name_snapshot)
VALUES ('20260906-153000', 1757140000000, 120, 1, 'CS POV');
```

事后改元数据：

```sql
UPDATE sessions
SET display_name = 'Aim warmup', note = 'first run', updated_at = strftime('%s', 'now')
WHERE id = 1;

INSERT INTO tags (tag) VALUES ('aim') ON CONFLICT(tag) DO NOTHING;
INSERT INTO session_tags (session_id, tag_id)
SELECT 1, id FROM tags WHERE tag = 'aim';
```

新格式版本：只增加 `format_version = 2` 和 C++ `V2Decoder`。旧行继续用 `V1Decoder`。不必加 `codecs` 表。

## 取舍

- **库存录制**：事务、查询、标签方便；必须做录制库 UI + 备份导入导出。
- **配置用 JSON**：可手改、可分享；库表不再承担 Profile / codec 注册。
- **run + bitset**：空闲段不爆炸；随机访问仍是一条范围查询。

## C++ 类型

| SQLite | C++20 / Qt |
| --- | --- |
| INTEGER | `qint64` / `int` |
| REAL | `double` |
| TEXT | `QString` |
| BLOB | `QByteArray` |

## 文档版本

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| 1.0 | 2026-09-06 | 四表 + Profile 进库（已废弃） |
| 1.1 | 2026-09-06 | 库只存录制；Profile 改为 JSON；run 存储与备份 |
| 1.2 | 2026-09-06 | JSON 只管颜色/导出 fps；全局 `key_codes` |
| 1.3 | 2026-09-06 | 码本区分 digital/analog；线性轴为正式通道 |
| 1.4 | 2026-09-06 | 用户自定义注册；`origin`；码本界面为正式入口 |
