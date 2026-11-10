// ---------------------------------------------------------------------------
// w32/windows.h - minimal Win32 compatibility layer for building the
// SlinkyNative bootstrap on Linux.  It is only added to the include path on
// non-Windows builds, so `#include <windows.h>` in the (otherwise portable)
// JNI/JVMTI sources resolves here instead.  Windows builds never see it.
//
// Only the subset actually used by SlinkyNative is implemented.  Semantics
// follow the Win32 originals closely enough that the shared sources compile
// and behave identically (wide strings are UTF-32 on Linux, the log file is
// written as UTF-8 like the `ccs=UTF-8` fopen mode on Windows).
// ---------------------------------------------------------------------------
#ifndef SLINKY_W32_COMPAT_WINDOWS_H
#define SLINKY_W32_COMPAT_WINDOWS_H

#ifdef _WIN32
// Never used on Windows (this directory is not on the Windows include path),
// but stay safe if someone adds it.
#include <windows.h>
#else

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <wchar.h>

// ---------------------------------------------------------------------------
// Base types
// ---------------------------------------------------------------------------
typedef unsigned long DWORD;      // wide enough for %lu formatting parity
typedef unsigned short WORD;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef int BOOL;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* LPVOID;
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef unsigned char BYTE;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define WINAPI
#define CALLBACK
#define MAX_PATH 4096
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define GENERIC_WRITE 0x40000000
#define CREATE_ALWAYS 2
#define CP_UTF8 65001

#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0x4  // glibc value; hidden without _GNU_SOURCE in some TUs
#endif

// ---------------------------------------------------------------------------
// Time / process helpers
// ---------------------------------------------------------------------------
inline void Sleep(unsigned long ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry remainder */ }
}

inline ULONGLONG GetTickCount64() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ULONGLONG)ts.tv_sec * 1000ULL + (ULONGLONG)(ts.tv_nsec / 1000000L);
}

inline DWORD GetTickCount() { return (DWORD)GetTickCount64(); }

inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }

inline DWORD GetLastError() { return (DWORD)errno; }

// ---------------------------------------------------------------------------
// UTF conversions (wide = UTF-32 on Linux)
// ---------------------------------------------------------------------------
namespace w32compat {

inline std::string WideToUtf8(const wchar_t* src, size_t n) {
    std::string out;
    out.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = (uint32_t)src[i];
        if (c == 0) break;
        // Combine surrogate pairs when present (input normally UTF-32, be lenient)
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n &&
            src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint32_t)src[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) {
            out.push_back((char)c);
        } else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (c >> 18)));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

inline std::wstring Utf8ToWide(const char* src, size_t n) {
    std::wstring out;
    out.reserve(n);
    size_t i = 0;
    while (i < n) {
        unsigned char ch = (unsigned char)src[i];
        uint32_t cp = 0xFFFD;
        int extra = 0;
        if (ch < 0x80) { cp = ch; extra = 0; }
        else if ((ch & 0xE0) == 0xC0) { cp = ch & 0x1F; extra = 1; }
        else if ((ch & 0xF0) == 0xE0) { cp = ch & 0x0F; extra = 2; }
        else if ((ch & 0xF8) == 0xF0) { cp = ch & 0x07; extra = 3; }
        else { ++i; out.push_back((wchar_t)0xFFFD); continue; }
        if (i + (size_t)extra >= n + 0 && extra > 0 && i + extra >= n) {
            out.push_back((wchar_t)0xFFFD);
            break;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = (unsigned char)src[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back((wchar_t)0xFFFD); ++i; continue; }
        i += (size_t)extra + 1;
        // Reject overlong / out-of-range crud, leniently
        if (cp > 0x10FFFF) cp = 0xFFFD;
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        out.push_back((wchar_t)cp);
    }
    return out;
}

inline std::string WideToUtf8(const wchar_t* s) { return WideToUtf8(s, wcslen(s)); }
inline std::wstring Utf8ToWide(const char* s) { return Utf8ToWide(s, strlen(s)); }
inline std::string WideToUtf8(const std::wstring& s) { return WideToUtf8(s.c_str(), s.size()); }
inline std::wstring Utf8ToWide(const std::string& s) { return Utf8ToWide(s.c_str(), s.size()); }

} // namespace w32compat

