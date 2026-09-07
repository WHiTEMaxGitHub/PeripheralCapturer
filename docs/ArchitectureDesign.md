# 架构设计

硬件输入录制与回放系统。**仅 Windows**，不做跨平台运行时，捕获层也不做通用后端。录制素材放在 SQLite，通过录制库界面管理；布局与导出偏好用可手改的 JSON。手感对齐 Keyboard Display。

配套文档：[数据库设计](DatabaseDesign.md)、[README](../README.md)。

## 目标

在 Windows 上录制键盘、鼠标，以及手柄摇杆/扳机这类线性输入。数字通道记按下集合，线性通道记归一化连续值，按同一帧号对齐后入库、回放、导出。

## 平台与捕获

本项目只服务 Windows 10/11 x64，编译器固定 MSVC。不要引入 `macos.rs` / `unsupported` 那种多后端，也不要为「以后移植」包一层抽象工厂。

| 输入 | Windows 方式 | 不做什么 |
| --- | --- | --- |
| 键盘 | `RegisterRawInputDevices` + `WM_INPUT`（键盘） | 不用 CGEventTap、不用 evdev |
| 鼠标按钮 / 位移 | Raw Input 鼠标，必要时低级 mouse hook 补按钮 | 不追求各平台同一套 hook |
| 手柄数字键与轴 | XInput 和/或 Windows HID（`Raw Input` HID / `HidD_*`） | 不接 SDL 跨平台手柄层 |
| 热键 | 自己吃 Raw Input 状态机，或 `RegisterHotKey` | 不设计跨平台快捷键库 |

`key_codes.native_*` 只存 Windows 语义：`native_vk` 是虚拟键码，Usage Page/Usage 是 Windows HID 枚举，手柄轴也可以记 XInput 槽位。没有「其他平台码」列。

## 技术栈

| 类别 | 选型 |
| --- | --- |
| 语言 | C++20 |
| GUI | Qt 6.11.2 Widgets |
| 系统 | 仅 Windows 10/11 x64 |
| 编译 | MSVC 2022 |
| 构建 | CMake 3.30 + Ninja |
| 录制存储 | SQLite（`QSqlDatabase`） |
| 用户配置 | JSON（可直接编辑） |
| 浮层 | Qt WebEngine + WebChannel |
| 格式化 | ClangFormat |
| 版本控制 | Git |

## 存储边界

先定边界，再拆模块。

| 层 | 形态 | 用户怎么用 |
| --- | --- | --- |
| 按键编码 | SQLite `key_codes` | 预置 + 捕获占位 + **用户自行注册**，回放按 id 解码 |
| 录制素材 | SQLite | 只通过「录制库」界面管理 |
| 录制元数据 | 同一库附属表 | 界面里改显示名/说明/标签/标记备注 |
| Profile | 可分享的 JSON | 改颜色、布局、导出 fps |
| App config | 本地 JSON | 当前 Profile 路径、目录、热键 |

原则：

1. 「录了什么」和「怎么显示/导出」分开。回放或导出时再选 Profile，会话不强制绑定 `profile_id`。
2. 素材在库里，必须有完整交互界面。补齐浏览、检查、改名、标签、删除、导入/导出备份。
3. JSON 只改观感和导出参数：颜色、透明度、布局、导出 fps、轴死区显示。按键编码进数据库，且必须能表达非线性（开/关）和线性（连续值）。改皮肤不能改「W 是什么键」，也不能改「LT 的范围是 0..1」。
4. 帧格式版本写在会话上，用代码里的解码器演进。不做字节偏移 `field_mapping`。

## 项目结构

保持 Visual Studio 打开文件夹的 CMake 布局，不另起 `src/` 根目录。顶层 `CMakeLists.txt` 做全局配置并 `add_subdirectory`，可执行目标在子目录 `PeripheralCapturer/`。新代码继续加在这个子目录里，由该处的 `CMakeLists.txt` 列出源文件。模块只是逻辑分层，需要时在该目录下加子文件夹即可。

```text
PeripheralCapturer/                 # 仓库根 = CMake 工程根
├── CMakeLists.txt                  # 标准、Qt、find_package、add_subdirectory
├── CMakePresets.json
├── CMakeUserPresets.json
├── .clang-format
├── README.md
├── docs/
│   ├── ArchitectureDesign.md
│   └── DatabaseDesign.md
├── PeripheralCapturer/             # 可执行目标，源码都在这里
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── core/                       # 后续：Application, MessageBus
│   ├── capture/
│   ├── recorder/
│   ├── storage/
│   ├── config/
│   ├── playback/
│   ├── export/
│   ├── ui/
│   ├── bridge/
│   └── utils/
└── out/                            # VS / preset 构建输出，勿提交
```

