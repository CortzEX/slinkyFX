#include "ProcessUtil.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <cstring>
#include <cwchar>

namespace Slinky {

std::vector<DWORD> ProcessUtil::GetJavaProcessIds() {
    std::vector<DWORD> pids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"javaw.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"java.exe") == 0)
            {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return pids;
}

struct EnumData {
    std::vector<DWORD> javaPids;
    std::vector<TargetWindow> windows;
};

static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    if (!IsWindowVisible(hWnd)) return TRUE;
    if (GetWindowTextLengthW(hWnd) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);

    EnumData* data = (EnumData*)lParam;
    if (std::find(data->javaPids.begin(), data->javaPids.end(), pid) == data->javaPids.end()) {
        return TRUE;
    }

    wchar_t titleBuf[512] = { 0 };
    if (GetWindowTextW(hWnd, titleBuf, 512) > 0) {
        TargetWindow tw;
        tw.hWnd = hWnd;
        tw.pid = pid;
        tw.title = titleBuf;
        data->windows.push_back(tw);
    }

    return TRUE;
}

std::vector<TargetWindow> ProcessUtil::FindMinecraftWindows() {
    EnumData data;
    data.javaPids = GetJavaProcessIds();
    if (data.javaPids.empty()) return {};

    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    return data.windows;
}

bool ProcessUtil::IsModuleLoaded(DWORD pid, const wchar_t* moduleName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

    bool found = false;
    if (Module32FirstW(hSnap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0 ||
                wcsstr(me.szExePath, moduleName) != nullptr)
            {
                found = true;
                break;
            }
        } while (Module32NextW(hSnap, &me));
    }

    CloseHandle(hSnap);
    return found;
}

bool ProcessUtil::IsModuleMappedAt(DWORD pid, const std::wstring& fullPath) {
    wchar_t wanted[MAX_PATH] = { 0 };
    if (!GetFullPathNameW(fullPath.c_str(), MAX_PATH, wanted, nullptr)) return false;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

    bool found = false;
    if (Module32FirstW(hSnap, &me)) {
        do {
            if (_wcsicmp(me.szExePath, wanted) == 0) { found = true; break; }
        } while (Module32NextW(hSnap, &me));
    }

    CloseHandle(hSnap);
    return found;
}

bool ProcessUtil::IsThirtyTwoBit(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    BOOL underWow64 = FALSE;
    BOOL ok = IsWow64Process(hProcess, &underWow64);

    CloseHandle(hProcess);
    return ok && underWow64 == TRUE;
}

namespace {

// The engine names a kernel object after the game's pid as the first thing its engine thread does and lets
// it go when that thread leaves: the marker is the engine saying "this process is mine".
void EngineMarkerName(DWORD pid, wchar_t* out, size_t outChars) {
    swprintf(out, outChars, L"Local\\slinky-engine-%lu", pid);
}

// Older clients publish no marker. Their engine thread is still created inside slinky-native, and it is the
// only thread the DLL ever starts, so a thread whose start address falls in one of those images is running.
bool HasEngineThreadInSlinky(DWORD pid) {
    struct Range { BYTE* base; BYTE* end; };
    Range ranges[8];
    int count = 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    if (Module32FirstW(hSnap, &me)) {
        do {
            if (_wcsnicmp(me.szModule, L"slinky-native", 13) != 0) continue;
            if (count == 8) break;
            ranges[count].base = me.modBaseAddr;
            ranges[count].end = me.modBaseAddr + me.modBaseSize;
            count++;
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);
    if (!count) return false;

    // The toolhelp thread record carries no start address, so ntdll is asked for it per thread.
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    typedef long(__stdcall *NtQueryThread)(HANDLE, UINT, void*, unsigned long, unsigned long*);
    NtQueryThread query = (NtQueryThread)(void*)GetProcAddress(ntdll, "NtQueryInformationThread");
    if (!query) return false;

    bool running = false;
    HANDLE hThreads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreads == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(hThreads, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            // The start address class answers only to THREAD_QUERY_INFORMATION; the limited right (0x8) on its
            // own fails with STATUS_DATATYPE_MISALIGNMENT, which would read as "no engine thread".
            HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
            if (!hThread) continue;
            void* start = nullptr;
            // 9 = ThreadQuerySetWin32StartAddress
            bool known = query(hThread, 9, &start, sizeof(start), nullptr) == 0 && start != nullptr;
            CloseHandle(hThread);
            if (!known) continue;
            for (int i = 0; i < count; i++) {
                if ((BYTE*)start >= ranges[i].base && (BYTE*)start < ranges[i].end) {
                    running = true;
                    break;
                }
            }
            if (running) break;
        } while (Thread32Next(hThreads, &te));
    }
    CloseHandle(hThreads);
    return running;
}

bool PollUntil(bool (*probe)(DWORD), DWORD pid, DWORD timeoutMs, bool wanted) {
    for (DWORD elapsed = 0; ; elapsed += 100) {
        if (probe(pid) == wanted) return true;
        if (elapsed >= timeoutMs) return false;
        Sleep(100);
    }
}

bool MarkerProbe(DWORD pid) {
    wchar_t name[64];
    EngineMarkerName(pid, name, 64);
    HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!h) return false;
    CloseHandle(h);
    return true;
}

} // namespace

bool ProcessUtil::IsSlinkyEngineRunning(DWORD pid) {
    return MarkerProbe(pid) || HasEngineThreadInSlinky(pid);
}

bool ProcessUtil::WaitForSlinkyEngineToLeave(DWORD pid, DWORD timeoutMs) {
    return PollUntil(&ProcessUtil::IsSlinkyEngineRunning, pid, timeoutMs, false);
}

bool ProcessUtil::WaitForSlinkyEngine(DWORD pid, DWORD timeoutMs) {
    return PollUntil(&ProcessUtil::IsSlinkyEngineRunning, pid, timeoutMs, true);
}

} // namespace Slinky
