# 输入队列与事件总线

本文把 `PeripheralCapturer/Input/` 下五个队列相关文件讲透：每个字段、每个函数、以及它们在整条采集架构里的位置。配套：[架构设计](ArchitectureDesign.md)、[InputEvent](InputEvent.md)、[设备注册表](DeviceRegistry.md)、[时钟](Timer.md)、[数据库设计](DatabaseDesign.md)。

源文件：

| 文件                         | 角色                            |
| -------------------------- | ----------------------------- |
| `RawInputPacket.h`         | 捕获线程 → 处理线程的 **系统包**          |
| `BoundedQueue.h`           | 有界、线程安全的通用队列（包和事件都用）          |
| `InputEventBus.h` / `.cpp` | 处理线程把 **InputEvent** 扇出给多个订阅者 |
| `QueuePresets.h`           | 容量、满员策略、是否收 MouseMove 的现成配方   |

---

## 1. 在整条架构里站哪一层

采集链路里，状态变化写成 **InputEvent**；录制按 `frameIndex` 归并成帧写入 `frame_data`。

从硬件到磁盘 / 浮层是 **两级队列，不要合成一个**：

```text
┌─────────────────────────────────────────────────────────────────┐
│ 捕获线程（隐藏 HWND 的消息循环）                                  │
│   WM_INPUT → GetRawInputData → Timer::nowUs()                   │
│            → RawInputPacket → packets.tryPush()                 │
│   禁止：HID 解析、差分、写文件、WebSocket、逐条 spdlog             │
└──────────────────────────┬──────────────────────────────────────┘
                           │ BoundedQueue<RawInputPacket>
                           │ 满员：DropOldest；只 tryPush，绝不 wait
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│ 处理线程                                                         │
│   packets.pop()                                                 │
│   DeviceRegistry.getOrCreateRawDevice(hDevice)                  │
│   shouldIgnoreRawHid？是 → 丢弃（Xbox 垫走 XInput，避免记两遍）   │
│   与该设备上一份状态差分                                          │
│   无变化 → 不发事件（连发 KeyDown、未动的轴、dx=dy=0）             │
│   有变化 → Timer::MakeBaseEvent() 填 sequence/时间/frameIndex    │
│          → 填 deviceID / control / 数值                          │
│          → InputEventBus::publish(e)                            │
│   XInput 约 250Hz 轮询也在本线程（或同级线程）差分后 publish       │
└──────────────────────────┬──────────────────────────────────────┘
                           │ 总线：每个订阅者一条 BoundedQueue<InputEvent>
                           ▼
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
   录制线程            浮层聚合             码本「按一下绑定」
   Block，不丢         可丢旧的             临时订阅
   本轮不含 Move       默认不含 Move        不含 Move
   归帧写 frame_data   降频快照 → WS       听一个控件就退订
```

对照关系：

| 概念   | 对应代码                                   | 不是什么                          |
| ---- | -------------------------------------- | ----------------------------- |
| 系统包  | `RawInputPacket`                       | 还不是业务事件                       |
| 包队列  | `BoundedQueue<RawInputPacket>`         | 不要直接给录制/浮层                    |
| 业务事件 | `InputEvent`                           | 不是 USB 帧、不是轮询快照               |
| 事件总线 | `InputEventBus`                        | 不是网络、不是 Qt 信号、不是万能 MessageBus |
| 设备身份 | `DeviceRegistry`                       | 在 **publish 之前**，不是总线订阅者      |
| 后端选择 | `shouldIgnoreRawHid` / 日后 DeviceRouter | 同样在 publish 之前                |

**时钟只给事件打戳和算 `frameIndex`，不决定「这一帧有没有东西可写」。** 没有状态变化就没有 `InputEvent`。Recorder 用开录时刻为原点重算本场帧号（不要用事件上进程寿命的 `frameIndex`），再批量写入 `frame_data`。空闲帧可以重复上一帧 blob，不要往总线灌假事件。本轮不录 `MouseMove`；勾了鼠标时 analog 槽位仍占着，值保持 0。

**浮层不要订阅全量 InputEvent。** WebView 只吃降频快照。总线里 overlay 那条队列已经默认丢掉 `MouseMove`；即便如此，仍应再聚合成 snapshot 再走 WebSocket，而不是把事件 JSON 进 JS。

