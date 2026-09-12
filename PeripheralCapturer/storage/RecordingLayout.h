#pragma once

#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>

// 开录把 device_bits 写进 sessions。通道顺序是下面这几张表。
// 帧 blob 进 data.db。packHeader 留给以后导出备份。
namespace RecLayout {

constexpr uint32_t kMagic = 0x31524350; // 'PCR1' 小端
constexpr uint16_t kFormatVersion = 1;
constexpr uint16_t kBitKeyboard = 1u << 0;
constexpr uint16_t kBitMouse = 1u << 1;
constexpr uint16_t kBitXInput = 1u << 2;
constexpr std::size_t kHeaderBytes = 16;

#pragma pack(push, 1)
struct Header {
    uint32_t magic = kMagic;
    uint16_t version = kFormatVersion;
    uint16_t deviceBits = 0;
    int64_t sessionId = 0;
};
#pragma pack(pop)

static_assert(sizeof(Header) == kHeaderBytes, "recording header must be 16 bytes");

inline bool packHeader(uint16_t deviceBits, int64_t sessionId, void* out16) {
    if (!out16 || sessionId <= 0) {
        return false;
    }
    Header h;
    h.deviceBits = deviceBits;
    h.sessionId = sessionId;
    std::memcpy(out16, &h, kHeaderBytes);
    return true;
}

inline bool unpackHeader(const void* in16, Header& out) {
    if (!in16) {
        return false;
    }
    std::memcpy(&out, in16, kHeaderBytes);
    return out.magic == kMagic && out.version == kFormatVersion && out.sessionId > 0;
}

// 下列顺序 = 帧里 bit / float 的下标。勾了哪类设备，就把哪段拼进去。
inline constexpr const char* kKeyboardDigital[] = {
    "a",        "b",          "c",          "d",         "e",          "f",
    "g",        "h",          "i",          "j",         "k",          "l",
    "m",        "n",          "o",          "p",         "q",          "r",
    "s",        "t",          "u",          "v",         "w",          "x",
    "y",        "z",          "0",          "1",         "2",          "3",
    "4",        "5",          "6",          "7",         "8",          "9",
    "space",    "shift-left", "shift-right","ctrl-left", "ctrl-right", "alt-left",
    "alt-right","win-left",   "win-right",  "menu",      "tab",        "caps-lock",
    "escape",   "enter",      "backspace",  "insert",    "delete",     "home",
    "end",      "page-up",    "page-down",  "arrow-left","arrow-right","arrow-up",
    "arrow-down","print-screen","scroll-lock","pause",   "num-lock",
    "semicolon","equal",      "comma",      "minus",     "period",     "slash",
    "grave",    "lbracket",   "backslash",  "rbracket",  "quote",
    "numpad-0", "numpad-1",   "numpad-2",   "numpad-3",  "numpad-4",   "numpad-5",
    "numpad-6", "numpad-7",   "numpad-8",   "numpad-9",  "numpad-mul", "numpad-add",
    "numpad-sub","numpad-dot","numpad-div",
    "f1",       "f2",         "f3",         "f4",        "f5",         "f6",
    "f7",       "f8",         "f9",         "f10",       "f11",        "f12",
};

inline constexpr const char* kMouseDigital[] = {
    "mouse-left", "mouse-right", "mouse-middle", "mouse-x1", "mouse-x2",
};

inline constexpr const char* kMouseAnalog[] = {"mouse-dx", "mouse-dy"};

inline constexpr const char* kPadDigital[] = {
    "pad-a",     "pad-b",      "pad-x",     "pad-y",     "pad-lb",    "pad-rb",
    "pad-start", "pad-back",   "pad-ls",    "pad-rs",    "pad-up",    "pad-down",
    "pad-left",  "pad-right",
};

inline constexpr const char* kPadAnalog[] = {
    "pad-lt", "pad-rt", "pad-lx", "pad-ly", "pad-rx", "pad-ry",
};

template <std::size_t N>
inline std::span<const char* const> table(const char* const (&arr)[N]) {
    return {arr, N};
}

inline int digitalCount(uint16_t bits) {
    int n = 0;
    if (bits & kBitKeyboard) {
        n += static_cast<int>(std::size(kKeyboardDigital));
    }
    if (bits & kBitMouse) {
        n += static_cast<int>(std::size(kMouseDigital));
    }
    if (bits & kBitXInput) {
        n += static_cast<int>(std::size(kPadDigital));
    }
    return n;
}

inline int analogCount(uint16_t bits) {
    int n = 0;
    if (bits & kBitMouse) {
        n += static_cast<int>(std::size(kMouseAnalog));
    }
    if (bits & kBitXInput) {
        n += static_cast<int>(std::size(kPadAnalog));
    }
    return n;
}

inline std::optional<int> findIn(std::span<const char* const> ids, std::string_view keyId,
                                 int offset) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == keyId) {
            return offset + static_cast<int>(i);
        }
    }
    return std::nullopt;
}

// 勾选顺序：键盘 → 鼠标 → 手柄。未勾的段不占下标。
inline std::optional<int> digitalIndex(uint16_t bits, std::string_view keyId) {
    int offset = 0;
    if (bits & kBitKeyboard) {
        if (const auto i = findIn(table(kKeyboardDigital), keyId, offset)) {
            return i;
        }
        offset += static_cast<int>(std::size(kKeyboardDigital));
    }
    if (bits & kBitMouse) {
        if (const auto i = findIn(table(kMouseDigital), keyId, offset)) {
            return i;
        }
        offset += static_cast<int>(std::size(kMouseDigital));
    }
    if (bits & kBitXInput) {
        if (const auto i = findIn(table(kPadDigital), keyId, offset)) {
            return i;
        }
    }
    return std::nullopt;
}

inline std::optional<int> analogIndex(uint16_t bits, std::string_view keyId) {
    int offset = 0;
    if (bits & kBitMouse) {
        if (const auto i = findIn(table(kMouseAnalog), keyId, offset)) {
            return i;
        }
        offset += static_cast<int>(std::size(kMouseAnalog));
    }
    if (bits & kBitXInput) {
        if (const auto i = findIn(table(kPadAnalog), keyId, offset)) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace RecLayout
