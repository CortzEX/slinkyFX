#include "Injector.hpp"
#include "ProcessUtil.hpp"
#include "ConsoleUI.hpp"
#include "Resource.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Slinky {

bool Injector::ExtractBundledDll(std::wstring& outDllPath, std::wstring& outLogPath) {
    // MAX_PATH, not a MAX_PATH buffer: a player's TEMP under a OneDrive or a long profile name overflows the
    // 260-char form of the old API, and the overflow used to be fatal twice over - GetTempPathW answers 0 when
    // the path does not fit, and swprintf_s fast-fails the process instead of returning an error, so the loader
    // died on the way to telling anybody anything. NT paths take 32K; the two buffers are that wide and every
    // result is checked.
    wchar_t tempPath[4096] = { 0 };
    if (!GetTempPathW((DWORD)(sizeof(tempPath) / sizeof(tempPath[0])), tempPath)) {
        ConsoleUI::PrintError("Finding temp folder", GetLastError());
        return false;
    }

    wchar_t slinkyTempDir[4096] = { 0 };
    if (swprintf_s(slinkyTempDir, (sizeof(slinkyTempDir) / sizeof(slinkyTempDir[0])),
                   L"%lsslinky-%lu", tempPath, GetCurrentProcessId()) < 0) {
        ConsoleUI::PrintFailure("Your Windows temp folder path is too long for slinky to use.");
        return false;
    }

    if (!CreateDirectoryW(slinkyTempDir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ConsoleUI::PrintError("Creating temp folder", GetLastError());
        return false;
    }

    outDllPath = std::wstring(slinkyTempDir) + L"\\slinky-native.dll";
    outLogPath = std::wstring(slinkyTempDir) + L"\\slinky-native.log";

    // A stale log is not a log: the monitor matches "FINAL STATUS" anywhere in the file, so one old success line
    // would be read as this run's. If it cannot be removed, the run does not start.
    if (!DeleteFileW(outLogPath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        ConsoleUI::PrintError("Replacing the client log", GetLastError());
        return false;
    }

    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_BUNDLED_DLL), (LPCWSTR)10);
    if (hRes) {
        DWORD resSize = SizeofResource(nullptr, hRes);
        HGLOBAL hLoad = LoadResource(nullptr, hRes);
        if (hLoad && resSize > 0) {
            const void* pData = LockResource(hLoad);
            if (pData) {
                HANDLE hFile = CreateFileW(outDllPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    // A short or failed write used to be treated as success, and the game then tried to load a
                    // truncated DLL - which Windows reports as a broken image, not as the disk-full or
                    // antivirus-blocked reason it really was. Anything but the whole resource goes back to the
                    // folder next to the loader instead.
                    if (WriteFile(hFile, pData, resSize, &written, nullptr) && written == resSize) {
                        CloseHandle(hFile);
                        return true;
                    }
                    CloseHandle(hFile);
                    DeleteFileW(outDllPath.c_str());
                }
            }
        }
    }

    wchar_t exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
            std::wstring localDll = std::wstring(exePath) + L"\\slinky-native.dll";
            if (GetFileAttributesW(localDll.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (CopyFileW(localDll.c_str(), outDllPath.c_str(), FALSE)) {
                    return true;
                }
                outDllPath = localDll;
                outLogPath = std::wstring(exePath) + L"\\slinky-native.log";
                DeleteFileW(outLogPath.c_str());
                return true;
            }
        }
    }

    ConsoleUI::PrintFailure("slinky-native.dll is not next to slinky-loader.exe. Unzip both files into the same folder.");
    return false;
}

namespace {

bool StageFreshCopy(const std::wstring& dllPath, std::wstring& outPath) {
    size_t dot = dllPath.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < 2) return false;
    wchar_t fresh[MAX_PATH * 2] = { 0 };
    if (swprintf_s(fresh, MAX_PATH * 2, L"%.*ls-%lu%ls", (int)dot, dllPath.c_str(),
                   GetTickCount(), dllPath.c_str() + dot) <= 0)
        return false;
    if (!CopyFileW(dllPath.c_str(), fresh, TRUE)) return false;
    outPath = fresh;
    return true;
}

} // namespace

