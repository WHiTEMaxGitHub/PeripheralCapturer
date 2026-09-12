#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../Input/RawInputPacket.h"
#include "../Input/BoundedQueue.h"

// 第三扇窗：零尺寸 / message-only，只收 WM_INPUT，不显示。
// WndProc 只拷包入队；不要用配置窗 winId()，也不要嵌进 POV。
class HiddenCaptureWindow {
public:
    bool create(BoundedQueue<RawInputPacket>& packets);
    void destroy();
    HWND hwnd() const { return hwnd_; }

private:
    void enqueueInput(LPARAM lParam);
    void enqueueDeviceChange(WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    BoundedQueue<RawInputPacket>* packets_ = nullptr;
    bool registered_ = false;

    friend LRESULT CALLBACK captureWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
