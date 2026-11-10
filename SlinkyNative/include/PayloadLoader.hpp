#pragma once

#include <windows.h>
#include <string>
#include "MinecraftDetector.hpp"

namespace SlinkyNative {

enum class InjectionStatus {
    Success = 0,
    AlreadyRunning,
    Reloaded,
    Failed
};

class PayloadLoader {
public:
    static InjectionStatus LoadAndStart(HMODULE hModule, const MinecraftInfo& mcInfo);

private:
    // The C++ client, from the file the loader staged next to this one: slinky-engine.dll on Windows,
    // libslinky-engine.so on Linux. That image is the whole client - both systems, both versions.
    static InjectionStatus LoadNativeEngine(HMODULE hModule);
};

} // namespace SlinkyNative