bool Injector::InjectDll(DWORD pid, const std::wstring& dllPath) {
    // A running client is taken out by its own Unload button, never from here: it owns a JVMTI environment,
    // a subclassed window procedure and Java classes that only its engine thread can release, so a remote
    // FreeLibrary would unmap code the game is still calling into.
    if (ProcessUtil::IsSlinkyEngineRunning(pid)) {
        // The engine thread leaves about a second after the client gave the game back. An unload that got
        // that far has already released everything, so waiting beats refusing.
        ConsoleUI::SetStatusText("Waiting for the previous Slinky to leave");
        if (!ProcessUtil::WaitForSlinkyEngineToLeave(pid, 8000)) {
            ConsoleUI::PrintFailure("Slinky is still running in that process. Unload it from the menu first.");
            return false;
        }
    }

    // A 32-bit JVM is not a target this loader can reach: the LoadLibraryW address handed to
    // CreateRemoteThread is the one from our own 64-bit kernel32, and inside a 32-bit process that number is
    // not a function. The thread would run into nothing and the player would wait 30 seconds for a timeout, so
    // say what is wrong - the fix is on the launcher side, and it is a 64-bit Java 8.
    if (ProcessUtil::IsThirtyTwoBit(pid)) {
        ConsoleUI::PrintFailure("That game runs on 32-bit Java. Slinky needs a 64-bit Java - install one and pick it in the launcher.");
        return false;
    }

    std::wstring injectPath = dllPath;
    if (ProcessUtil::IsModuleMappedAt(pid, dllPath)) {
        // A leftover image still holds this exact path, and LoadLibrary would hand that one back without
        // running its entry point, so the new client goes in under a fresh name in the same folder - the
        // engine writes its log next to its own image, which keeps the path the loader watches valid.
        std::wstring fresh;
        if (StageFreshCopy(dllPath, fresh)) injectPath = fresh;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);

    if (!hProcess) {
        DWORD reason = GetLastError();
        if (reason == ERROR_ACCESS_DENIED) {
            // The game was started as administrator and this loader was not: Windows refuses the open, and
            // without saying so the player sees a Windows error number where the answer is one extra right-click.
            ConsoleUI::PrintFailure("The game cannot be opened. If it runs as administrator, start slinky-loader.exe as administrator too.");
        } else {
            ConsoleUI::PrintError("OpenProcess", reason);
        }
        return false;
    }

    SIZE_T pathSizeBytes = (injectPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemotePath = VirtualAllocEx(hProcess, nullptr, pathSizeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!pRemotePath) {
        ConsoleUI::PrintError("Allocating DLL path in game", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pRemotePath, injectPath.c_str(), pathSizeBytes, nullptr)) {
        ConsoleUI::PrintError("Copying DLL path into game", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE pLoadLibraryW = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");

    if (!pLoadLibraryW) {
        ConsoleUI::PrintError("Finding Windows DLL loader", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibraryW, pRemotePath, 0, nullptr);
    if (!hThread) {
        ConsoleUI::PrintError("Starting DLL loader in game", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    DWORD waitRes = WaitForSingleObject(hThread, 30000);
    if (waitRes != WAIT_OBJECT_0) {
        ConsoleUI::PrintFailure("Game did not finish loading the DLL.");
        CloseHandle(hThread);
        // The remote thread is not dead - it is inside LoadLibraryW, reading the path we wrote for it, and a
        // game that takes thirty seconds there is stuck on a loader lock somebody else holds. Freeing that
        // memory here would pull the string out from under a call that is still reading it, so the block stays
        // allocated and goes when the game does. One 400-byte leak in a process that failed to load us is the
        // cheaper of the two mistakes.
        CloseHandle(hProcess);
        return false;
    }

    DWORD dwExitCode = 0;
    GetExitCodeThread(hThread, &dwExitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    // The thread's exit code is the low half of the HMODULE LoadLibraryW returned, and that is not a verdict: a
    // DLL that loaded at a 4-GB-aligned base answers 0 and looks like a failure, and one that died taking an
    // exception back answers its exception code, which looks like success. The module list is the fact, so the
    // number is only believed when the list agrees with it.
    if (dwExitCode == 0 && !ProcessUtil::IsModuleMappedAt(pid, injectPath)) {
        ConsoleUI::PrintFailure("LoadLibraryW failed in game process.");
        return false;
    }

    // The engine thread names its marker before it does anything else, so this is the point where a new
    // client is confirmed - a mapped image would also be true of the leftover one.
    if (!ProcessUtil::WaitForSlinkyEngine(pid, 10000)) {
        ConsoleUI::PrintFailure("No Slinky engine came up in game.");
        return false;
    }

    return true;
}

namespace {

// The console is written in UTF-8 (ConsoleUI asks the codepage for it), so anything that comes out of a wide
// string or goes into a wide one has to cross here.
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// The same three facts the Linux loader gives a player whose run did not finish: where the log is, what the
// client said there, and - when it said nothing at all - that the silence is itself the answer.
void PrintLogTail(const std::wstring& logPath) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, logPath.c_str(), L"r, ccs=UTF-8") != 0 || !fp) {
        std::string where = ToUtf8(logPath);
        fprintf(stderr, "\nNothing was written to %s - the client never got as far as keeping a log.\n",
                where.c_str());
        return;
    }

    std::vector<std::wstring> lines;
    wchar_t line[1024];
    while (fgetws(line, 1024, fp)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len - 1] == L'\n' || line[len - 1] == L'\r')) line[--len] = L'\0';
        if (len > 0) {
            lines.push_back(line);
            if (lines.size() > 30) lines.erase(lines.begin());
        }
    }
    fclose(fp);

    std::string where = ToUtf8(logPath);
    if (lines.empty()) {
        fprintf(stderr, "\nThe log at %s is empty.\n", where.c_str());
        return;
    }
    fprintf(stderr, "\n--- last lines of %s ---\n", where.c_str());
    for (const std::wstring& l : lines) {
        // Best-effort narrow output for the console
        int n = WideCharToMultiByte(CP_UTF8, 0, l.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::vector<char> buf(n);
            WideCharToMultiByte(CP_UTF8, 0, l.c_str(), -1, buf.data(), n, nullptr, nullptr);
            fprintf(stderr, "%s\n", buf.data());
        }
    }
    fprintf(stderr, "--- end of log ---\n");
}

// A run that is working and a run that has stopped look identical while the loader waits: one status line for
// two and a half minutes. The client's log grows as it works, so the newest line it wrote becomes the status.
// Sixty units is a deliberate ceiling - the status is the last row of the block the animation redraws, and a
// status that wraps would leave the redraw counting rows that are no longer there.
void ShowClientProgress(const std::wstring& line, std::string& shown) {
    size_t a = line.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return;
    std::wstring text = line.substr(a, line.find_last_not_of(L" \t\r\n") - a + 1);
    if (text.front() == L'[') {
        size_t close = text.find(L']');
        if (close != std::wstring::npos && close + 2 < text.size()) text = text.substr(close + 2);
    }
    if (text.size() > 60) {
        text.resize(60);
        if (text.back() >= 0xD800 && text.back() <= 0xDBFF) text.pop_back();   // half a surrogate pair
    }
    std::string u8 = ToUtf8(text);
    if (u8.empty() || u8 == shown) return;
    shown = u8;
    ConsoleUI::SetStatusText(shown.c_str());
}

} // namespace

bool Injector::MonitorStartup(DWORD pid, const std::wstring& logPath) {
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    // OpenProcess(SYNCHRONIZE) can be refused where PROCESS_QUERY_LIMITED_INFORMATION is granted. With no handle
    // the death check used to be skipped entirely, so a game that had already gone cost the player the whole
    // budget and then blamed the client for not starting.
    HANDLE hQuery = hProcess ? nullptr : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    auto gameIsGone = [&]() -> bool {
        if (hProcess) return WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0;
        if (!hQuery) return false;
        DWORD code = 0;
        return GetExitCodeProcess(hQuery, &code) != 0 && code != STILL_ACTIVE;
    };
    auto closeHandles = [&]() {
        if (hProcess) { CloseHandle(hProcess); hProcess = nullptr; }
        if (hQuery) { CloseHandle(hQuery); hQuery = nullptr; }
    };

    // The status line stays on "Injecting into Minecraft..." until the client starts writing, and then follows
    // its log - see ShowClientProgress.
    DWORD startTime = GetTickCount();
    // The client waits up to two minutes for the game to reach its client thread, and the old 150 s budget left
    // almost no room on a slow start: failure was declared while the client was still doing its job.
    const DWORD timeoutMs = 210000;

    std::string shown;
    bool announced = false;
    DWORD lastNotice = 0;
    while (GetTickCount() - startTime < timeoutMs) {
        if (gameIsGone()) {
            PrintLogTail(logPath);   // before the failure screen, which ends in a countdown and takes the tail with it
            ConsoleUI::PrintFailure("Game process closed before the client finished starting.");
            closeHandles();
            return false;
        }

        int verdict = 0;   // 0 = still waiting, 1 = running, 2 = already running, 3 = failed
        bool haveLog = false;
        bool logEmpty = true;
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, logPath.c_str(), L"r, ccs=UTF-8") == 0 && fp) {
            haveLog = true;
            wchar_t line[1024];
            std::wstring newest;
            while (fgetws(line, 1024, fp)) {
                std::wstring text(line);
                if (text.find_first_not_of(L" \t\r\n") != std::wstring::npos) {
                    newest = text;   // the last non-blank line, not the last line
                    logEmpty = false;
                }
                if (wcsstr(line, L"FINAL STATUS: RUNNING")) { verdict = 1; break; }
                if (wcsstr(line, L"FINAL STATUS: RELOADED")) { verdict = 1; break; }
                if (wcsstr(line, L"FINAL STATUS: ALREADY_RUNNING")) { verdict = 2; break; }
                if (wcsstr(line, L"FINAL STATUS: FAILED")) { verdict = 3; break; }
            }
            fclose(fp);
            if (!verdict) ShowClientProgress(newest, shown);
        }

        if (verdict) {
            closeHandles();
            if (verdict == 1) return true;
            if (verdict == 2) {
                ConsoleUI::PrintFailure("Already running. Unload from menu first.");
                return false;
            }
            PrintLogTail(logPath);
            ConsoleUI::PrintFailure("Minecraft could not start the client.");
            return false;
        }

        if (!announced) {
            // Injection is finished and the game has said nothing yet: name the state instead of leaving the
            // injection text on screen for minutes, which is what made this look like a hang.
            ConsoleUI::SetStatusText("Waiting for the client to start");
            announced = true;
        }
        DWORD elapsed = GetTickCount() - startTime;
        if (shown.empty() && elapsed >= 10000 && elapsed - lastNotice >= 5000) {
            lastNotice = elapsed;
            char notice[96];
            snprintf(notice, sizeof notice, "Waiting for the client (%lus, %s)", (unsigned long)(elapsed / 1000),
                     !haveLog ? "log missing" : logEmpty ? "log empty" : "log has data");
            ConsoleUI::SetStatusText(notice);
        }

        Sleep(150);
    }

    closeHandles();
    // Evidence first, verdict second: PrintFailure ends in a countdown and the process exits, so a tail printed
    // after it was scrolled away with the screen - the one thing the player could have sent.
    PrintLogTail(logPath);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &fad)) {
        ConsoleUI::PrintFailure("The client never wrote its log, so it never started inside the game.");
    } else if (fad.nFileSizeHigh == 0 && fad.nFileSizeLow == 0) {
        ConsoleUI::PrintFailure("The client opened its log but wrote nothing. It stopped before start-up.");
    } else {
        ConsoleUI::PrintFailure("DLL loaded but the client never confirmed startup.");
    }
    return false;
}

} // namespace Slinky