// ---------------------------------------------------------------------------
// Wide/narrow API shims (UTF-8 only, which is all SlinkyNative uses)
// ---------------------------------------------------------------------------
inline int WideCharToMultiByte(unsigned /*cp*/, unsigned /*flags*/,
                               const wchar_t* src, int srcLen,
                               char* dst, int dstLen,
                               const char* /*defaultChar*/, int* /*usedDefault*/) {
    if (!src) return 0;
    size_t n = (srcLen < 0) ? wcslen(src) : (size_t)srcLen;
    std::string u8 = w32compat::WideToUtf8(src, n);
    int need = (int)u8.size() + (srcLen < 0 ? 1 : 0);  // -1 counts the terminator
    if (!dst || dstLen == 0) return need;
    int copy = (int)u8.size();
    if (copy > dstLen - 1) copy = dstLen - 1;
    memcpy(dst, u8.data(), (size_t)copy);
    dst[copy] = '\0';
    return copy;
}

inline int MultiByteToWideChar(unsigned /*cp*/, unsigned /*flags*/,
                               const char* src, int srcLen,
                               wchar_t* dst, int dstLen) {
    if (!src) return 0;
    size_t n = (srcLen < 0) ? strlen(src) : (size_t)srcLen;
    std::wstring w = w32compat::Utf8ToWide(src, n);
    int need = (int)w.size() + (srcLen < 0 ? 1 : 0);
    if (!dst || dstLen == 0) return need;
    int copy = (int)w.size();
    if (copy > dstLen - 1) copy = dstLen - 1;
    if (copy > 0) memcpy(dst, w.data(), (size_t)copy * sizeof(wchar_t));
    dst[copy] = L'\0';
    return copy;
}

// ---------------------------------------------------------------------------
// Critical sections -> recursive pthread mutexes (Win32 ones are recursive)
// ---------------------------------------------------------------------------
typedef pthread_mutex_t CRITICAL_SECTION;

inline void InitializeCriticalSection(CRITICAL_SECTION* cs) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs, &attr);
    pthread_mutexattr_destroy(&attr);
}
inline void EnterCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_lock(cs); }
inline void LeaveCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_unlock(cs); }
inline void DeleteCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_destroy(cs); }

// ---------------------------------------------------------------------------
// SYSTEMTIME / local time
// ---------------------------------------------------------------------------
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay;
    WORD wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

inline void GetLocalTime(SYSTEMTIME* st) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    st->wYear = (WORD)(tmv.tm_year + 1900);
    st->wMonth = (WORD)(tmv.tm_mon + 1);
    st->wDayOfWeek = (WORD)tmv.tm_wday;
    st->wDay = (WORD)tmv.tm_mday;
    st->wHour = (WORD)tmv.tm_hour;
    st->wMinute = (WORD)tmv.tm_min;
    st->wSecond = (WORD)tmv.tm_sec;
    st->wMilliseconds = (WORD)(ts.tv_nsec / 1000000L);
}

// ---------------------------------------------------------------------------
// Console debug output (no-op on Linux; journald/stderr is not its job)
// ---------------------------------------------------------------------------
inline void OutputDebugStringW(const wchar_t*) { }

// ---------------------------------------------------------------------------
// printf-family wrappers.  glibc's swprintf/vswprintf take the buffer size.
// (Verified: glibc supports %hs and %ls in wide formatting like MSVC.)
// ---------------------------------------------------------------------------
#define swprintf_s(buf, ...) swprintf((buf), sizeof(buf) / sizeof((buf)[0]), __VA_ARGS__)
#define vswprintf_s(buf, fmt, args) vswprintf((buf), sizeof(buf) / sizeof((buf)[0]), (fmt), (args))
#define vsprintf_s(buf, fmt, args) vsnprintf((buf), sizeof(buf), (fmt), (args))