构建输出在 `out/build/<preset>/`（例如 `x64-debug`），不要改成手写 `build/`。

## 模块依赖

```text
                    Application
                   /           \
          MessageBus <-------> UI（浮层 / 配置 / 录制库）
              ^                      ^
              |                      |
        InputCapture           Playback / Export
              \                      /
                     Recorder
                        |
                  Storage (SQLite)
                  编码 + 录制 + 元数据
```

Profile / AppConfig 由 config 读写 JSON，给 UI、Recorder、Playback、Export 用，不经过 SQLite。

### MessageBus

模块间发布-订阅，支持跨线程。热路径不逐条转发：捕获线程维护按下集合，按 UI 刷新或录制采样周期批量发 snapshot。

- `publish(topic, message)`
- `subscribe(topic, callback)`
- `unsubscribe(connection)`

主题：`input/state`（主路径）、`input/axis`、`input/pointer`、`system/recording`、`system/playback`、`system/export`。

### InputCapture

直接写 Win32：Raw Input 收键盘鼠标，XInput / Windows HID 收手柄。立刻归一化为稳定 `key_id`，在 `key_codes` 按 `value_kind` 解析或登记：

- `digital`：键盘、鼠标键、手柄面键 → 按下集合
- `analog`：摇杆轴、扳机、压感、鼠标位移 → 归一化到该码的 `range_min` / `range_max`

帧里数字通道存下标 bitset，线性通道存 float。设备序号是可选属性，不替代 `key_id`。捕获线程只做轻量接收和状态合并。

`startCapture` / `stopCapture` / `registerDevice` / `getDeviceList`

### Recorder

按 FPS 同时采样数字按下集合和线性轴向量。两套状态同一帧号。空闲（键未变且轴未变）可 RLE。同步 marker 单独记。控制热键完整触发时不写入按键流。

`startSession(name, fps)` / `stopSession` / `addStateSnapshot` / `addAxisSnapshot` / `addMarker` / `buildFrame`

### Storage

存按键编码、录制素材、录制库元数据。WAL，批量事务。不存颜色、布局、导出 fps。

`KeyCodeRepository` 必须支持用户注册，不只是读预置表：`list` / `create` / `update` / `remove` / `bindNative` / `findByNative`。会话 CRUD、按范围读帧、改元数据、备份导入导出。详见 [数据库设计](DatabaseDesign.md)。

### Config

读写用户 JSON，校验、填默认、版本迁移。内置模板思路对齐旧项目（default / left-keyboard / 68-keyboard）。

App config：当前 Profile 快照与 `sourcePath`、默认录制 fps、热键、静默录制、备份目录、语言主题、窗口几何。

Profile：浮层 `rows` / `style`、录制默认值、导出偏好。

手感：加载 / 另存为 / 覆盖写回 `sourcePath`。启动不预生成一堆 profile。`sourcePath` 不做「最近打开」历史。

### Playback

`loadSession` / `play` / `pause` / `stop` / `seekTo` / `setSpeed` / `applyProfile`（换皮肤，不改库）。推给浮层的是当前帧 `keyIds`。

### Export

`exportSession(sessionId, profile, outputPath, format)`

- 一期：备份二进制 / JSON 检查数据
- 二期：可选 overlay 视频

用当时选中的 Profile，不读会话上的过期绑定。

### UI

**浮层**：无边框、置顶、透明、鼠标穿透。WebView 按 `Profile.rows` + 当前 `keyIds` 渲染。

**配置窗**：概览 / 布局 / 外观 / 窗口 / 录制热键 / 设置。加载、另存、覆盖写回，可打开外部编辑器。布局编辑器的「加键」只能从码本已有 `key_id` 里选，没有则先去码本注册。

**码本**：数据库的另一块正式入口。列表筛选 builtin/capture/user、新增数字键或线性轴、监听绑定原生码、改标签和轴范围、认领捕获占位行、删除未被引用的自定义行。没有这页，库存一张写死对照表，和硬编码没区别。

**录制库**：列表、搜索筛选、检查器、改显示名/说明/标签/marker 备注、删除、导入导出备份、开录/回放/导出。禁止只丢一个 `QSqlTableModel`。

### WebBridge

JS 可调：`startRecording` / `stopRecording` / `loadSession(id)`。

C++ 推：`onInputState`、`onAxisState`、`onOverlayProfile`、`onFrameState`、`onSyncFeedback`。

## 数据流

### 录制

```text
硬件
  -> InputCapture 归一化 Key ID
  -> KeyCodeRepository 查/登记（digital 或 analog）
  -> 捕获线程合并按下集合 + 轴值
  -> 按 sessions.fps 采样（不是 JSON 的 export.fps）
  -> FrameSerializer（RLE）
  -> SQLite 批量事务
  -> 录制库列表刷新
```

