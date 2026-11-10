#pragma once
// Linux counterpart of the Windows ProcessUtil: /proc scanning instead of
// Toolhelp snapshots, X11 window titles instead of EnumWindows.
#include <string>
#include <vector>
#include <sys/types.h>

namespace Slinky {

struct TargetWindow {
    pid_t pid = 0;
    std::string title;      // X11 window title, or cmdline hint if X11 had nothing - short, for the list
    // The whole evidence behind the guess: for a command-line candidate this is the complete cmdline, not the
    // 200-char display string. A game started by Prism or the vanilla launcher carries its version and its
    // lwjgl jars past character 200, so scoring on the display string was scoring on the JVM's options alone.
    std::string hint;
    bool fromTitle = false; // true when the title came from an actual X11 window
};

class ProcessUtil {
public:
    // PIDs whose process is a JVM (comm/cmdline says java), like the
    // javaw.exe/java.exe enumeration on Windows.
    static std::vector<pid_t> GetJavaProcessIds();

    // Java processes that own a visible, titled X11 window.  If X11 yields
    // nothing (no DISPLAY, headless), falls back to synthetic entries built
    // from /proc/<pid>/cmdline so scanning still works.
    static std::vector<TargetWindow> FindMinecraftWindows();

    // True when /proc/<pid>/maps mentions moduleName (case-insensitive),
    // mirroring the module snapshot check on Windows.
    static bool IsModuleLoaded(pid_t pid, const char* moduleName);

    static bool WaitForModuleLoad(pid_t pid, const char* moduleName, unsigned timeoutMs);
};

} // namespace Slinky
