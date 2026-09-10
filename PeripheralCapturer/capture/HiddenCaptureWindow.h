#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// 第三扇窗：零尺寸 / message-only，只收 WM_INPUT，不显示。
// 不要用配置窗 winId()，也不要嵌进 POV。
class HiddenCaptureWindow {
public:
    bool create();
    void destroy();
    HWND hwnd() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};