---

## 2. 为什么必须两级（包 vs 事件）

`WM_INPUT` 可能 1000Hz 以上。若在 WndProc 里解析 HID、查注册表、写盘：

- 消息循环被拖住，鼠标卡、Qt 窗也卡；
- 系统内核队列堆积，丢包不可控。

所以捕获线程只做 **拷贝 + 打时间戳 + tryPush**。解析和「这算不算一次状态变化」放处理线程。

事件总线再拆一层，是因为 **同一个 InputEvent 有多个消费者，节奏不同**：

- 录制必须一条不丢（含鼠标位移）；
- 浮层只要按键/轴，Move 会把队列打爆；
- 码本监听只存在几秒。

处理线程若直接 `recorder.write(); overlay.push();`，停录、关浮层、打开绑定都会改捕获循环。总线让「谁在听」和「怎么差分」解耦。

XInput 没有 `WM_INPUT`。`XInputGetState(0..3)` 的结果 **不要进 RawInputPacket 队列**（那是 Raw Input 专用形状）。在处理线程（或专用轮询线程）差分后，直接 `publish` 成 InputEvent。包队列与 XInput 轮询在处理侧汇合。

---

## 3. `RawInputPacket.h` — 包的形状

WndProc 与处理线程之间约定的内存布局。过了处理线程就不该再出现（除非以后做「原始 HID 重解析」调试，那也只留在包这一层或失败日志里）。

### 预处理宏

| 符号                    | 作用                                               |
| --------------------- | ------------------------------------------------ |
| `WIN32_LEAN_AND_MEAN` | 少包含 GDI/OLE 等，加快编译、减少宏污染。                        |
| `NOMINMAX`            | 禁止 Windows 的 `min`/`max` 宏，避免和 `std::min`、Qt 冲突。 |

必须在 `#include <Windows.h>` **之前** 定义。

### 字段

| 成员            | 类型                | 作用                                                                                                                                                             |
| ------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `timestampUs` | `int64_t`         | 相对 `Timer` 起点的微秒。必须在 **入队前** 用 QPC 算好。若等处理线程弹出再计时，队列等待会被算进「按键发生时刻」，回放会偏。                                                                                       |
| `hDevice`     | `HANDLE`          | 本次插入后内核给这台设备的运行期句柄。处理线程拿去 `DeviceRegistry` 换成 `mouse_1` 这类 `deviceID`。禁止 `CloseHandle`，禁止写入 JSON/SQLite。拔出后句柄数值可能被复用，注册表要删行。                                   |
| `dwType`      | `DWORD`           | `GetRawInputData` 里 `RAWINPUTHEADER.dwType`：`RIM_TYPEKEYBOARD` / `RIM_TYPEMOUSE` / `RIM_TYPEHID`。决定按哪套结构解释 `bytes`。与 `InputDeviceType` 粗类型对齐，但这里仍是 Windows 原值。 |
| `bytes`       | `vector<uint8_t>` | 整包 `RAWINPUT` 的字节拷贝（含头）。键盘是 `RAWKEYBOARD`，鼠标是 `RAWMOUSE`，HID 是 report。WndProc 不知道按钮/轴含义；处理线程按 `dwType` 解析。                                                     |

没有方法。它是可移动的值类型，`tryPush(std::move(pkt))` 避免再拷 `vector`。

`hDevice == nullptr` 有时出现在合成输入。处理侧应丢掉或记成未知设备，不要当正常键去关句柄。

---

## 4. `BoundedQueue.h` — 有界队列

`template<typename T>`：`T` 可以是 `RawInputPacket` 或 `InputEvent`。逻辑与元素无关。

多线程安全。常见 1 生产者 + 1 消费者。总线里则是「每个订阅者各有一条」，处理线程对每条队列都是生产者。

**不要按值搬迁。** 内部有 `std::mutex`，不可移动。做成成员变量或 `shared_ptr`。

### `QueueOverflow`（创建时定死）

