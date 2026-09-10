#include "Timer.h"

#include <spdlog/spdlog.h>

void Timer::init() {
    QueryPerformanceFrequency(&Timer::global_qpcFreq);
    QueryPerformanceCounter(&Timer::global_startCount);
    if (Timer::global_qpcFreq.QuadPart <= 0) {
        spdlog::critical("[timer] QueryPerformanceFrequency failed (0 Hz)");
        return;
    }
    spdlog::info("[timer] init freqHz={} startQpc={}",
                 Timer::global_qpcFreq.QuadPart, Timer::global_startCount.QuadPart);
}

int64_t Timer::nowUs() {
    if (global_qpcFreq.QuadPart <= 0) {
        return 0;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<int64_t>((now.QuadPart - global_startCount.QuadPart) *
                                1000000LL / global_qpcFreq.QuadPart);
}

uint32_t Timer::ToFrameIndex(int64_t timeStampUs, int fps) {
    if (fps <= 0) {
        spdlog::error("[timer] ToFrameIndex invalid fps={}", fps);
        return 0;
    }
    const int64_t frameUs = 1000000LL / fps;
    return static_cast<uint32_t>(timeStampUs / frameUs);
}

InputEvent Timer::MakeBaseEvent() {
    InputEvent e;
    e.sequence = global_sequence.fetch_add(1, std::memory_order_relaxed);
    e.timestampUs = nowUs();
    e.frameIndex = ToFrameIndex(e.timestampUs, 60);
    return e;
}
