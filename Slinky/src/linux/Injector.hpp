#pragma once
// Linux counterpart of the Windows Injector: ptrace-based remote dlopen
// instead of VirtualAllocEx/WriteProcessMemory/CreateRemoteThread(LoadLibraryW).
#include <string>
#include <sys/types.h>

namespace Slinky {

class Injector {
public:
    // Locate libslinky-native.so and libslinky-engine.so - the bootstrap and the C++ client both Minecraft
    // versions are played through - next to the executable, and copy both into a private temp dir (so the game
    // process can dlopen them), returning the bootstrap's path plus the log path the client writes to.
    static bool ExtractBundledSo(std::string& outSoPath, std::string& outLogPath);

    // Load libslinky-native.so into the target with a remote dlopen().
    // If Yama ptrace_scope is set, try to lower it for this session first;
    // no sudo re-launch and no pre-emptive failure. Runs as the invoking
    // user, exactly like the Windows loader.
    static bool InjectDll(pid_t pid, const std::string& soPath);

    // Watch the engine log for a FINAL STATUS line (150s budget, same as
    // Windows) while making sure the game process stays alive.
    static bool MonitorStartup(pid_t pid, const std::string& logPath);
};

} // namespace Slinky
