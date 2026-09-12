# Timer（时钟与事件序号）

`PeripheralCapturer/Input/Timer.h`（及 `.cpp`）。给包和事件提供 **单调微秒时间**、**全局 sequence**、以及把时间映射成 `frameIndex`。

配套：[InputEvent](InputEvent.md)、[输入队列](InputQueue.md)、[架构设计](ArchitectureDesign.md)。

---

## 为什么不用系统日历时钟

录制要的是「过了多久、谁先谁后」，不是「今天几号」。

| API | 问题 |
| --- | --- |
| `SYSTEMTIME` / 文件时间 | NTP 校时会跳，时间轴会裂 |
| `GetTickCount` | 分辨率约 10–16ms，1000Hz 鼠标糊成一块 |

Windows **QueryPerformanceCounter（QPC）** 是内核封装的高精度、单调递增节拍（现代机上多半是不变 TSC）。`QueryPerformanceFrequency` 给出一秒多少 tick。换算：

```text
微秒 = (当前 QPC - 起点 QPC) * 1_000_000 / 频率
```

频率开机后基本不变，只在 `init()` 问一次。不要每条事件都 `QueryPerformanceFrequency`。QPC 不是 UTC，跨进程不要假设零点相同。

---

## 命名空间里的状态

均在 `namespace Timer`。设计上应是 **全进程一份**（定义放在 cpp）。若放在头文件 `static`，每个编译单元各有一份，`main` 里 `init()` 不会初始化处理线程那个 TU 的频率——实现时应把定义挪到 `Timer.cpp`。

| 符号 | 作用 |
| --- | --- |
| `global_sequence` | `atomic<uint64_t>`，从 0 起。每做出一条 **InputEvent** `fetch_add(1)` 写入 `e.sequence`。系统包不占号。只应在处理线程加（当前用 atomic 是为了以后多线程发事件也安全）。 |
| `global_qpcFreq` | `LARGE_INTEGER`，tick/秒。`init` 时 `QueryPerformanceFrequency`。换算用 `.QuadPart`。 |
| `global_startCount` | 时间原点。`init` 时打一次 `QueryPerformanceCounter`。之后所有 `nowUs()` 相对它。 |

**起点何时打：** 预览时间轴要连续可以用进程启动；**一场录像更干净的是开录时重打**，让这场从 0 附近开始。当前 `init()` 在 `main` 启动时调用一次，尚未按「开录重置」。开录重置后，未结束的预览与本场的零点会不同，这是预期。

---

## 函数

### `init()`

写频率和起点。必须在任何 `nowUs()` / `MakeBaseEvent()` 之前。失败（频率 0）应视为致命，否则后面除零。

### `nowUs()`

读当前 QPC，减起点，换成 `int64_t` 微秒。乘法用 `1000000LL` 避免 32 位溢出。

**谁调用：**

- WndProc 填 `RawInputPacket.timestampUs`（必须在入队前）；
- 不要在处理线程弹出包后再 `nowUs()` 当事件时间——应 **沿用包上的时间戳**。
- XInput 轮询没有包，可在采样当下 `nowUs()`。

### `ToFrameIndex(timestampUs, fps)`

`kFrameUs = 1_000_000 / fps`，返回 `timestampUs / kFrameUs`（整数除）。`fps` 必须是 **本场冻结的 `sessions.fps`**，不是导出 fps，也不是后来改过的 `recording.defaultFps`。

`fps == 0` 调用方保证。`MakeBaseEvent` 用 `Timer::sessionFps()`（开录时 `setSessionFps`），不要写死 60。

### `MakeBaseEvent()`

只填三条：新 `sequence`、`timestampUs = nowUs()`、`frameIndex`。`deviceID` / `type` / `control` 由 Registry 与解析填写。

设计注意：从 Raw 包生成事件时，`timestampUs` 应用 **包上的值**，再 `ToFrameIndex(包时间, session.fps)`，而不是再采样一次 QPC。此函数适合 XInput/合成事件，或作为「先拿序号再覆盖时间」的起点。

序号用 `memory_order_relaxed` 即可：只要单调唯一，不依赖跨线程的额外同步（事件内容的可见性由队列的 mutex 保证）。

---

## 与抓取帧率

`frameIndex` 是事件上的标签。没有事件就没有可归并的变化。不要用 Timer 每 `kFrameUs` 往总线灌一帧空状态。Recorder 按帧号写 `frame_data` 时可以重复上一帧 blob。

浮层快照频率可以 ≤ `captureFps`。