| 枚举           | 满员时           | 用在哪                                    |
| ------------ | ------------- | -------------------------------------- |
| `Block`      | 生产者等到有空位      | **仅录制事件队列**。WndProc 禁用。                |
| `DropOldest` | 丢掉队头（最旧），写入新的 | **包队列**、浮层、绑定监听。保证捕获线程立刻返回，或界面总看到较新状态。 |
| `DropNewest` | 拒绝本条          | 几乎不用。                                  |

反压连锁（刻意如此）：

1. 磁盘慢 → 录制队列满 → 处理线程在 `push` 上阻塞；
2. 处理线程不 `pop` 包 → 包队列满 → `tryPush` 丢最老的 Raw 包；
3. **不要**在录制队列上丢 `KeyDown` 来「保护」捕获。系统包可以丢（HID 会整包重发）；KeyDown 丢了回放少一次按下。

### 构造函数

`BoundedQueue(capacity, overflow)`

| 参数         | 作用                             |
| ---------- | ------------------------------ |
| `capacity` | 最多存放条数。`0` 会被改成 `1`，避免无意义的空队列。 |
| `overflow` | 满员策略，存进 `overflow_`，之后不变。      |

### 公开方法

| 方法              | 阻塞？         | 作用                                                                                     |
| --------------- | ----------- | -------------------------------------------------------------------------------------- |
| `close()`       | 否           | 置 `closed_`，叫醒所有等在 `pop`/`push` 上的线程。之后 `push`/`tryPush` 失败；队列里剩余元素仍可取出。用于停录、退出。       |
| `closed()`      | 否           | 是否已关闭。                                                                                 |
| `size()`        | 否           | 当前条数（调试、状态栏）。热路径不必每包都问。                                                                |
| `dropped()`     | 否           | 因满员丢掉的累计次数（`DropOldest`/`DropNewest`）。`Block` 下应为 0。用来判断处理线程是否长期跟不上鼠标。                 |
| `tryPush(item)` | **否**       | 入队；满且策略为 Block 时返回 false。**WndProc 必须用这个。**                                            |
| `push(item)`    | 仅 Block 且满时 | 入队；录制用。                                                                                |
| `tryPop()`      | 否           | 空则 `nullopt`。浮层可「有就取」。                                                                 |
| `pop()`         | 空则等待        | 有数据或 `close` 后返回。处理线程等包、录制线程等事件的主循环：`while (auto x = q.pop())`。关闭且取尽 → `nullopt` 结束循环。 |

返回 `bool` 的入队：`true` 表示进队成功；`false` 表示已关闭、或 DropNewest 丢了本条、或 tryPush 遇上 Block 已满。

### 私有 `pushImpl(item, waitWhenFull)`

真正的入队。`tryPush` 传 `waitWhenFull=false`，`push` 传 `true`。

顺序：

1. 已关闭 → `false`；
2. 未满 → `push_back`，`notEmpty_.notify_one()`；
3. 已满 + `Block` + 不允许等 → `false`（保护 WndProc）；
4. 已满 + `Block` + 允许等 → 在 `notFull_` 上睡，直到有空位或关闭；
5. 已满 + `DropOldest` → `pop_front`，`dropped_++`，再写入；
6. 已满 + `DropNewest` → `dropped_++`，本条不写，返回 `false`。

出队后 `notFull_.notify_one()`，把卡在步骤 4 的录制 `push` 叫醒。

### 私有成员

| 成员          | 作用                                                                          |
| ----------- | --------------------------------------------------------------------------- |
| `capacity_` | 上限。有界才能在 1000Hz 下不把 RAM 吃光。                                                 |
| `overflow_` | 满员策略。                                                                       |
| `mutex_`    | 保护 `items_` / `closed_` / `dropped_`。`mutable` 是为了 `size()` 等 const 查询也能加锁。 |
| `notEmpty_` | 条件变量：「从空变为非空」时叫醒 `pop`。                                                     |
| `notFull_`  | 「从满变为有空位」时叫醒 Block 的 `push`。必须两个 CV：一个表示「有货」，一个表示「有坑」，合成一个会把错误的一方空叫醒。       |
| `items_`    | `deque<T>`。要从队头丢最旧、从队尾入新，双端队列合适。                                            |
| `closed_`   | 关闭旗标。                                                                       |
| `dropped_`  | 满员丢弃计数。                                                                     |

