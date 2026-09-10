#pragma once

#include "DeviceRegistry.h"

// 手柄识别小 demo：不注册 WM_INPUT，也不解析按键。
// Xbox 用 XInput 槽位；其它垫用 GetRawInputDeviceList 里的 Game Pad/Joystick Usage。
void enumerateHidGamepads(DeviceRegistry& registry);
void pollXInputSlots(DeviceRegistry& registry, bool wasConnected[4]);
