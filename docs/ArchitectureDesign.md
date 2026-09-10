# 架构设计

硬件输入录制与回放系统。**仅 Windows**，不做跨平台运行时，捕获层也不做通用后端。

形态对齐 Tauri：**C++ Native Core** 负责系统能力，**Qt WebView 浮层**负责渲染，两者用 **本机 WebSocket** 解耦。Raw Input / XInput 负责高精度采集，WebView 只消费降频快照。

配套文档：[数据库设计](DatabaseDesign.md)、[InputEvent](InputEvent.md)、[设备注册表](DeviceRegistry.md)、[时钟](Timer.md)、[输入队列与事件总线](InputQueue.md)、[POV 前端](PovFrontend.md)、[待办](TODO.md)、[README](../README.md)。

## 目标

在 Windows 上录制键盘、鼠标、摇杆、手柄、方向盘、脚踏板等输入。采集时保留全量 **InputEvent**（带微秒时间戳），再按**本场录制的抓取帧率**归到 `frameIndex`；浮层按同一套（或更低的）快照频率绘制。导出帧率单独可配，不改已写入的时间戳。

## 平台与捕获

本项目只服务 Windows 10/11 x64，编译器固定 MSVC。不要引入多后端抽象工厂。

| 输入 | Windows 方式 | 不做什么 |
| --- | --- | --- |
| 键盘 / 鼠标 | Raw Input + `WM_INPUT`，`RIDEV_INPUTSINK` | 不用 Qt `keyPressEvent` 当事实源；**不要** `RIDEV_NOLEGACY`（会掐掉 Qt 旧消息） |
| 通用 HID（摇杆、方向盘、踏板、非 Xbox 手柄） | Raw Input `RIM_TYPEHID` + `HidP_*` | 不接 SDL |
| Xbox / XInput 兼容手柄 | `XInputGetState` 轮询（约 250Hz） | 不要和 HID 重复记同一把手柄 |
| 热键 | Raw Input 状态机，或 `RegisterHotKey` | 不设计跨平台快捷键库 |

`key_codes.native_*` 只存 Windows 语义：VK、HID Usage Page/Usage、XInput 槽位。`hDevice` 只作运行期设备区分，不写入配置、不 `CloseHandle`。

Usage 注册建议同时覆盖 Desktop 页：`0x02` 鼠标、`0x06` 键盘、`0x04` 摇杆、`0x05` Game Pad、`0x08` 多轴控制器。顶层 Game Pad（页 `0x01` / Usage `0x05`）和 Game Controls 页（页 `0x05`）不是一回事。

## 技术栈

| 类别 | 选型 |
| --- | --- |
| 语言 | C++20 |
| GUI | Qt 6 Widgets（配置窗、录制库） |
| 浮层 | 无边框置顶透明窗口 + Qt WebView（**POV**，Windows 为 WebView2） |
| Native ↔ Web | 本机 WebSocket（控制面 RPC + 数据面快照）；QWebChannel 仅作备选 |
| 录制热路径 | 二进制 append-only log |
| 会话 / 码本 | SQLite |
| 用户配置 | JSON |
| 日志 | spdlog |
| 构建 | CMake + Ninja，MSVC 2022 |

## 存储边界

| 层 | 形态 | 用户怎么用 |
| --- | --- | --- |
| 按键编码 | SQLite `key_codes` | 预置 + 捕获占位 + **用户自行注册** |
| InputEvent 事实源 | 会话旁的二进制 log | 录制/回放/分析只认事件流 |
| 会话元数据 | SQLite | 录制库列表、标签、marker 备注 |
| 派生帧 / 快照 | 可选缓存或导出 | 不替代事件流 |
| Profile | JSON | 颜色、布局、导出 fps |
| App config | JSON | Profile 路径、热键、目录 |

原则：

1. **InputEvent 是唯一事实源。** `InputState`、脏标记、60fps 快照都是派生视图。只存帧末状态会丢掉一帧内的 down/up。
2. **素材与皮肤分离。** 回放/导出时再选 Profile。会话不强制 `profile_id`。
3. **录制库界面是产品。** 列表、检查、改名、标签、删除、导入导出备份走 UI。
4. **JSON 只管观感和导出。** 编码在库；改皮肤不能改「W 是什么键」。
5. **实时不要写 JSON。** 热路径 append 二进制，结束后再导出 JSON/CSV。

## 项目结构

保持 Visual Studio CMake 布局，不另起 `src/`。源码在子目录 `PeripheralCapturer/`。

```text
PeripheralCapturer/                 # 仓库根
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── docs/
│   ├── ArchitectureDesign.md
│   └── DatabaseDesign.md
├── PeripheralCapturer/
│   ├── main.cpp
│   ├── Layout/                     # 配置窗
│   ├── overlay/                    # POV
│   ├── capture/                    # 隐藏捕获窗
│   ├── web/                        # POV 前端
│   └── utils/                      # Logger
└── out/
```

构建输出在 `out/build/<preset>/`。

## 三扇窗口

进程里固定三扇原生窗，互不嵌套：

