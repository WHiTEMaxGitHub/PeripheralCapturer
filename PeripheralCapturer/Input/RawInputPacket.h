#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <vector>

// 捕获线程（WM_INPUT）→ 处理线程 的载荷。
// 只拷系统包 + 入队前打好的微秒时间戳。禁止在此解析 HID、差分、写盘。
// 字段含义与接线见 docs/InputQueue.md。
struct RawInputPacket {
    // 相对 Timer 起点的微秒。必须在 tryPush 之前用 Timer::nowUs() 填写。
    // 若等处理线程弹出再计时，队列等待会被算进「按键发生时刻」。
    int64_t timestampUs = 0;

    // 本包来自哪台 Raw 设备。处理线程交给 DeviceRegistry 换成 deviceID。
    // 运行期句柄：不写 log/JSON，不 CloseHandle；拔出后数值可能被系统复用。
    HANDLE hDevice = nullptr;

    // RAWINPUTHEADER.dwType：RIM_TYPEKEYBOARD / MOUSE / HID。
    // 处理线程靠它决定如何解释 bytes，并映射到 InputDeviceType。
    DWORD dwType = 0;

    // GetRawInputData 整包拷贝（含 RAWINPUT 头 + RAWKEYBOARD/RAWMOUSE/HID report）。
    // WndProc 只 memcpy 进 vector；具体键/轴在处理线程解析。
    std::vector<uint8_t> bytes;
};
