#include "Timer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <spdlog/spdlog.h>

#include <atomic>

namespace {

std::atomic<uint64_t> g_sequence{0};
LARGE_INTEGER g_qpcFreq{};
LARGE_INTEGER g_startCount{};
std::atomic<int> g_sessionFps{60};

InputEvent fillBase(int64_t timestampUs) {
    InputEvent e;
    e.sequence = g_sequence.fetch_add(1, std::memory_order_relaxed);
    e.timestampUs = timestampUs;
    e.frameIndex = Timer::ToFrameIndex(timestampUs, Timer::sessionFps());
    return e;
}

} // namespace

void Timer::init() {
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_startCount);
    if (g_qpcFreq.QuadPart <= 0) {
        spdlog::critical("[timer] QueryPerformanceFrequency failed (0 Hz)");
        return;
    }
    g_sessionFps.store(60, std::memory_order_relaxed);
    spdlog::info("[timer] init freqHz={} startQpc={} sessionFps={}", g_qpcFreq.QuadPart,
                 g_startCount.QuadPart, sessionFps());
}

void Timer::setSessionFps(int fps) {
    if (fps <= 0) {
        spdlog::error("[timer] setSessionFps invalid fps={}", fps);
        return;
    }
    g_sessionFps.store(fps, std::memory_order_relaxed);
    spdlog::info("[timer] session fps={}", fps);
}

int Timer::sessionFps() {
    return g_sessionFps.load(std::memory_order_relaxed);
}

int64_t Timer::nowUs() {
    if (g_qpcFreq.QuadPart <= 0) {
        return 0;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<int64_t>((now.QuadPart - g_startCount.QuadPart) * 1000000LL /
                                g_qpcFreq.QuadPart);
}

uint32_t Timer::ToFrameIndex(int64_t timestampUs, int fps) {
    if (fps <= 0) {
        spdlog::error("[timer] ToFrameIndex invalid fps={}", fps);
        return 0;
    }
    const int64_t frameUs = 1000000LL / fps;
    return static_cast<uint32_t>(timestampUs / frameUs);
}

InputEvent Timer::MakeBaseEvent() {
    return fillBase(nowUs());
}

InputEvent Timer::MakeBaseEvent(int64_t timestampUs) {
    return fillBase(timestampUs);
}
