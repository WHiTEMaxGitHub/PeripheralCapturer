#pragma once
#include "InputEvent.h"

#include <Windows.h>
#include <string>
#include <unordered_map>
#include <vector>
struct DeviceInfo {
    std::string deviceID;
    std::wstring rawName;
    InputDeviceType type = InputDeviceType::Unknown;
    HidDeviceKind hidKind = HidDeviceKind::Unspecified;
    InputBackend preferredBackend = InputBackend::RawInput;
    bool likelyXInput = false;
};
// 硬件注册表
class DeviceRegistry {
public:
    std::string getOrCreateRawDevice(HANDLE hDevice, InputDeviceType type);
    std::vector<DeviceInfo> snapshot() const;
	// 获取XInput设备ID
    inline std::string getXInputDeviceID(DWORD slot) {
        return "xinput_" + std::to_string(slot);
	}
	// 判断是否应当忽略RawInput（也就是XInput设备）
    inline bool shouldIgnoreRawHid(HANDLE hDevice) {
        auto it = rawDevices_.find(hDevice);
        if (it == rawDevices_.end()) return false;
        return it->second.likelyXInput;
	}

private:
    std::unordered_map<HANDLE, DeviceInfo> rawDevices_;	// 设备表本体
	// 同种类多台设备时发号：mouse_1/mouse_2
    uint32_t nextMouse_ = 1;
    uint32_t nextKeyboard_ = 1;
    uint32_t nextHid_ = 1;

	std::string makeDeviceID(InputDeviceType type);
    std::wstring queryRawDeviceName(HANDLE hDevice);
    // 如果设备名形如"...IG_..."则大概率是XInput设备（大部分硬件厂商的约定）
	inline bool isLikelyXInputHid(const std::wstring& name) {
        return name.find(L"IG_") != std::wstring::npos;
	}
};