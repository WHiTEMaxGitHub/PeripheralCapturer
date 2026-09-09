#include "DeviceRegistry.h"

std::string DeviceRegistry::getOrCreateRawDevice(HANDLE hDevice,
                                                 InputDeviceType type) {
    auto it = rawDevices_.find(hDevice);
    if (it != rawDevices_.end()) {
        return it->second.deviceID;
    }
    DeviceInfo info;
    info.deviceID = makeDeviceID(type);
    info.type = type;
    info.rawName = queryRawDeviceName(hDevice);
    info.likelyXInput = isLikelyXInputHid(info.rawName);
    rawDevices_[hDevice] = info;
    return info.deviceID;
}
// 从设备种类来获取ID
std::string DeviceRegistry::makeDeviceID(InputDeviceType type) {
    switch (type) {
        case InputDeviceType::Keyboard:
            return "keyboard_" + std::to_string(nextKeyboard_++);
        case InputDeviceType::Mouse:
            return "mouse_" + std::to_string(nextMouse_++);
        case InputDeviceType::Hid:
        case InputDeviceType::Unknown:
        default: return "hid_" + std::to_string(nextHid_++);
    }
}

std::wstring DeviceRegistry::queryRawDeviceName(HANDLE hDevice) {
    UINT size = 0;
    // 获取设备名（widechar），但是需要精确的大小，此次的作用是获取名字字符数
    GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nullptr, &size);
    // 没有获取到名字（size为0）
    if (size == 0) {
        return L"";
    }
    std::wstring name(size, L'\0');
    // 如果获取名字失败则返回空
    if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, name.data(), &size) ==
        static_cast<UINT>(-1)) {
        return L"";
    }

    return name;
}
