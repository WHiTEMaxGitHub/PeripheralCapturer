#pragma once
#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "InputEvent.h"

#include <Windows.h>
namespace Timer {
static std::atomic<uint64_t> global_sequence{0}; // 全局序列号
/* QPC(Query Performance Counter) 是Windows提供的高精度单调递增的计时器 */
static LARGE_INTEGER global_qpcFreq{};    // qpc频率（单位Hz）
static LARGE_INTEGER global_startCount{}; // 起始qpc计数器的值
void init();
int64_t nowUs();
uint32_t ToFrameIndex(int64_t timeStamp, int fps = 100);
InputEvent MakeBaseEvent();
} // namespace Timer
