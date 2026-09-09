#include "Timer.h"

#include <spdlog/spdlog.h>
// 初始化
void Timer::init() {
    QueryPerformanceFrequency(&Timer::global_qpcFreq);
    QueryPerformanceCounter(&Timer::global_startCount);
    spdlog::info("Timer inited");
}
// 获取现在的微秒时间
int64_t Timer::nowUs() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<int64_t>((now.QuadPart - global_startCount.QuadPart) *
                                1000000LL / global_qpcFreq.QuadPart);
}
// 将时间戳转换为帧戳
uint32_t Timer::ToFrameIndex(int64_t timeStampUs, int fps) {
    const int64_t frameUs = 1000000LL / fps;
    return static_cast<uint32_t>(timeStampUs / frameUs);
}
// 最基础的InputEvent，仅填充序列号与时间信息
InputEvent Timer::MakeBaseEvent() {
    InputEvent e;
    e.sequence = global_sequence.fetch_add(1, std::memory_order_relaxed);
	// 这里要提前记录，以免错过周期导致错误
    e.timestampUs = nowUs();
    e.frameIndex = ToFrameIndex(e.timestampUs, 60);
}
