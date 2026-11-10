#include "Logger.hpp"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>

static bool g_logOpenFailureReported = false;

// Directory separator: identical literals on Windows, POSIX on Linux builds.
#ifdef _WIN32
#define SLINKY_DIRSEP L"\\"
#define SLINKY_DIRSEP_CH L'\\'
#else
#define SLINKY_DIRSEP L"/"
#define SLINKY_DIRSEP_CH L'/'
#endif

namespace SlinkyNative {

CRITICAL_SECTION Logger::s_CriticalSection;
std::wstring Logger::s_LogFilePath;
bool Logger::s_Initialized = false;

static std::wstring GetModuleFileNameString(HMODULE hModule) {
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD len = 0;
    while ((len = GetModuleFileNameW(hModule, buf.data(), (DWORD)buf.size())) >= buf.size() - 1) {
        if (buf.size() >= 32767) break;
        buf.resize(buf.size() * 2);
    }
    if (len == 0) return std::wstring();
    return std::wstring(buf.data(), len);
}

void Logger::Initialize(HMODULE hModule) {
    if (s_Initialized) return;
    InitializeCriticalSection(&s_CriticalSection);

    std::wstring modulePath = GetModuleFileNameString(hModule);
    if (!modulePath.empty()) {
        size_t lastSlash = modulePath.find_last_of(SLINKY_DIRSEP_CH);
        if (lastSlash != std::wstring::npos) {
            s_LogFilePath = modulePath.substr(0, lastSlash) + SLINKY_DIRSEP + L"slinky-native.log";
        }
    }

    if (s_LogFilePath.empty()) {
        wchar_t tempPath[MAX_PATH] = { 0 };
        GetTempPathW(MAX_PATH, tempPath);
        s_LogFilePath = std::wstring(tempPath) + L"slinky-native.log";
        // The loader watches the file beside this image. This fallback is somewhere else entirely, so a client
        // that lands here writes a log nobody reads - and the loader waits for a line that will never arrive. The
        // one channel left is the game's stderr, which some launchers keep.
        fprintf(stderr, "slinkyFX: cannot locate my own image; the loader will not see this log\n");
    }

    s_Initialized = true;
}

void Logger::Shutdown() {
    if (!s_Initialized) return;
    DeleteCriticalSection(&s_CriticalSection);
    s_Initialized = false;
}

const std::wstring& Logger::GetLogFilePath() {
    return s_LogFilePath;
}

void Logger::Log(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(format, args);
    va_end(args);
}

void Logger::LogV(const wchar_t* format, va_list args) {
    if (!s_Initialized) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t messageBuf[2048] = { 0 };
    vswprintf_s(messageBuf, format, args);

    wchar_t lineBuf[2560] = { 0 };
    swprintf_s(lineBuf, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %ls\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        messageBuf);

    EnterCriticalSection(&s_CriticalSection);

    // Also output to debug string for debugger attach
    OutputDebugStringW(lineBuf);

    // Write to slinky-native.log
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, s_LogFilePath.c_str(), L"a, ccs=UTF-8") == 0 && fp) {
#ifdef _WIN32
        fputws(lineBuf, fp);   // file opened with ccs=UTF-8
#else
        // Convert explicitly: fputws would depend on the global C locale, and
        // changing the locale inside the game process is not acceptable.
        std::string u8 = w32compat::WideToUtf8(lineBuf);
        fwrite(u8.data(), 1, u8.size(), fp);
#endif
        fclose(fp);
    } else if (!g_logOpenFailureReported) {
        // The loader reads this file; a client that cannot write it has to say so somewhere, and stderr is the
        // only channel left. Ignoring the failure is how a full disk, a read-only folder or an ownership mismatch
        // becomes "the client never started".
        g_logOpenFailureReported = true;
        fprintf(stderr, "slinkyFX: cannot write the client log: %s\n", strerror(errno));
    }

    LeaveCriticalSection(&s_CriticalSection);
}

void Logger::LogA(const char* format, ...) {
    va_list args;
    va_start(args, format);

    char charBuf[2048] = { 0 };
    vsprintf_s(charBuf, format, args);
    va_end(args);

    wchar_t wideBuf[2048] = { 0 };
    MultiByteToWideChar(CP_UTF8, 0, charBuf, -1, wideBuf, 2048);
    Log(L"%ls", wideBuf);
}

} // namespace SlinkyNative
