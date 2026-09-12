#include "InputPipeline.h"

#include "Timer.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {

constexpr auto kXInputPeriod = std::chrono::milliseconds(4);

InputDeviceType typeFromRaw(DWORD dwType) {
    switch (dwType) {
    case RIM_TYPEKEYBOARD:
        return InputDeviceType::Keyboard;
    case RIM_TYPEMOUSE:
        return InputDeviceType::Mouse;
    case RIM_TYPEHID:
        return InputDeviceType::Hid;
    default:
        return InputDeviceType::Unknown;
    }
}

const char* eventTypeName(InputEventType type) {
    switch (type) {
    case InputEventType::KeyDown:
        return "KeyDown";
    case InputEventType::KeyUp:
        return "KeyUp";
    case InputEventType::MouseMove:
        return "MouseMove";
    case InputEventType::MouseButtonDown:
        return "MouseButtonDown";
    case InputEventType::MouseButtonUp:
        return "MouseButtonUp";
    case InputEventType::MouseWheel:
        return "MouseWheel";
    case InputEventType::ButtonDown:
        return "ButtonDown";
    case InputEventType::ButtonUp:
        return "ButtonUp";
    case InputEventType::AxisChanged:
        return "AxisChanged";
    case InputEventType::DeviceConnected:
        return "DeviceConnected";
    case InputEventType::DeviceDisconnected:
        return "DeviceDisconnected";
    default:
        return "other";
    }
}

uint16_t normalizeVkey(uint16_t vkey, uint16_t scan, bool extended) {
    if (vkey == VK_SHIFT) {
        return scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    }
    if (vkey == VK_CONTROL) {
        return extended ? VK_RCONTROL : VK_LCONTROL;
    }
    if (vkey == VK_MENU) {
        return extended ? VK_RMENU : VK_LMENU;
    }
    return vkey;
}

std::string keyboardControl(uint16_t vkey) {
    // 字母 / 数字行：VK 本身就是 ASCII，直接当 key_id（w、1…）。
    if (vkey >= 'A' && vkey <= 'Z') {
        return std::string(1, static_cast<char>(vkey - 'A' + 'a'));
    }
    if (vkey >= '0' && vkey <= '9') {
        return std::string(1, static_cast<char>(vkey));
    }
    switch (vkey) {
    case VK_SPACE:
        return "space";
    case VK_LSHIFT:
        return "shift-left";
    case VK_RSHIFT:
        return "shift-right";
    case VK_LCONTROL:
        return "ctrl-left";
    case VK_RCONTROL:
        return "ctrl-right";
    case VK_LMENU:
        return "alt-left";
    case VK_RMENU:
        return "alt-right";
    case VK_LWIN:
        return "win-left";
    case VK_RWIN:
        return "win-right";
    case VK_APPS:
        return "menu";
    case VK_TAB:
        return "tab";
    case VK_CAPITAL:
        return "caps-lock";
    case VK_ESCAPE:
        return "escape";
    case VK_RETURN:
        return "enter";
    case VK_BACK:
        return "backspace";
    case VK_INSERT:
        return "insert";
    case VK_DELETE:
        return "delete";
    case VK_HOME:
        return "home";
    case VK_END:
        return "end";
    case VK_PRIOR:
        return "page-up";
    case VK_NEXT:
        return "page-down";
    case VK_LEFT:
        return "arrow-left";
    case VK_RIGHT:
        return "arrow-right";
    case VK_UP:
        return "arrow-up";
    case VK_DOWN:
        return "arrow-down";
    case VK_SNAPSHOT:
        return "print-screen";
    case VK_SCROLL:
        return "scroll-lock";
    case VK_PAUSE:
        return "pause";
    case VK_NUMLOCK:
        return "num-lock";
    case VK_OEM_1:
        return "semicolon";
    case VK_OEM_PLUS:
        return "equal";
    case VK_OEM_COMMA:
        return "comma";
    case VK_OEM_MINUS:
        return "minus";
    case VK_OEM_PERIOD:
        return "period";
    case VK_OEM_2:
        return "slash";
    case VK_OEM_3:
        return "grave";
    case VK_OEM_4:
        return "lbracket";
    case VK_OEM_5:
        return "backslash";
    case VK_OEM_6:
        return "rbracket";
    case VK_OEM_7:
        return "quote";
    case VK_MULTIPLY:
        return "numpad-mul";
    case VK_ADD:
        return "numpad-add";
    case VK_SUBTRACT:
        return "numpad-sub";
    case VK_DECIMAL:
        return "numpad-dot";
    case VK_DIVIDE:
        return "numpad-div";
    case VK_NUMPAD0:
        return "numpad-0";
    case VK_NUMPAD1:
        return "numpad-1";
    case VK_NUMPAD2:
        return "numpad-2";
    case VK_NUMPAD3:
        return "numpad-3";
    case VK_NUMPAD4:
        return "numpad-4";
    case VK_NUMPAD5:
        return "numpad-5";
    case VK_NUMPAD6:
        return "numpad-6";
    case VK_NUMPAD7:
        return "numpad-7";
    case VK_NUMPAD8:
        return "numpad-8";
    case VK_NUMPAD9:
        return "numpad-9";
    default:
        if (vkey >= VK_F1 && vkey <= VK_F24) {
            return "f" + std::to_string(vkey - VK_F1 + 1);
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "vk-%02x", vkey);
        return buf;
    }
}

