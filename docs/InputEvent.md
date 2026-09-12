# InputEvent

`PeripheralCapturer/Input/InputEvent.h`。一条事件 = **某一个控件（或设备插拔）的状态变了**，不是系统包原样转发。

配套：[架构设计](ArchitectureDesign.md)、[设备注册表](DeviceRegistry.md)、[时钟](Timer.md)、[输入队列](InputQueue.md)、[数据库设计](DatabaseDesign.md)。

---

## 在流水线里的位置

```text
RawInputPacket / XInput 状态
    → 处理线程差分（相对该设备上一份状态）
    → 无变化：不生成
    → 有变化：Timer 填 sequence / 时间 / frameIndex
              Registry 填 deviceID
              解析结果填 type / control / 数值
    → InputEventBus::publish
    → Recorder 归帧写入 frame_data；浮层等订总线做派生快照
```

不进 `InputEvent` 的：

- 键盘自动连发（布尔状态已是 Down）
- 轴/帽数值未变的重复 HID/XInput 报告
- `dx = dy = 0` 的鼠标包
- `MOUSE_MOVE_ABSOLUTE` 的绝对坐标（处理线程差分后再写入 `dx`/`dy`）
- HID 原始 report 字节（留在 `RawInputPacket`）
- 控制热键（开始/停止、穿透切换）完整触发时

排序：先 `timestampUs`，再 `sequence`（同微秒时）。

---

## `InputBackend`

采集后端，不是「这是不是手柄」。

| 值 | 含义 |
| --- | --- |
| `RawInput` | 键盘、鼠标、通用 HID（摇杆、盘、踏板、非 Xbox 垫） |
| `XInput` | `XInputGetState` 的 0..3 槽。Xbox 兼容垫走这里 |

同一物理 Xbox 垫不要两条后端都记。路径含 `IG_` 的 HID 在 Registry 里标 `likelyXInput`，处理线程丢掉 Raw 包。

---

## `InputDeviceType`（粗类型）

对齐 Raw Input 的 `dwType`，挂在每条事件和 `DeviceInfo.type` 上。

| 值 | 来源 |
| --- | --- |
| `Keyboard` | `RIM_TYPEKEYBOARD` |
| `Mouse` | `RIM_TYPEMOUSE` |
| `Hid` | `RIM_TYPEHID`（摇杆/盘/踏板都先是这个） |
| `Unknown` | 尚未判定 |

**不要**在事件枚举里再拆 Gamepad / Joystick / Wheel / Pedal。那是描述符解析后的细类，放 `DeviceInfo.hidKind`。Xbox 垫用 `backend == XInput` 区分，不必再加 `Gamepad`。

码本 `key_codes.kind` 仍可写手柄/踏板，那是编码条目，不是运行期设备枚举。

---

## `HidDeviceKind`

只出现在注册表 `DeviceInfo`，**不进每条 InputEvent**。解析 HID Usage 之后填写，给 UI 显示「这是方向盘」。`deviceID` 仍是 `hid_3`。

| 值 | 含义 |
| --- | --- |
| `Unspecified` | 未解析或不必分 |
| `Gamepad` / `Joystick` / `Wheel` / `Pedal` | 细类 |

---

## `InputEventType`

| 值 | 何时发 | 主要字段 |
| --- | --- | --- |
| `KeyDown` / `KeyUp` | 键盘从未按下→按下，或按下→松开。连发不发 Down | `control` + `vkey` + `scanCode` + `extended` |
| `MouseMove` | `(dx,dy) != (0,0)` | `dx` / `dy` |
| `MouseButtonDown` / `Up` | 鼠标键状态变 | `control` + `rawValue` 0/1 |
| `MouseWheel` | 本包有滚动量 | `control`（竖/横）+ `rawValue` |
| `ButtonDown` / `Up` | 手柄/HID 数字键 | `control` + `rawValue` |
| `AxisChanged` | 轴值相对上次超出死区/ε | `control` + `rawValue` + `normalizedValue` |
| `HatChanged` | 方向帽档位变了 | `control` + `rawValue` 为 `HatDirection` |
| `DeviceConnected` / `Disconnected` | 设备集合变了 | 主要靠 `deviceID`，`control` 可空 |

