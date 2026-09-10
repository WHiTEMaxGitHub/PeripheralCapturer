#include "GamepadProbe.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Xinput.h>

#include <spdlog/spdlog.h>

#include <vector>

namespace {

bool isGamepadHid(const RID_DEVICE_INFO& info) {
    if (info.dwType != RIM_TYPEHID) {
        return false;
    }
    const unsigned page = info.hid.usUsagePage;
    const unsigned usage = info.hid.usUsage;
    if (page == 0x01) {
        return usage == 0x04 || usage == 0x05 || usage == 0x08;
    }
    return page == 0x05;
}

} // namespace

void enumerateHidGamepads(DeviceRegistry& registry) {
    UINT count = 0;
    GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    if (count == 0) {
        return;
    }
    std::vector<RAWINPUTDEVICELIST> list(count);
    if (GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) ==
        static_cast<UINT>(-1)) {
        spdlog::warn("[pad] GetRawInputDeviceList failed err={}", GetLastError());
        return;
    }
    for (UINT i = 0; i < count; ++i) {
        if (list[i].dwType != RIM_TYPEHID) {
            continue;
        }
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) ==
            static_cast<UINT>(-1)) {
            spdlog::debug("[pad] RIDI_DEVICEINFO failed err={}", GetLastError());
            continue;
        }
        if (!isGamepadHid(info)) {
            continue;
        }
        registry.getOrCreateRawDevice(list[i].hDevice, InputDeviceType::Hid);
    }
}

void pollXInputSlots(DeviceRegistry& registry, bool wasConnected[4]) {
    for (DWORD slot = 0; slot < 4; ++slot) {
        XINPUT_STATE state{};
        const DWORD result = XInputGetState(slot, &state);
        const bool on = (result == ERROR_SUCCESS);
        if (!on && result != ERROR_DEVICE_NOT_CONNECTED) {
            static bool loggedUnexpected[4] = {};
            if (!loggedUnexpected[slot]) {
                spdlog::warn("[pad] XInputGetState slot={} err={}", slot, result);
                loggedUnexpected[slot] = true;
            }
        }
        const std::string id = registry.getXInputDeviceID(slot);
        if (on && !wasConnected[slot]) {
            spdlog::info("[pad] XInput connected {}", id);
        } else if (!on && wasConnected[slot]) {
            spdlog::info("[pad] XInput disconnected {}", id);
        }
        wasConnected[slot] = on;
    }
}