uint32_t keyIdentity(uint16_t scan, bool extended) {
    return (static_cast<uint32_t>(extended) << 16) | scan;
}

const RAWINPUT* asRaw(const RawInputPacket& pkt) {
    if (pkt.bytes.size() < sizeof(RAWINPUTHEADER)) {
        return nullptr;
    }
    return reinterpret_cast<const RAWINPUT*>(pkt.bytes.data());
}

float deadzoneAxis(short value, short dead) {
    const int v = static_cast<int>(value);
    const int mag = std::abs(v);
    if (mag <= dead) {
        return 0.f;
    }
    const float sign = v < 0 ? -1.f : 1.f;
    return sign * static_cast<float>(mag - dead) / static_cast<float>(32767 - dead);
}

float deadzoneTrigger(BYTE value) {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.f;
    }
    return static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
           static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

bool axisMoved(float a, float b) {
    return std::fabs(a - b) > 0.002f;
}

struct PadButton {
    WORD mask;
    const char* control;
};

constexpr PadButton kPadButtons[] = {
    {XINPUT_GAMEPAD_A, "pad-a"},
    {XINPUT_GAMEPAD_B, "pad-b"},
    {XINPUT_GAMEPAD_X, "pad-x"},
    {XINPUT_GAMEPAD_Y, "pad-y"},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, "pad-lb"},
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, "pad-rb"},
    {XINPUT_GAMEPAD_START, "pad-start"},
    {XINPUT_GAMEPAD_BACK, "pad-back"},
    {XINPUT_GAMEPAD_LEFT_THUMB, "pad-ls"},
    {XINPUT_GAMEPAD_RIGHT_THUMB, "pad-rs"},
    {XINPUT_GAMEPAD_DPAD_UP, "pad-up"},
    {XINPUT_GAMEPAD_DPAD_DOWN, "pad-down"},
    {XINPUT_GAMEPAD_DPAD_LEFT, "pad-left"},
    {XINPUT_GAMEPAD_DPAD_RIGHT, "pad-right"},
};

} // namespace

bool InputPipeline::start() {
    if (running_.exchange(true)) {
        spdlog::warn("[capture] pipeline already running");
        return true;
    }
    worker_ = std::thread([this] { run(); });
    spdlog::info("[capture] pipeline started");
    return true;
}

void InputPipeline::stop() {
    if (!running_.exchange(false) && !worker_.joinable()) {
        return;
    }
    packets_.close();
    if (worker_.joinable()) {
        worker_.join();
    }
    bus_.close();
    spdlog::info("[capture] pipeline stopped published={} packetDropped={}",
                 published_.load(), packets_.dropped());
}