XInput 十字键是四个数字键（`Button*`），不是 `HatChanged`。Hat 是 HID POV。

---

## `HatDirection`

离散档，不是连续轴。写入 `HatChanged.rawValue`（转成 `int`）。中位是 `Center`。不要做摇杆那种 −1..1 归一化。HID 常见 0–7 为八向、超出范围为中位，映射进本枚举即可。

---

## `InputEvent` 字段

### 时间与顺序

| 字段 | 作用 |
| --- | --- |
| `sequence` | 全局单调序号，只给 **事件** 发（`Timer::global_sequence`）。系统包不占号。同微秒时用它排序、查漏号。 |
| `timestampUs` | 相对 Timer 起点的微秒。包上的时间在 WndProc 入队前打好，生成事件时应沿用包的时间，而不是再问一次 `nowUs()`（否则处理延迟会进时间轴）。 |
| `frameIndex` | `timestampUs / kFrameUs`。`kFrameUs` 来自本场冻结的 `sessions.fps`，不是全局 `#define 60`。归帧字段，不表示「到点必须写一行」。 |

### 设备

| 字段 | 作用 |
| --- | --- |
| `deviceID` | 会话内稳定名：`keyboard_1`、`mouse_2`、`hid_3`、`xinput_0`。不是 `HANDLE`。两台鼠标的左键靠这个分开。 |
| `backend` | 这条事件从哪条 API 来。 |
| `deviceType` | 粗类型，见上。 |

### 控件与数值

| 字段 | 作用 |
| --- | --- |
| `control` | 码本 `key_id` 或设备上的控件名（`A`、`LeftTrigger`、`Pointer`、`Hat`）。说的是 **哪个控件**，不是哪台设备。插拔事件可空。 |
| `rawValue` | 硬件/API 整数：数字键 0/1，轴为原始计数，帽为 `HatDirection`，滚轮为滚动量。不要拿它直接画进度条（扳机 255 和摇杆 32767 不可比）。 |
| `normalizedValue` | 映射后的可比区间：数字键 0/1，扳机 0..1，摇杆 −1..1。归一规则看码本 `range_min` / `range_max`。MouseMove 不用。 |
| `dx` / `dy` | **仅 MouseMove**，永远是相对位移。绝对报告在处理线程按设备 `lastAbs` 差分后再写入。消费者只做加法，不再判断 `MOUSE_MOVE_ABSOLUTE`。第一包绝对/重连无上一包：只更新缓存，不发 Move。归帧后同一 `frameIndex` 内对 `dx`/`dy` 求和 → 码本 analog 通道 `mouse-dx` / `mouse-dy`。 |
| `vkey` | Windows 逻辑键（布局之后），对应码本 `native_vk`。 |
| `scanCode` | 物理 MakeCode（键位）。 |
| `extended` | E0/E1 前缀，区分右 Ctrl、方向键与小键盘等。 |

没有 `repeat` 字段：连发不是状态变化，处理线程应丢掉。

---

## 键盘三层编码

一次物理按键：

- **ScanCode**：哪颗键帽；
- **extended**：与另一颗键撞码时的前缀；
- **VKey**：当前布局下 Windows 认为是哪个逻辑键。

码本一期绑 `native_vk` 即可；要稳定认物理键再绑 ScanCode。

---

## 与码本 / 帧

`control` 对齐 `key_codes.key_id`。digital 进 bitset，analog 进 float32。鼠标位移是帧增量，走 `mouse-dx` / `mouse-dy`。一帧内 down 又 up，检查器看到的是帧末状态。
