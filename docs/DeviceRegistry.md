# DeviceRegistry（设备注册表）

`PeripheralCapturer/Input/DeviceRegistry.h`（及 `.cpp`）。把 Windows 运行期 `HANDLE` / XInput 槽，换成这场会话里能写进 `InputEvent.deviceID` 的稳定名字，并记住这台设备是谁。

配套：[InputEvent](InputEvent.md)、[输入队列](InputQueue.md)、[架构设计](ArchitectureDesign.md)。

---

## 解决什么问题

`WM_INPUT` 每包带 `hDevice`。这只是 **本次插入后的内核句柄**：

- 拔掉再插，数值会变；
- 不能当配置、不能进码本、不能 `CloseHandle`；
- 业务层需要 `mouse_1` 这种会话内名字。

没有注册表，事件上只能写裸 `HANDLE`，两台鼠标和回放都会对不上。

它是处理线程上的 **字典**，不是消息总线订阅者。路由（要不要丢掉 `IG_` HID）在 **生成事件之前** 问它。

---

## 和 DeviceRouter / 码本的差别

| | Registry | Router（逻辑，可与 `shouldIgnoreRawHid` 合并） | 码本 `key_codes` |
| --- | --- | --- | --- |
| 认的是 | 哪一台设备 | 走 Raw 还是 XInput | 哪个控件（W、左扳机） |
| 键 | `HANDLE` / slot | 读 `likelyXInput` / `preferredBackend` | `key_id` |
| 写入 | 仅内存 | 决定丢包还是解析 | SQLite |

长期认人（VID/PID/路径）给 UI 看，可存在 `rawName` 里解析；不要把 `HANDLE` 写进 JSON。

---

## `DeviceInfo`（一台设备一行）

| 字段 | 作用 |
| --- | --- |
| `deviceID` | 写入事件的字符串：`keyboard_1`、`mouse_2`、`hid_3`。同一台设备上所有按键共用这一个。 |
| `rawName` | `GetRawInputDeviceInfoW(..., RIDI_DEVICENAME)` 的设备接口路径。用于显示和 `IG_` 判断。只在第一次见到该 `HANDLE` 时查询，不要每条 `WM_INPUT` 都问。 |
| `type` | 粗类型 `InputDeviceType`，与事件上 `deviceType` 一致。 |
| `hidKind` | HID 细类（摇杆/盘/踏板等）。解析描述符后填；未解析保持 `Unspecified`。不改变已经发出的 `deviceID`。 |
| `preferredBackend` | 这台设备优先哪条后端。键鼠永远 RawInput；`IG_` 垫默认 XInput。用户配置以后可覆盖。 |
| `likelyXInput` | 路径含 `L"IG_"` 的启发式。`true` 时 Raw HID 应忽略，改走 XInput，避免 A 键记两遍。空路径则 false，不会误杀。 |

**尚未进结构、但差分需要挂在设备上的状态**（不要进 `InputEvent`）：

- 键盘当前 Down 集合（用来丢掉连发、认 KeyUp）；
- 鼠标上一包虚拟屏坐标（绝对报告差分）；
- HID 上一份轴/键、Hat 上一档。

---

## `rawDevices_` 与三个 `next*`

`unordered_map<HANDLE, DeviceInfo>`：用本包 `hDevice` O(1) 找到行。

三个计数器是 **同种类多台设备** 的发号器，初值 1：

- `nextKeyboard_` → `keyboard_1`、`keyboard_2`
- `nextMouse_` → `mouse_1`、`mouse_2`
- `nextHid_` → `hid_1`、…（Gamepad/盘/踏板/Unknown 都走这里）

**不是按键计数。** 同一 `HANDLE` 无论按多少次都 map 命中，计数器不加。只有新的 `HANDLE` 才 `++`。

号一般不回收（避免新鼠标顶替旧的 `mouse_1`）。**拔出必须 `erase` 该 HANDLE**：Windows 可能把同一数值分给下一台设备。插拔通知来后应发 `DeviceDisconnected` 再删行。

XInput 没有 `HANDLE`，不能进这张 map。用 `getXInputDeviceID(slot)` → `xinput_0` … `xinput_3`。以后若要差分状态，另做 slot 表或统一键。

---

## 公开方法

### `getOrCreateRawDevice(hDevice, type)`

见过该 `HANDLE`：返回已有 `deviceID`。

没见过：

1. `makeDeviceID(type)` 发新号；
2. `queryRawDeviceName` 填 `rawName`；
3. `isLikelyXInputHid` 填 `likelyXInput`；
4. 插入 map，返回新 id。

可选：上层据此发 `DeviceConnected`。`hDevice == nullptr`（合成输入）应单独处理，不要当正常键。

当前实现插入时尚未根据 `likelyXInput` 改 `preferredBackend`；路由已可用 `shouldIgnoreRawHid`。设计上 `likelyXInput == true` 时默认 `preferredBackend = XInput`。

### `getXInputDeviceID(slot)`

`xinput_` + 槽号。与 Raw 发号独立。用户拔插手柄时 slot 可能变化，这是 XInput 的限制，会话内仍用槽号即可。

### `shouldIgnoreRawHid(hDevice)`

map 中该设备 `likelyXInput` 为真则 true。处理线程在解析 HID 前调用：为真则丢弃本包。

**注意：** 必须先 `getOrCreateRawDevice`（或等价地查过路径）再问 ignore。从未登记的 `HANDLE` 会返回 false，第一包可能漏进 HID 解析。接线顺序应是：先 getOrCreate，再 shouldIgnore。

---

## 私有方法

### `makeDeviceID(type)`

`Keyboard` / `Mouse` / 其余 → 三类前缀 + `next*++`（先用当前值再加一，故从 `_1` 起）。

### `queryRawDeviceName(hDevice)`

两次 `GetRawInputDeviceInfoW(RIDI_DEVICENAME)`：先问字符数（含结尾 `L'\0'`），再填 `wstring`。`size == 0` 或第二次返回 `-1`（设备刚拔）→ 空串。可在处理线程调，不要放 WndProc。

显示前如需去掉末尾空字符：`back() == L'\0'` 则 `pop_back`。`find(L"IG_")` 不依赖这一点。

### `isLikelyXInputHid(name)`

`name.find(L"IG_") != npos`。厂商约定，不是 100% 规格；用户以后可用 `preferredBackend` 覆盖。

---

## 推荐处理顺序

```text
pkt = packets.pop()
id = registry.getOrCreateRawDevice(pkt.hDevice, typeFrom(pkt.dwType))
if (pkt.dwType == RIM_TYPEHID && registry.shouldIgnoreRawHid(pkt.hDevice))
    丢弃
else
    用该 deviceID 的运行时状态差分 → 或 publish
```

XInput 轮询：`id = getXInputDeviceID(slot)`，不进 `rawDevices_`。