void InputPipeline::run() {
    while (running_.load(std::memory_order_relaxed) || packets_.size() > 0) {
        auto pkt = packets_.popFor(kXInputPeriod);
        if (pkt) {
            processPacket(*pkt);
            while (auto more = packets_.tryPop()) {
                processPacket(*more);
            }
        } else if (packets_.closed() && packets_.size() == 0) {
            break;
        }
        pollXInput();

        const auto dropped = packets_.dropped();
        const auto now = Timer::nowUs();
        if (dropped != lastDropped_ && now - lastDropLogUs_ >= 5'000'000) {
            spdlog::debug("[capture] packet queue dropped={}", dropped);
            lastDropped_ = dropped;
            lastDropLogUs_ = now;
        }
    }
}

void InputPipeline::processPacket(const RawInputPacket& pkt) {
    if (pkt.kind == RawPacketKind::DeviceRemoval) {
        processRemoval(pkt);
        return;
    }
    if (pkt.kind == RawPacketKind::DeviceArrival) {
        return;
    }
    if (!pkt.hDevice) {
        return;
    }
    const InputDeviceType type = typeFromRaw(pkt.dwType);
    const std::string id = registry_.getOrCreateRawDevice(pkt.hDevice, type);
    if (id.empty()) {
        return;
    }
    if (pkt.dwType == RIM_TYPEHID) {
        if (registry_.shouldIgnoreRawHid(pkt.hDevice)) {
            return;
        }
        return;
    }

    if (seenFirst_.insert(id + "|conn").second) {
        InputEvent ev = Timer::MakeBaseEvent(pkt.timestampUs);
        ev.deviceID = id;
        ev.backend = InputBackend::RawInput;
        ev.deviceType = type;
        ev.type = InputEventType::DeviceConnected;
        publish(std::move(ev));
    }

    if (pkt.dwType == RIM_TYPEKEYBOARD) {
        processKeyboard(pkt, id);
    } else if (pkt.dwType == RIM_TYPEMOUSE) {
        processMouse(pkt, id);
    }
}

void InputPipeline::processRemoval(const RawInputPacket& pkt) {
    if (!pkt.hDevice || !registry_.contains(pkt.hDevice)) {
        keyboards_.erase(pkt.hDevice);
        mice_.erase(pkt.hDevice);
        return;
    }
    const std::string id = registry_.getOrCreateRawDevice(pkt.hDevice, InputDeviceType::Unknown);
    InputEvent ev = Timer::MakeBaseEvent(pkt.timestampUs);
    ev.deviceID = id;
    ev.backend = InputBackend::RawInput;
    ev.deviceType = InputDeviceType::Unknown;
    ev.type = InputEventType::DeviceDisconnected;
    publish(std::move(ev));
    registry_.erase(pkt.hDevice);
    keyboards_.erase(pkt.hDevice);
    mice_.erase(pkt.hDevice);
    spdlog::info("[registry] removed {}", id);
}

void InputPipeline::processKeyboard(const RawInputPacket& pkt, const std::string& deviceID) {
    const RAWINPUT* raw = asRaw(pkt);
    if (!raw || pkt.bytes.size() < sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
        return;
    }
    const RAWKEYBOARD& kb = raw->data.keyboard;
    if (kb.VKey == 0xFF) {
        return;
    }
    const bool extended = (kb.Flags & RI_KEY_E0) != 0;
    const bool up = (kb.Flags & RI_KEY_BREAK) != 0;
    const uint16_t vkey = normalizeVkey(kb.VKey, kb.MakeCode, extended);
    const uint32_t ident = keyIdentity(kb.MakeCode, extended);
    auto& state = keyboards_[pkt.hDevice];
    const bool wasDown = state.down.contains(ident);
    if (!up && wasDown) {
        return;
    }
    if (up && !wasDown) {
        return;
    }
    if (up) {
        state.down.erase(ident);
    } else {
        state.down.insert(ident);
    }

    InputEvent ev = Timer::MakeBaseEvent(pkt.timestampUs);
    ev.deviceID = deviceID;
    ev.backend = InputBackend::RawInput;
    ev.deviceType = InputDeviceType::Keyboard;
    ev.type = up ? InputEventType::KeyUp : InputEventType::KeyDown;
    ev.control = keyboardControl(vkey);
    ev.rawValue = up ? 0 : 1;
    ev.normalizedValue = up ? 0.f : 1.f;
    ev.vkey = vkey;
    ev.scanCode = kb.MakeCode;
    ev.extended = extended;
    publish(std::move(ev));
}

