#pragma once

#include <windows.h>
#include <string>

namespace SlinkyNative {

class Logger {
public:
    static void Initialize(HMODULE hModule);
    static void Shutdown();

    // Log a wide string message with timestamp formatted identically to original Slinky
    static void Log(const wchar_t* format, ...);
    static void LogV(const wchar_t* format, va_list args);

    // Helper for char format strings
    static void LogA(const char* format, ...);

    static const std::wstring& GetLogFilePath();

private:
    static CRITICAL_SECTION s_CriticalSection;
    static std::wstring s_LogFilePath;
    static bool s_Initialized;
};

} // namespace SlinkyNative
