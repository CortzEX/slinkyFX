#pragma once
// Small timing helpers for the Linux loader (counterparts of the Win32
// Sleep/GetTickCount64 used by the Windows build of Main.cpp).
#include <time.h>
#include <cerrno>

namespace Slinky {

inline void Sleep(unsigned long ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry remainder */ }
}

inline unsigned long long GetTickCount64() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000L);
}

} // namespace Slinky
