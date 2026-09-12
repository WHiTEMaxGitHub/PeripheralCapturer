#pragma once

#include "InputEvent.h"

#include <cstdint>

// 进程内一份 QPC 时钟。定义必须在 cpp：头文件 static 会让每个 TU 各有一份频率。
namespace Timer {

void init();

// 本场归帧用的 fps。没开录时用默认 60；开录后再改，不要写死在 MakeBaseEvent 里。
void setSessionFps(int fps);
int sessionFps();

int64_t nowUs();
uint32_t ToFrameIndex(int64_t timestampUs, int fps);

// XInput / 合成：时间用当下 nowUs()。
InputEvent MakeBaseEvent();
// 从 Raw 包生成事件：沿用入队前打的时间戳，不要再采一次 QPC。
InputEvent MakeBaseEvent(int64_t timestampUs);

} // namespace Timer