| 窗口 | 类 | 看得见？ | 职责 |
| --- | --- | --- | --- |
| **配置** | `MainWindow`（Qt Widgets） | 是 | 设备 / 码本 / Profile / 录制库 / 日志 |
| **POV** | `PovWindow`（`QWebView` + Vue） | 是（浮层） | 只画降频快照，不采集 |
| **捕获** | `HiddenCaptureWindow`（`HWND_MESSAGE`，0×0） | 否 | `WM_INPUT` 目标窗；只拷包入队 |

POV 不是配置窗的子控件，捕获窗也不是配置窗的 `winId()`。捕获窗用 `HWND_MESSAGE`，没有客户区、不进任务栏。

**配置窗**管码本 / Profile / 录制库。**POV** 开发期加载 Vite `http://127.0.0.1:5173`，发布期加载打包结果；和 Native 用本机 WebSocket，不用 QWebChannel。**捕获窗**以后 `RegisterRawInputDevices`，`RIDEV_INPUTSINK`，不要 `RIDEV_NOLEGACY`。

```text
HiddenRawInputWindow
  ↓ WM_INPUT（只拷包、打时间戳、入队）
RawInputPacketQueue
  ↓
InputProcessor          RawInputPacket / XInput 差分 → InputEvent
  ↓
InputEventBus
  ├── InputRecorder     全量二进制 log
  ├── FrameAggregator   按本场 captureFps 聚合成 OverlayInputSnapshot
  ├── LocalWebSocket    推快照、收命令
  └── DeviceRouter      XInput 优先，HID 侧过滤 IG_
  ↓
POV：Vue（Vite）
  开发期 http://127.0.0.1:5173
  发布期 打包后的 dist / qrc
  页面 ws://127.0.0.1:<port>?token=...
```

POV 窗口：`FramelessWindowHint | Tool | WindowStaysOnTopHint`，`WA_TranslucentBackground`。默认鼠标穿透（`WS_EX_LAYERED | WS_EX_TRANSPARENT`），热键在「可点 / 穿透」间切换，避免盖住配置窗也点不到。

WebView **不要**消费全量 InputEvent。JS 缓存最新快照，用 `requestAnimationFrame` 画。

WebSocket 只绑 `127.0.0.1`，带随机 token；控制面 request/response，数据面可丢旧帧。不要给前端任意文件/进程能力。

## 采集链路

### 为什么要隐藏窗口

Raw Input 靠 `WM_INPUT` 投递到 `hwndTarget`。后台录制用 message-only / 隐藏窗口，不是为了显示。该线程必须有自己的消息循环。可用 Qt `winId()`，但高频采集更宜独立 HWND，避免和 UI 绑死。

### Raw Input 线程只做轻活

```text
WM_INPUT → GetRawInputData → QPC 时间戳 → RawInputPacket 入队 → 立刻返回
```

禁止在 WndProc 里：JSON、写文件、复杂 HID 解析、业务逻辑、WebSocket send、逐条 spdlog。鼠标可能 1000Hz 以上。

HID 解析、和上一份状态比、生成 ButtonDown/AxisChanged，放到 **InputProcessingThread**。

### 抓取帧率与导出帧率

这是两个数，不要合成一个全局 `#define 60`。

| 名称 | 作用 | 怎么定 |
| --- | --- | --- |
| **抓取 / 对齐帧率** `sessions.fps` | 把 `timestampUs` 映射成 `frameIndex`，快照也按它 | **点开始录制时**从当前配置拷入并锁定。改配置只影响下一场 |
| **导出帧率** `export.fps` | 导出视频或重采样时间线时用 | Profile JSON 随时可改，只影响导出，不改 log |

事件仍然是「来了就记」，不是按抓取帧率去轮询硬件。抓取帧率只决定怎么切帧。

开录 = 把当前录制相关配置**拷一份钉死**到这一场，再算 `kFrameUs`。当场录制过程中改 Profile / UI，只影响**下一场**，不改本场的 `fps` 和快照频率。

```cpp
// start_recording 时
session.fps = currentProfile.recording.defaultFps;
session.recording_config_snapshot = serialize(currentProfile.recording); // 本场只读副本
const int captureFps = session.fps;
const int64_t kFrameUs = 1'000'000 / captureFps;
```

导出帧率仍是另一套：用**导出当下**的 `export.fps`（以及当时选的皮肤）。不要用本场冻结的抓取 fps 去覆盖导出配置，也不要用后来改过的 `recording.defaultFps` 去重算这场已经写好的 `frameIndex`。

XInput 轮询频率（例如 250Hz）是采集后端的事，和 `sessions.fps` 无关：轮询可以更快，再归到同一套 `frameIndex`。

浮层快照默认跟 `captureFps`；若 UI 吃力，可以另用更低的 overlay fps，那只是派生，不是第二份事实源。

### RawInputPacket 与 InputEvent

`RawInputPacket`：贴近系统（qpc、hDevice、rawType、原始 bytes），便于以后重解析。