inline int _wfopen_s(FILE** fp, const wchar_t* wpath, const wchar_t* wmode) {
    *fp = nullptr;
    std::string path = w32compat::WideToUtf8(wpath);
    std::string mode = w32compat::WideToUtf8(wmode);
    // Strip the MSVC ", ccs=UTF-8" suffix if present (UTF-8 is the native default here)
    size_t ccs = mode.find(", ccs=");
    if (ccs != std::string::npos) mode = mode.substr(0, ccs);
    FILE* f = fopen(path.c_str(), mode.c_str());
    if (!f) return errno ? errno : 1;
    *fp = f;
    return 0;
}

inline int _wcsicmp(const wchar_t* a, const wchar_t* b) {
    for (; *a && *b; ++a, ++b) {
        wchar_t ca = *a, cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
        if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    return (*a == *b) ? 0 : (*a ? 1 : -1);
}

// ---------------------------------------------------------------------------
// Module introspection.  The path of a module is read from the dynamic linker,
// which is the one thing in the process that knows which file an address came
// from; a scan of /proc/self/maps has to guess which segment of the file an
// address sits in and loses when the linker splits the segment that carries the
// library's own data.
// ---------------------------------------------------------------------------
namespace w32compat {

inline bool ImagePath(void* inside, std::string& out) {
    Dl_info info;
    if (!dladdr(inside, &info) || !info.dli_fname) return false;
    out = info.dli_fname;
    return !out.empty();
}

// This library - the file libslinky-native.so was loaded from, never the game's java executable.
inline bool SelfPath(std::string& out) { return ImagePath((void*)&SelfPath, out); }

} // namespace w32compat

inline DWORD GetModuleFileNameW(HMODULE hModule, wchar_t* buf, DWORD bufChars) {
    if (!buf || bufChars == 0) return 0;
    // A Win32 HMODULE is a module's base address, and dladdr takes any address inside the image it belongs
    // to, so the handle carries over as it is. Without one, Win32 would name the executable; here it names
    // this library, because every caller in the bootstrap wants the folder the library was loaded from.
    std::string path;
    if (!(hModule ? w32compat::ImagePath((void*)hModule, path) : w32compat::SelfPath(path))) return 0;
    std::wstring w = w32compat::Utf8ToWide(path);
    DWORD n = (DWORD)((w.size() < bufChars) ? w.size() : bufChars - 1);
    memcpy(buf, w.data(), n * sizeof(wchar_t));
    buf[n] = L'\0';
    return n;
}

inline DWORD GetTempPathW(DWORD bufChars, wchar_t* buf) {
    // Windows: DWORD GetTempPathW(DWORD nBufferLength, LPTSTR lpBuffer)
    const wchar_t* tmp = L"/tmp/";
    DWORD n = 5;
    if (buf && bufChars > n) {
        memcpy(buf, tmp, (n + 1) * sizeof(wchar_t));
        return n;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Module/symbol lookup.  GetModuleHandleW(L"jvm.dll") maps to "already loaded
// libjvm.so" (RTLD_NOLOAD) - the game process always has the JVM loaded.
// ---------------------------------------------------------------------------
inline HMODULE GetModuleHandleW(const wchar_t* /*name*/) {
    void* h = dlopen("libjvm.so", RTLD_LAZY | RTLD_NOLOAD);
    if (h) return h;
    // Fall back to the path recorded in maps if the soname lookup missed.
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return nullptr;
    char line[4096];
    std::string found;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libjvm.so")) continue;
        // The path is the last field and the only one that holds a slash: the permissions use dashes and
        // the device uses a colon. Taking the slash after the name would look for one inside a file name.
        char* slash = strchr(line, '/');
        if (!slash) continue;
        char* nl = strchr(slash, '\n');
        found.assign(slash, nl ? (size_t)(nl - slash) : strlen(slash));
        break;
    }
    fclose(f);
    if (found.empty()) return nullptr;
    return dlopen(found.c_str(), RTLD_LAZY | RTLD_NOLOAD);
}

inline void* GetProcAddress(HMODULE module, const char* name) {
    if (!module) return nullptr;
    return dlsym(module, name);
}

#endif // !_WIN32
#endif // SLINKY_W32_COMPAT_WINDOWS_H
