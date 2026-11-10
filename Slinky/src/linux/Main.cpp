// ---------------------------------------------------------------------------
// Main.cpp (Linux) - same flow as the Windows loader:
//   extract slinky-native -> scan for the game (3 min budget, 20 s before the
//   lwjgl-less fallback kicks in) -> inject -> watch the log for FINAL STATUS.
// ---------------------------------------------------------------------------
#include "ConsoleUI.hpp"
#include "Injector.hpp"
#include "ProcessUtil.hpp"
#include "Compat.hpp"

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Slinky;

// Titles used by the clients we support (lower-case compare) - same keywords
// as the Windows build. Scored on the whole evidence behind a candidate, not the short string shown in the list:
// a game started by Prism or the vanilla launcher carries its version name and its libraries well past the first
// two hundred characters of its command line.
static int TitleScore(const std::string& title) {
    std::string t = title;
    for (auto& ch : t) ch = (char)tolower((unsigned char)ch);
    int score = 0;
    if (t.find("minecraft") != std::string::npos) score += 4;
    if (t.find("lunar") != std::string::npos) score += 4;
    if (t.find("badlion") != std::string::npos) score += 4;
    if (t.find("1.8") != std::string::npos) score += 1;
    return score;
}

static const std::string& Evidence(const TargetWindow& w) {
    return w.hint.empty() ? w.title : w.hint;
}

// The proof that a Java process is playing the game rather than launching it. The game's own libraries (lwjgl),
// its own classes (net.minecraft) or a mod loader's (cpw.mods, fml) can only be on a game's command line, and the
// two branded clients are their own distribution - Lunar and Badlion hand their client jar to the JVM and load
// LWJGL out of it, so their command lines carry neither of the first two markers. A launcher's own JVM carries the
// word "minecraft" and the ~/.minecraft folder in its branding properties, which is exactly why the keywords in
// TitleScore are not enough on their own to decide where to inject.
static bool LooksLikeGame(const std::string& hay) {
    std::string t = hay;
    for (auto& ch : t) ch = (char)tolower((unsigned char)ch);
    static const char* const kMarkers[] = {
        "lwjgl", "net.minecraft", "cpw.mods", "fml", "lunarclient", "badlion",
    };
    for (const char* m : kMarkers) {
        if (t.find(m) != std::string::npos) return true;
    }
    return false;
}

// Picks the candidate that really belongs to a Minecraft JVM: LWJGL must be
// loaded in the process (the Linux twin of the module check on Windows).
// After allowFallback is set - a client that does not have lwjgl in its maps the way we look for it - the
// fallback still has to see something about Minecraft in the window name. A Gradle daemon, an IDE, the
// launcher's own JVM and every other Java process score zero: putting the engine into one of those does
// nothing for the player and leaves a loaded library in a program they never meant to touch.
static bool PickTarget(const std::vector<TargetWindow>& windows, bool allowFallback,
                       TargetWindow& out) {
    int best = -1;
    for (const TargetWindow& w : windows) {
        if (!ProcessUtil::IsModuleLoaded(w.pid, "lwjgl")) continue;
        int score = 10 + TitleScore(Evidence(w));
        if (score > best) { best = score; out = w; }
    }
    if (best >= 0) return true;
    if (!allowFallback || windows.empty()) return false;

    best = 0;
    for (const TargetWindow& w : windows) {
        if (!LooksLikeGame(Evidence(w))) continue;
        int score = TitleScore(Evidence(w));
        if (score > best) { best = score; out = w; }
    }
    return best > 0;
}

// --pid <number> - point the loader at a game it is not finding on its own.
// Detection on this platform reads /proc, and a Wayland client that gives the compositor no window title, an
// instance started before the loader, or a launcher that renames its own process can all leave a perfectly good
// game invisible to the scan. The player can always see the number themselves (ps, top, the launcher's own
// log line), so this makes the one failure we cannot diagnose from here fixable without a rebuild.
static bool ParsePidArg(int argc, char** argv, pid_t& outPid) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") && strcmp(argv[i], "-p")) continue;
        if (i + 1 >= argc) {
            ConsoleUI::PrintFailure("Usage: slinky-loader --pid <process id of the game>");
            return false;
        }
        char* end = nullptr;
        long v = strtol(argv[++i], &end, 10);
        if (v <= 0 || (end && *end)) {
            ConsoleUI::PrintFailure("That is not a process id.");
            return false;
        }
        outPid = (pid_t)v;
        return true;
    }
    return true;   // no --pid given: normal scanning
}

int main(int argc, char** argv) {
    ConsoleUI::Initialize();
    ConsoleUI::StartAnimation();

    pid_t forcedPid = -1;
    if (!ParsePidArg(argc, argv, forcedPid)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    std::string soPath;
    std::string logPath;
    if (!Injector::ExtractBundledSo(soPath, logPath)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    ConsoleUI::SetStatusText("Waiting for Minecraft");

    unsigned long long startWait = GetTickCount64();
    const unsigned long long timeoutWait = 180000;
    TargetWindow target;
    bool found = false;

    if (forcedPid > 0) {
        // Checked rather than trusted: injecting into the wrong process leaves a library inside a program the
        // player never meant to touch. The same JVM test the scan uses, so --pid cannot point at a shell.
        bool java = false;
        for (pid_t p : ProcessUtil::GetJavaProcessIds()) if (p == forcedPid) { java = true; break; }
        if (!java) {
            std::string msg = "Process " + std::to_string((long)forcedPid) +
                              " is not a running Java game. Start Minecraft, then run: "
                              "slinky-loader --pid <that process id>";
            ConsoleUI::PrintFailure(msg.c_str());
            ConsoleUI::Shutdown();
            return 1;
        }
        target.pid = forcedPid;
        target.title = "";
        found = true;
    }

    while (!found && GetTickCount64() - startWait < timeoutWait) {
        std::vector<TargetWindow> windows = ProcessUtil::FindMinecraftWindows();
        if (PickTarget(windows, GetTickCount64() - startWait > 20000, target)) {
            found = true;
            break;
        }

        Sleep(500);
    }

    if (!found) {
        ConsoleUI::PrintFailure("Minecraft not found after 3 minutes.");
        ConsoleUI::Shutdown();
        return 1;
    }

    ConsoleUI::SetPhase(ConsoleUI::Phase::Injecting);
    ConsoleUI::SetStatusText("Injecting into Minecraft");

    if (!Injector::InjectDll(target.pid, soPath)) {
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