### 回放

录制库选中会话 → Playback 读帧 → 当前 Profile 决定怎么画 → 浮层渲染 `keyIds`。

### 改配置

手改或界面编辑 JSON → 校验 → 内存 `currentProfile` → 可选写回 `sourcePath` → 浮层立刻应用外观与布局。

## 线程

| 线程 | 做什么 |
| --- | --- |
| 主线程 | UI、WebEngine、WebChannel、应用 JSON 结果 |
| 捕获线程 | Raw Input、状态合并 |
| 工作线程 | 序列化、库写入、解码、导出 |

订阅表加锁。数据库写入队列化 + 事务。跨线程用 `QueuedConnection`。热路径禁止逐事件写日志。

## 性能

- 捕获线程只收数据和改按下集合
- UI 用 `QTimer` 刷新，不跟输入频率绑死
- SQLite WAL，每 100–500 帧或固定时间片提交
- 帧状态 + RLE，避免空闲帧膨胀
- 回放 LRU 缓存窗口帧
- 日志只记开停录、导入导出、失败

## JSON 配置

### Release 文件位置

```text
应用目录/
├─ app-config.json
├─ data.db
├─ log/
└─ backups/
```

Profile 不预生成，用户「另存为」时自选路径。Debug 用系统 AppData，不在开发二进制旁写配置。

### app-config.json

本地状态，允许手改，但日常由程序维护：

- `currentProfile.sourcePath`（覆盖写回，可 `null`）
- `currentProfile.changed` 与 overlay / recording / export 快照
- `recording.hotkeys` / `silent` / `defaultFps`
- `library.backupDirectory`
- `ui.language` / `theme`
- 窗口几何（不要再用 QSettings 当第二份真相）

### profile JSON

只放观感与导出，不放按键编码。`overlay.rows[].id` 引用库里已有的 `key_id`，用来决定画哪颗键、什么颜色。

```json
{
  "version": 1,
  "name": "CS POV",
  "overlay": {
    "visible": true,
    "position": "bottom-right",
    "layout": { "unitPx": 54, "gapUnit": 0.15 },
    "style": { "scale": 1, "opacity": 0.92 },
    "rows": [
      [
        { "type": "key", "id": "w", "label": "W", "group": "movement", "widthUnit": 1 }
      ]
    ]
  },
  "recording": {
    "defaultFps": 60,
    "fpsOptions": [30, 60, 120],
    "filenameTemplate": "${start}-${end}"
  },
  "export": {
    "defaultFormat": "json",
    "fps": 60,
    "filenameTemplate": "${profileSlug}-${recordingName}"
  }
}
```

| 字段 | 含义 |
| --- | --- |
| `recording.defaultFps` | 建议新会话采样率，开录后写入 `sessions.fps` |
| `export.fps` | 导出重采样。改 JSON 不影响已入库素材 |
| `rows[].id` | 必须等于 `key_codes.key_id`，否则浮层不画该键 |

非法 JSON 或缺字段：提示错误，用内置默认值恢复可运行状态，不覆盖用户文件，除非用户确认。

## 构建与部署

```bat
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

依赖：Qt6 Core / Widgets / WebEngine / WebChannel / Sql。标准 C++20。部署用 `windeployqt`，可选安装包。

## 版本计划

| 版本 | 内容 |
| --- | --- |
| v0.1 | CMake C++20、JSON 读写校验、建表、配置窗 + 录制库空壳 + 码本注册页 |
| v0.2 | Raw Input + Key ID 归一化（数字通道先通） |
| v0.3 | 采样、RLE、批量写入、热键、marker、录制库改元数据 |
| v0.4 | 解码与时间轴、浮层按 Profile 渲染、检查器 |
| v0.5 | 手柄线性轴/扳机入库与回放；会话备份；JSON/CSV；静默录制 |
| v1.0 | 多手柄、安装包、可选 overlay 视频 |

## 附录

### 运行时输入状态

```cpp
struct InputStateSnapshot {
    quint64 seq;
    qint64 tCapture;       // 毫秒
    QStringList keyIds;    // 当前按下的 digital key_id
};

struct AxisSample {
    uint8_t deviceId;
    QString keyId;         // 与 key_codes.key_id 相同，如 pad-lt、pad-lx
    float value;           // 已按码本范围归一化
};
```

### 消息主题

| 主题 | 载荷 |
| --- | --- |
| `input/state` | `InputStateSnapshot` |
| `input/axis` | `AxisSample` |
| `system/recording/start` | `SessionInfo` |
| `system/recording/stop` | `SessionInfo` |
| `system/playback/frame` | `FrameState` |
| `system/export/progress` | `int` |