热路径不要 `spdlog`。要看丢包看 `dropped()`。

---

## 5. `InputEventBus` — 事件扇出

进程内、C++ 对象直送。热路径禁止 JSON、禁止为每条事件发 Qt 信号。

**不挂在总线上的：**

- `RawInputPacket`（只给处理线程）；
- `DeviceRegistry` / 路由（生成事件之前）；
- 开始/停止录制、改 Profile（控制面，Qt 或另一组命令）。

### `EventSubscribeOptions`

创建一条订阅队列时的配方。

| 字段                | 默认           | 作用                                                                                                     |
| ----------------- | ------------ | ------------------------------------------------------------------------------------------------------ |
| `name`            | 空            | 调试标签（`recorder` / `overlay` / `bind`）。运行不分支这个字符串。                                                      |
| `capacity`        | 4096         | 该订阅者专用队列长度。                                                                                            |
| `overflow`        | `DropOldest` | 该队列满员策略。录制必须改成 `Block`。                                                                                |
| `acceptMouseMove` | `true`       | `false` 时 `publish` 遇到 `InputEventType::MouseMove` 直接跳过。浮层、绑键、**本轮 Recorder** 都是 `false`。位移以后再开；1000Hz Move 进 Block 队列会把按键挤住。 |

### 公开方法

**`subscribe(options)`**

1. `make_shared<BoundedQueue<InputEvent>>(capacity, overflow)`；
2. 登记进 `subscriptions_`；
3. 把 **同一个** `shared_ptr` 返回给调用方。

总线之后 `push`/`tryPush`，订阅者只 `pop`/`tryPop`。不要在订阅方再 `push`。码本监听结束可以不再 pop；完整做法以后再加 `unsubscribe`（现在关进程靠 `close`）。

**`publish(const InputEvent& event)`**

处理线程在确认「这是一次状态变化」之后调用。对每个订阅者 **拷贝** 一份事件（`InputEvent` 含 `string`，事件频率远低于 Raw 包，可接受）。

实现要点：先在 `mutex_` 下把 `subscriptions_` 拷到 `snapshot`，**解锁后再入队**。若录制队列 `Block` 时仍握着总线锁，其它线程不能 `subscribe`，后面的订阅者也收不到本条。拷贝表之后，录制卡住最多拖处理线程自己，不锁死整条总线。

入队时：`overflow == Block` 走 `push`（可等），否则 `tryPush`。

**`close()`**

对每条订阅队列 `close()`，让各消费者的 `pop` 循环退出。

### 私有

| 成员                     | 作用                                |
| ---------------------- | --------------------------------- |
| `Subscription.options` | 仍要用来过滤 MouseMove、选择 push/tryPush。 |
| `Subscription.queue`   | 与订阅者共享的那条事件队列。                    |
| `mutex_`               | **只保护订阅表**（谁在听），不保护队列内部（队列自己有锁）。  |
| `subscriptions_`       | 当前听众列表。                           |

---

## 6. `QueuePresets.h` — 配方

避免各处手写 4096 却把包队列写成 Block。

| 符号                             | 含义                                                                                                                                                   |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `kRawPacketQueueCapacity`      | 包队列长度 **4096**。创建：`BoundedQueue<RawInputPacket> packets(kRawPacketQueueCapacity, QueueOverflow::DropOldest);` 策略必须是 DropOldest，预设里只定容量，防止有人抄成 Block。 |
| `recorderSubscribeOptions()`   | 名 `recorder`，容量 8192，**Block**，**不收** MouseMove。归帧后写 `frame_data`。                                                                                                |
| `overlaySubscribeOptions()`    | 容量 64，DropOldest，**不收** MouseMove。给快照聚合用；真正画 UI 还要再降频。                                                                                               |
| `bindListenSubscribeOptions()` | 容量 32，DropOldest，不收 Move。码本监听「下一个非移动事件」。                                                                                                             |

数字可调；**录制 Block、捕获 tryPush、浮层可丢** 三条不要改。

---

## 7. 和其它模块怎么接

### Timer

`timestampUs` / `sequence` / `frameIndex` 在处理线程生成 InputEvent 时填写（包上的时间用入队时的 QPC）。`MakeBaseEvent()` 不要在 WndProc 调：序号应只给 **事件** 发，系统包不占号。

