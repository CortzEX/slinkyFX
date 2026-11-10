// Minimal Win32 declarations for the readable reimplementations in src/engine/protected/.
// (The image calls these through the protector's encrypted import slots; here they are ordinary imports.)
//
// On Linux the same prototypes are served by src/platform/linux/win32.cpp, which implements them on POSIX, so the
// engine sources that use them (files.cpp, key_codes_and_path_helpers.cpp) stay byte-identical on both platforms.
// The wide types keep their Windows shape there: wchar_t is 2 bytes (-fshort-wchar), because the paths, the
// UTF-16 literals in .rdata and calloc(0x105, 2) in settings_path_append were recovered from the image that way.
// Path separators are the other image-side habit: the path code joins with L'\\' and POSIX reads '/', so the
// conversion that turns a wide path into a file name folds '\' to '/'. A literal backslash in a Linux file name
// would be lost by that fold; that is the one behaviour the two platforms do not share.
#pragma once
#include <stddef.h>

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
// The decoration the prototypes carry. Empty on both platforms: x64 has one calling convention, these are
// ordinary imports here rather than exports, and the header has to stay readable as one file for both builds.
#define SLINKY_API

typedef void *HANDLE, *HWND, *HMODULE, *HINSTANCE, *LPVOID;
typedef int BOOL;
#ifdef _WIN32
typedef unsigned long DWORD;      // Win32 "long" is 32-bit
#else
typedef unsigned int DWORD;       // on LP64 a long would be 64-bit and change every layout here
#endif
typedef long long LONG_PTR, LRESULT;
typedef unsigned long long ULONG_PTR, WPARAM;
typedef long long LPARAM;
typedef unsigned int UINT;
typedef unsigned short WCHAR;
typedef long long LARGE_INTEGER_QUAD;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef DWORD (*THREAD_START)(LPVOID);

struct FILETIME { DWORD lo, hi; };
struct WIN32_FIND_DATAW { DWORD attributes; FILETIME created, accessed, written; DWORD size_hi, size_lo, reserved0, reserved1; wchar_t name[260]; wchar_t alt_name[14]; };
#define INVALID_HANDLE ((HANDLE)(LONG_PTR)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define INVALID_FILE_SIZE 0xffffffffu
#define ERROR_FILE_NOT_FOUND 2u
// The "I could not read that folder" codes the profile scan has to tell apart. Guarded: a Windows translation
// unit that already saw winerror.h carries these, and the values here are the same numbers.
#ifndef ERROR_PATH_NOT_FOUND
#define ERROR_PATH_NOT_FOUND 3u
#endif
#ifndef ERROR_ACCESS_DENIED
#define ERROR_ACCESS_DENIED 5u
#endif
#ifndef ERROR_BAD_PATHNAME
#define ERROR_BAD_PATHNAME 161u
#endif
#ifndef ERROR_DIRECTORY
#define ERROR_DIRECTORY 267u
#endif
#define ERROR_NO_MORE_FILES 0x12u
#define ERROR_FILE_EXISTS 0x50u
#define ERROR_ALREADY_EXISTS 0xb7u
#define GWLP_WNDPROC (-4)
#define SW_SHOWNORMAL 1
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 0x102u
#define WAIT_FAILED 0xffffffffu
#define INFINITE 0xffffffffu

extern "C" {
SLINKY_API BOOL WINAPI QueryPerformanceCounter(long long *count);
SLINKY_API BOOL WINAPI QueryPerformanceFrequency(long long *freq);
SLINKY_API DWORD WINAPI GetLastError();
SLINKY_API BOOL WINAPI CreateDirectoryW(const wchar_t *path, void *security);
SLINKY_API BOOL WINAPI DeleteFileW(const wchar_t *path);
SLINKY_API HANDLE WINAPI CreateFileW(const wchar_t *path, DWORD access, DWORD share, void *security,
                                                DWORD disposition, DWORD flags, HANDLE templ);
SLINKY_API DWORD WINAPI GetFileSize(HANDLE h, DWORD *high);
SLINKY_API BOOL WINAPI ReadFile(HANDLE h, void *buf, DWORD n, DWORD *done, void *overlapped);
SLINKY_API BOOL WINAPI WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *done, void *overlapped);
SLINKY_API BOOL WINAPI CloseHandle(HANDLE h);
SLINKY_API HANDLE WINAPI FindFirstFileW(const wchar_t *pattern, WIN32_FIND_DATAW *fd);
SLINKY_API BOOL WINAPI FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd);
SLINKY_API BOOL WINAPI FindClose(HANDLE h);
SLINKY_API void WINAPI GetSystemTimeAsFileTime(FILETIME *ft);
SLINKY_API HINSTANCE WINAPI ShellExecuteW(HWND hwnd, const wchar_t *op, const wchar_t *file,
                                                     const wchar_t *params, const wchar_t *dir, int show);
SLINKY_API HMODULE WINAPI GetModuleHandleA(const char *name);
SLINKY_API void *WINAPI GetProcAddress(HMODULE m, const char *name);
SLINKY_API HWND WINAPI GetForegroundWindow();
SLINKY_API BOOL WINAPI IsWindowUnicode(HWND hwnd);
SLINKY_API DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, DWORD *pid);
SLINKY_API DWORD WINAPI GetCurrentProcessId();
SLINKY_API HANDLE WINAPI CreateThread(void *security, size_t stack, THREAD_START start, LPVOID param,
                                                 DWORD flags, DWORD *id);
SLINKY_API HANDLE WINAPI CreateEventW(void *security, BOOL manual_reset, BOOL initial_state, const wchar_t *name);
SLINKY_API BOOL WINAPI SetEvent(HANDLE e);
SLINKY_API DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
SLINKY_API void WINAPI Sleep(DWORD ms);
SLINKY_API void WINAPI FreeLibraryAndExitThread(HMODULE m, DWORD code);
SLINKY_API int WINAPI WideCharToMultiByte(UINT codepage, DWORD flags, const wchar_t *src, int n, char *dst,
                                                 int dst_size, const char *def, BOOL *used_def);
SLINKY_API int WINAPI MultiByteToWideChar(UINT codepage, DWORD flags, const char *src, int n, wchar_t *dst,
                                                 int dst_size);
SLINKY_API HANDLE WINAPI GetStdHandle(DWORD which);
SLINKY_API void WINAPI OutputDebugStringA(const char *text);
SLINKY_API HMODULE WINAPI LoadLibraryA(const char *name);
SLINKY_API BOOL WINAPI DisableThreadLibraryCalls(HMODULE m);
wchar_t *_wgetenv(const wchar_t *name);
}
