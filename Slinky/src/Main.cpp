#include <windows.h>
#include "ConsoleUI.hpp"
#include "Injector.hpp"
#include "ProcessUtil.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

using namespace Slinky;

// Titles used by the clients we support (lower-case compare).
static int TitleScore(const std::wstring& title) {
    std::wstring t = title;
    for (auto& ch : t) ch = (wchar_t)towlower(ch);
    int score = 0;
    if (t.find(L"minecraft") != std::wstring::npos) score += 4;
    if (t.find(L"lunar") != std::wstring::npos) score += 4;
    if (t.find(L"badlion") != std::wstring::npos) score += 4;
    if (t.find(L"1.8") != std::wstring::npos) score += 1;
    return score;
}

// Picks the window that really belongs to a Minecraft JVM. Any Java program with a window
// (launchers, IDEs, other tools) used to be treated as the game; now the JVM must have LWJGL
// loaded. After allowFallback is set - a client that does not load lwjgl.dll the way we look for it -
// the fallback still has to see something about Minecraft in the title: a launcher's own JVM, an IDE,
// or any other Java window scores zero, and putting the engine in one of those does nothing for the
// player and leaves a loaded DLL in a program they never meant to touch.
static bool PickTarget(const std::vector<TargetWindow>& windows, bool allowFallback, TargetWindow& out) {
    int best = -1;
    for (const TargetWindow& w : windows) {
        if (!ProcessUtil::IsModuleLoaded(w.pid, L"lwjgl")) continue;
        int score = 10 + TitleScore(w.title);
        if (score > best) { best = score; out = w; }
    }
    if (best >= 0) return true;
    if (!allowFallback || windows.empty()) return false;
    best = 0;
    for (const TargetWindow& w : windows) {
        int score = TitleScore(w.title);
        if (score > best) { best = score; out = w; }
    }
    return best > 0;
}

// --pid <number> - point the loader at a game its own scan is not finding, for symmetry with the Linux loader
// and for the case the scan cannot see: a game running elevated, a custom launcher that renames the window, or
// an instance started before this one.
static bool ParsePidArg(int argc, char** argv, DWORD& outPid) {
    outPid = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") && strcmp(argv[i], "-p")) continue;
        if (i + 1 >= argc) {
            ConsoleUI::PrintFailure("Usage: slinky-loader.exe --pid <process id of the game>");
            return false;
        }
        char* end = nullptr;
        long v = strtol(argv[++i], &end, 10);
        if (v <= 0 || (end && *end)) {
            ConsoleUI::PrintFailure("That is not a process id.");
            return false;
        }
        outPid = (DWORD)v;
    }
    return true;
}

int main(int argc, char** argv) {
    ConsoleUI::Initialize();
    ConsoleUI::StartAnimation();

    DWORD forcedPid = 0;
    if (!ParsePidArg(argc, argv, forcedPid)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    std::wstring dllPath;
    std::wstring logPath;

    if (!Injector::ExtractBundledDll(dllPath, logPath)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    ConsoleUI::SetStatusText("Waiting for Minecraft");

    DWORD startWait = GetTickCount();
    const DWORD timeoutWait = 180000;
    std::vector<TargetWindow> windows;
    TargetWindow target;
    bool found = false;
    size_t javaWindows = 0;   // what the last scan saw, so a failure can say whether to look again

    if (forcedPid) {
        // Checked rather than trusted, with the same fact the scan uses - the process must be a Java one -
        // because putting a library into a program the player never meant to touch is not recoverable.
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, forcedPid);
        wchar_t image[MAX_PATH] = { 0 };
        DWORD len = (DWORD)(sizeof(image) / sizeof(image[0]));
        bool java = h && QueryFullProcessImageNameW(h, 0, image, &len);
        if (h) CloseHandle(h);
        if (java) {
            for (wchar_t& ch : std::wstring(image)) ch = (wchar_t)towlower(ch);
            std::wstring img(image);
            java = img.find(L"java") != std::wstring::npos || img.find(L"javaw") != std::wstring::npos;
        }
        if (!java) {
            ConsoleUI::PrintFailure("That process id is not a running Java game. Start Minecraft, then run: "
                                    "slinky-loader.exe --pid <that process id>");
            ConsoleUI::Shutdown();
            return 1;
        }
        target.pid = forcedPid;
        found = true;
    }

    while (!found && GetTickCount() - startWait < timeoutWait) {
        windows = ProcessUtil::FindMinecraftWindows();
        javaWindows = windows.size();
        if (PickTarget(windows, GetTickCount() - startWait > 20000, target)) {
            found = true;
            break;
        }

        Sleep(500);
    }

    if (!found) {
        if (javaWindows) {
            // Java windows exist and none of them is the game: either we cannot see inside them, which is what
            // an elevated game does to a loader running as the normal user, or they are not games at all.
            ConsoleUI::PrintFailure("Minecraft not found after 3 minutes.");
            ConsoleUI::PrintFailure("Windows that run Java were found but none looked like the game. If the game "
                                    "is running as administrator, start slinky-loader.exe as administrator too; "
                                    "otherwise start the game before running this.");
        } else {
            ConsoleUI::PrintFailure("Minecraft not found after 3 minutes.");
        }
        ConsoleUI::Shutdown();
        return 1;
    }

    ConsoleUI::SetPhase(ConsoleUI::Phase::Injecting);
    ConsoleUI::SetStatusText("Injecting into Minecraft");

    if (!Injector::InjectDll(target.pid, dllPath)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    bool success = Injector::MonitorStartup(target.pid, logPath);
    if (!success) {
        ConsoleUI::Shutdown();
        return 1;
    }

    // Success ending, exactly like the original loader: DONE header, then
    // "Injected successfully. Loader will close in 5s." counting down before
    // the window closes itself.
    ConsoleUI::FinishSuccess();

    ConsoleUI::Shutdown();
    return 0;
}
