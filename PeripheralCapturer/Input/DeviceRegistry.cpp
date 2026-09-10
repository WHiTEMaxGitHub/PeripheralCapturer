#include "DeviceRegistry.h"

#include <spdlog/spdlog.h>

#include <string>

namespace {

const char* deviceTypeName(InputDeviceType type) {
    switch (type) {
    case InputDeviceType::Keyboard:
        return "keyboard";
    case InputDeviceType::Mouse:
        return "mouse";
    case InputDeviceType::Hid:
        return "hid";
    default:
        return "unknown";
    }
}

std::string utf8FromWide(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                          static_cast<int>(wide.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (bytes <= 0) {
        spdlog::warn("[registry] WideCharToMultiByte failed err={}", GetLastError());
        return {};
    }
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        utf8.data(), bytes, nullptr, nullptr);
    const auto nul = utf8.find('\0');
    if (nul != std::string::npos) {
        utf8.resize(nul);
    }
    return utf8;
}

} // namespace

std::string DeviceRegistry::getOrCreateRawDevice(HANDLE hDevice,
                                                 InputDeviceType type) {
    if (!hDevice) {
        spdlog::warn("[registry] getOrCreateRawDevice null HANDLE");
        return {};
    }
    auto it = rawDevices_.find(hDevice);
    if (it != rawDevices_.end()) {
        return it->second.deviceID;
    }
    DeviceInfo info;
    info.deviceID = makeDeviceID(type);
    info.type = type;
    info.rawName = queryRawDeviceName(hDevice);
    info.likelyXInput = isLikelyXInputHid(info.rawName);
    if (info.likelyXInput) {
        info.preferredBackend = InputBackend::XInput;
    }
    rawDevices_[hDevice] = info;

    spdlog::info("[registry] new device id={} type={} xinput={} path={}",
                 info.deviceID, deviceTypeName(type), info.likelyXInput,
                 utf8FromWide(info.rawName));
    if (info.rawName.empty()) {
        spdlog::warn("[registry] {} has empty RIDI_DEVICENAME", info.deviceID);
    }
    return info.deviceID;
}

std::vector<DeviceInfo> DeviceRegistry::snapshot() const {
    std::vector<DeviceInfo> out;
    out.reserve(rawDevices_.size());
    for (const auto& [handle, info] : rawDevices_) {
        out.push_back(info);
    }
    return out;
}

std::string DeviceRegistry::makeDeviceID(InputDeviceType type) {
    switch (type) {
    case InputDeviceType::Keyboard:
        return "keyboard_" + std::to_string(nextKeyboard_++);
    case InputDeviceType::Mouse:
        return "mouse_" + std::to_string(nextMouse_++);
    case InputDeviceType::Hid:
    case InputDeviceType::Unknown:
    default:
        return "hid_" + std::to_string(nextHid_++);
    }
}

std::wstring DeviceRegistry::queryRawDeviceName(HANDLE hDevice) {
    UINT size = 0;
    GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nullptr, &size);
    if (size == 0) {
        return L"";
    }
    std::wstring name(size, L'\0');
    if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, name.data(), &size) ==
        static_cast<UINT>(-1)) {
        spdlog::warn("[registry] RIDI_DEVICENAME read failed err={}", GetLastError());
        return L"";
    }
    return name;
}
