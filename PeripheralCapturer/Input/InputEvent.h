#pragma once

#include <cstdint>
#include <string>

// 采集后端。Xbox 兼容垫走 XInput；键鼠与通用 HID 走 Raw Input。
enum class InputBackend : uint8_t {
	RawInput,
	XInput,
};

// 运行期粗类型，对齐 Raw Input 的 dwType（键 / 鼠 / 其它 HID）。
// 摇杆、方向盘、踏板等是描述符解析后的细类，放 DeviceInfo.hidKind，不进每条事件。
// Xbox 垫用 InputBackend::XInput 区分，不必再加一个 Gamepad。
enum class InputDeviceType : uint8_t {
	Keyboard,
	Mouse,
	Hid,
	Unknown,
};

enum class HidDeviceKind : uint8_t {
	Unspecified,
	Gamepad,
	Joystick,
	Wheel,
	Pedal,
};

// 业务状态变化。处理线程对上一份设备状态做差分后再生成，不是系统包原样转发。
// 不发：键盘自动连发、轴/帽未变化的重复报告、dx=dy=0 的鼠标包。
enum class InputEventType : uint8_t {
	KeyDown,
	KeyUp,
	MouseMove,
	MouseButtonDown,
	MouseButtonUp,
	MouseWheel,
	ButtonDown,
	ButtonUp,
	AxisChanged,
	HatChanged,
	DeviceConnected,
	DeviceDisconnected,
};

// Hat / POV：离散档位，不是连续轴。写入 HatChanged.rawValue。
enum class HatDirection : uint8_t {
	Center = 0,
	N,
	NE,
	E,
	SE,
	S,
	SW,
	W,
	NW,
};

// 一条事件 = 某一个 control（或设备插拔）的状态变了。
// HID 原始 report、绝对鼠标坐标、连发 KeyDown 都不进这里。
struct InputEvent {
	uint64_t sequence = 0;		// 全局单调序号；同微秒时按此排序
	int64_t timestampUs = 0;
	uint32_t frameIndex = 0;	// 按本场 kFrameUs 归帧

	std::string deviceID;
	InputBackend backend = InputBackend::RawInput;
	InputDeviceType deviceType = InputDeviceType::Unknown;
	InputEventType type = InputEventType::KeyDown;

	// 码本 key_id / 设备上的控件名（A、LeftTrigger、Pointer、Hat…）。插拔事件可空。
	std::string control;

	// 数字键：0/1。轴：硬件整数。帽：HatDirection。滚轮：本包滚动量。
	int rawValue = 0;
	// 轴：扳机 0..1，摇杆 -1..1。数字键一般 0/1。MouseMove 不用。
	float normalizedValue = 0.f;

	// 仅 MouseMove：相对位移（绝对报告已在处理线程差分）。
	int dx = 0;
	int dy = 0;

	// 仅键盘：Windows 逻辑键、物理 MakeCode、E0/E1。
	uint16_t vkey = 0;
	uint16_t scanCode = 0;
	bool extended = false;
};