void InputPipeline::processMouse(const RawInputPacket& pkt, const std::string& deviceID) {
    const RAWINPUT* raw = asRaw(pkt);
    if (!raw || pkt.bytes.size() < sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) {
        return;
    }
    const RAWMOUSE& mouse = raw->data.mouse;
    auto& state = mice_[pkt.hDevice];

    const bool absolute = (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
    int dx = 0;
    int dy = 0;
    if (absolute) {
        if (state.hasAbs) {
            dx = static_cast<int>(mouse.lLastX - state.lastX);
            dy = static_cast<int>(mouse.lLastY - state.lastY);
        }
        state.lastX = mouse.lLastX;
        state.lastY = mouse.lLastY;
        state.hasAbs = true;
    } else {
        dx = static_cast<int>(mouse.lLastX);
        dy = static_cast<int>(mouse.lLastY);
    }
    if (dx != 0 || dy != 0) {
        InputEvent ev = Timer::MakeBaseEvent(pkt.timestampUs);
        ev.deviceID = deviceID;
        ev.backend = InputBackend::RawInput;
        ev.deviceType = InputDeviceType::Mouse;
        ev.type = InputEventType::MouseMove;
        ev.control = "pointer";
        ev.dx = dx;
        ev.dy = dy;
        publish(std::move(ev));
    }

    const USHORT flags = mouse.usButtonFlags;
    struct MouseBtn {
        USHORT downBit;
        USHORT upBit;
        const char* control;
    };
    static constexpr MouseBtn kBtns[] = {
        {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, "mouse-left"},
        {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, "mouse-right"},
        {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, "mouse-middle"},
        {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, "mouse-x1"},
        {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, "mouse-x2"},
    };
    for (const auto& btn : kBtns) {
        const bool down = (flags & btn.downBit) != 0;
        const bool up = (flags & btn.upBit) != 0;
        if (!down && !up) {
            continue;
        }
        InputEvent ev = Timer::MakeBaseEvent(pkt.timestampUs);
        ev.deviceID = deviceID;
        ev.backend = InputBackend::RawInput;
        ev.deviceType = InputDeviceType::Mouse;
        ev.type = down ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp;
        ev.control = btn.control;
        ev.rawValue = down ? 1 : 0;
        ev.normalizedValue = down ? 1.f : 0.f;
        publish(std::move(ev));
    }

    if (flags & RI_MOUSE_WHEEL) {
        InputEvent ev = Timer::MakeBaseEvent(pkt.timestampUs);
        ev.deviceID = deviceID;
        ev.backend = InputBackend::RawInput;
        ev.deviceType = InputDeviceType::Mouse;
        ev.type = InputEventType::MouseWheel;
        ev.control = "mouse-wheel";
        ev.rawValue = static_cast<short>(mouse.usButtonData);
        ev.normalizedValue = static_cast<float>(ev.rawValue);
        publish(std::move(ev));
    }
    if (flags & RI_MOUSE_HWHEEL) {
        InputEvent ev = Timer::MakeBaseEvent(pkt.timestampUs);
        ev.deviceID = deviceID;
        ev.backend = InputBackend::RawInput;
        ev.deviceType = InputDeviceType::Mouse;
        ev.type = InputEventType::MouseWheel;
        ev.control = "mouse-hwheel";
        ev.rawValue = static_cast<short>(mouse.usButtonData);
        ev.normalizedValue = static_cast<float>(ev.rawValue);
        publish(std::move(ev));
    }
}

void InputPipeline::pollXInput() {
    for (DWORD slot = 0; slot < 4; ++slot) {
        XINPUT_STATE state{};
        const DWORD result = XInputGetState(slot, &state);
        auto& prev = pads_[slot];
        const bool on = result == ERROR_SUCCESS;
        const std::string id = registry_.getXInputDeviceID(slot);

        if (on != prev.connected) {
            InputEvent ev = Timer::MakeBaseEvent();
            ev.deviceID = id;
            ev.backend = InputBackend::XInput;
            ev.deviceType = InputDeviceType::Hid;
            ev.type = on ? InputEventType::DeviceConnected : InputEventType::DeviceDisconnected;
            publish(std::move(ev));
            spdlog::info("[pad] {} {}", on ? "connected" : "disconnected", id);
            prev = XInputRuntime{};
            prev.connected = on;
            if (!on) {
                continue;
            }
        }
        if (!on) {
            continue;
        }

        const WORD buttons = state.Gamepad.wButtons;
        const WORD changed = static_cast<WORD>(buttons ^ prev.buttons);
        if (changed) {
            for (const auto& btn : kPadButtons) {
                if ((changed & btn.mask) == 0) {
                    continue;
                }
                const bool down = (buttons & btn.mask) != 0;
                InputEvent ev = Timer::MakeBaseEvent();
                ev.deviceID = id;
                ev.backend = InputBackend::XInput;
                ev.deviceType = InputDeviceType::Hid;
                ev.type = down ? InputEventType::ButtonDown : InputEventType::ButtonUp;
                ev.control = btn.control;
                ev.rawValue = down ? 1 : 0;
                ev.normalizedValue = down ? 1.f : 0.f;
                publish(std::move(ev));
            }
            prev.buttons = buttons;
        }

        const auto emitAxis = [&](const char* control, int raw, float norm, float& prevNorm) {
            if (!axisMoved(norm, prevNorm)) {
                return;
            }
            prevNorm = norm;
            InputEvent ev = Timer::MakeBaseEvent();
            ev.deviceID = id;
            ev.backend = InputBackend::XInput;
            ev.deviceType = InputDeviceType::Hid;
            ev.type = InputEventType::AxisChanged;
            ev.control = control;
            ev.rawValue = raw;
            ev.normalizedValue = norm;
            publish(std::move(ev));
        };

        const float nlt = deadzoneTrigger(state.Gamepad.bLeftTrigger);
        const float nrt = deadzoneTrigger(state.Gamepad.bRightTrigger);
        // XInput 摇杆 Y：上为负。归一化后上为正，和常见游戏坐标一致。
        const float nlx = deadzoneAxis(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        const float nly = -deadzoneAxis(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        const float nrx = deadzoneAxis(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        const float nry = -deadzoneAxis(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

        emitAxis("pad-lt", state.Gamepad.bLeftTrigger, nlt, prev.nlt);
        emitAxis("pad-rt", state.Gamepad.bRightTrigger, nrt, prev.nrt);
        emitAxis("pad-lx", state.Gamepad.sThumbLX, nlx, prev.nlx);
        emitAxis("pad-ly", state.Gamepad.sThumbLY, nly, prev.nly);
        emitAxis("pad-rx", state.Gamepad.sThumbRX, nrx, prev.nrx);
        emitAxis("pad-ry", state.Gamepad.sThumbRY, nry, prev.nry);
    }
}

void InputPipeline::publish(InputEvent event) {
    noteFirst(event);
    bus_.publish(event);
    published_.fetch_add(1, std::memory_order_relaxed);
}

void InputPipeline::noteFirst(const InputEvent& event) {
    if (event.control.empty()) {
        return;
    }
    const std::string key = event.deviceID + "|" + event.control;
    if (!seenFirst_.insert(key).second) {
        return;
    }
    spdlog::info("[capture] first control={} ev={} device={}", event.control,
                 eventTypeName(event.type), event.deviceID);
}