### DeviceRegistry

`hDevice` → `deviceID`。同一 `HANDLE` 的所有键共用一个 id；`nextMouse_` 等是「第二台鼠标」发号，不是按键发号。`likelyXInput` / `shouldIgnoreRawHid` 在差分前丢掉 HID 重复垫。

差分缓存（键盘当前 Down 集合、鼠标 lastAbs、上一份 HID 轴）应挂在设备上，以后扩展 `DeviceInfo`，不要进队列结构。

### InputEvent

总线搬运的就是这个结构。一条 = 某一个 `control`（或插拔）的状态变了。连发、未变化的轴、`(dx,dy)=(0,0)` 根本不应 `publish`。

### 录制

订阅 recorder 队列，按 `frameIndex` 归并冻通道（`RecLayout` + `device_bits`），再 `FrameBatchWriter` 批量 INSERT。不要按 60Hz 定时往总线灌全状态。也不要逐事件 INSERT。

### 浮层

订阅 overlay 队列 → 聚合成 `OverlayInputSnapshot` → localhost WebSocket。JS 用 `requestAnimationFrame` 画缓存的快照。

---

## 8. 推荐接线（Recorder / WS 尚未进 main，采集 Pipeline 已接上）

```cpp
// 成员
BoundedQueue<RawInputPacket> packets{
    kRawPacketQueueCapacity, QueueOverflow::DropOldest};
InputEventBus bus;
DeviceRegistry registry;

// 启动订阅
auto logQ = bus.subscribe(recorderSubscribeOptions());
auto overlayQ = bus.subscribe(overlaySubscribeOptions());

// WndProc
RawInputPacket pkt;
pkt.timestampUs = Timer::nowUs();
pkt.hDevice = /* RAWINPUTHEADER */;
pkt.dwType = /* ... */;
pkt.bytes.assign(/* GetRawInputData 缓冲区 */);
packets.tryPush(std::move(pkt));

// 处理线程
while (auto pkt = packets.pop()) {
    if (registry.shouldIgnoreRawHid(pkt->hDevice)) continue;
    const auto id = registry.getOrCreateRawDevice(pkt->hDevice, /* 由 dwType 映射 */);
    // 解析 bytes，相对该设备上一份状态差分
    // if (!changed) continue;
    InputEvent e = Timer::MakeBaseEvent(pkt->timestampUs);
    e.deviceID = id;
    // ... 填 type / control / 数值
    bus.publish(e);
}

// 录制线程：按本场帧号归并冻通道（不含 MouseMove），攒一批再 appendFrames
while (auto e = logQ->pop()) {
    recorder.ingest(*e);
}
```

控制热键（开始/停止、穿透切换）在 **publish 之前** 拦掉，不要先进录制队列再删。

---

## 9. 常见误用

| 误用                                         | 后果              |
| ------------------------------------------ | --------------- |
| WndProc 里 `push` 而不是 `tryPush`             | 消息循环睡着，UI/鼠标假死  |
| 包队列用 `Block`                               | 同上              |
| 录制队列 `DropOldest`                          | 丢 KeyDown，回放缺按键 |
| overlay `acceptMouseMove = true` 且容量很小     | 全是 Move，按键被挤掉   |
| 把 `hDevice` 当 `deviceID` 写进库            | 拔插后对不上设备        |
| 在 WndProc 调 `GetRawInputDeviceInfo` / HidP | 捕获线程变重          |
| 处理线程 `publish` 里直接写文件、发 WebSocket          | 总线失去意义，卡差分      |
| 把 DeviceRouter 当总线订阅者                      | 重复事件已经生成了，过滤太晚  |
| 用 60Hz 定时器往总线灌「当前全状态」                      | 违背「事件 = 状态变化」   |

---

## 10. 和数据库 / 帧缓存的边界

队列和总线 **不写 SQLite**。Recorder 订阅者在自己的线程里攒帧再调 `appendFrames`。热路径（WndProc）禁止碰库。

`frame_data` 的 bitset / float 由总线上的事件按 `frameIndex` 归并。空闲帧不必每 16ms 往队列里塞假事件。