`InputEvent`：业务事实（sequence、时间、frameIndex、deviceID、backend、deviceType、type、control、数值）。高频下时间戳可能相同，**先时间再 sequence** 排序。字段表见 [InputEvent](InputEvent.md)。

鼠标在 `InputEvent` 上只表达相对位移：`dx`/`dy` 永远是增量；`MOUSE_MOVE_ABSOLUTE` 在处理线程差分。键盘保留 VKey、ScanCode、extended，**不发连发 KeyDown**。HID 原始字节留在 `RawInputPacket`，事件只留解析结果。

### 设备路由

Xbox 类：路径含 `IG_` 的 HID 默认交给 XInput，Raw HID 忽略，避免 A 键记两遍。用户配置可覆盖 `preferred_backend`。长期身份用 VID/PID/路径，不用 `HANDLE`。

## 模块说明

**MessageBus / InputEventBus**：跨线程发 InputEvent。浮层不订阅全量鼠标 move。字段与接线见 [输入队列与事件总线](InputQueue.md)。

**DeviceRegistry / DeviceRouter**：运行期 `hDevice` → `mouse_1` 等会话内 ID；`IG_` HID 忽略。见 [设备注册表](DeviceRegistry.md)。

**Timer**：QPC 微秒、`sequence`、按本场 fps 算 `frameIndex`。见 [时钟](Timer.md)。

**Recorder**：append-only 二进制事件流。SQLite 存编码表（`key_codes` 的原生码映射、本场 `session_keys` / `session_axes` 下标）和会话行 / log 路径，用来查表解码，不逐条存事件。Marker、控制热键完整触发时不写入按键流。

**Storage**：码本用户可注册（见数据库文档）。不存 Profile。

**Config**：App config 与 Profile JSON，手感同旧项目。

**Playback**：重放事件流（或派生帧），套当前 Profile。

**Export**：当时选中的 Profile。一期 JSON/CSV；二期 overlay 视频。

**配置窗 / 码本 / 录制库**：仍是 Qt Widgets。布局加键只能选自码本。

## 数据流

### 录制

```text
硬件
  → Raw Input 线程：拷包 + 时间戳
  → 处理线程：解析、路由、查/登记 key_codes、生成 InputEvent
  → Recorder 二进制 log + 会话元数据入库
  → FrameAggregator 按 captureFps 出快照
  → WebSocket 推给浮层（可丢旧帧）
```

### 回放

录制库打开会话 → 读事件 log → 按时间/帧重建 → 当前 Profile 决定怎么画。

### 改配置

手改 JSON → 校验 → currentProfile → 可选写回 sourcePath → 下次快照按新皮肤画。不改已录事件。

## 线程

| 线程 | 做什么 |
| --- | --- |
| RawInputWindowThread | 隐藏窗口、注册设备、消息循环、入队 packet |
| InputProcessingThread | 解析 HID/键鼠、XInput 差分、标准化事件 |
| XInput 轮询 | 约 250Hz `XInputGetState`，可与处理线程合并 |
| RecorderWriterThread | 批量写二进制 log |
| NetworkThread | 本机 WebSocket |
| Qt UI Thread | 配置窗、录制库、浮层窗口；不采集、不落盘 |

热路径禁止逐事件写日志。

## JSON 配置

### Release 文件位置

```text
应用目录/
├─ app-config.json
├─ data.db
├─ recordings/              # 每会话一个二进制 log
├─ log/
└─ backups/
```

Profile 不预生成。Debug 用系统 AppData。

### app-config.json

`currentProfile.sourcePath`、热键、静默录制、`recording.defaultFps`、备份目录、语言主题、窗口几何、WebSocket 端口策略。不要再用 QSettings 当第二份真相。

### profile JSON

只放观感与导出。`overlay.rows[].id` 必须等于 `key_codes.key_id`。

`recording.defaultFps`：工作副本里的抓取默认值，随时能改。**只有点开始录制的那一刻**拷进 `sessions.fps` 并冻结 `recording_config_snapshot`。当场和事后改这个字段，下一场才生效。  
`export.fps`：导出时用当时的 Profile，不写进冻结的录制配置。

非法 JSON 只提示，不擅自覆盖用户文件。

## 构建与部署

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

依赖：Qt6 Core / Widgets / WebView / Sql、spdlog、hid、xinput。C++20。`windeployqt` 打包。

## 版本计划

| 版本 | 内容 |
| --- | --- |
| v0.1 | CMake、spdlog、配置窗空壳、码本表、录制库空壳 |
| v0.2 | 隐藏窗口 Raw Input：键盘鼠标 → InputEvent 入队（WndProc 不重活） |
| v0.3 | 二进制 recorder、按本场 fps 归帧、WebSocket 快照、浮层 WebView |
| v0.4 | HID 解析、XInput 路由、码本监听绑定 |
| v0.5 | 录制库检查器、回放、JSON/CSV 导出、穿透热键 |
| v1.0 | 多设备、安装包、可选 overlay 视频 |

## 附录：事件形状

字段与「何时发」见 [InputEvent](InputEvent.md)。

浮层快照不是事实源，只含按下的 key_id、鼠标 delta 合计、轴当前值。
