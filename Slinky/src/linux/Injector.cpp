// ---------------------------------------------------------------------------
// Injector.cpp (Linux)
//
// Counterpart of the Windows CreateRemoteThread(LoadLibraryW) injector.
//
// Windows:  OpenProcess -> VirtualAllocEx -> WriteProcessMemory(dll path)
//           -> CreateRemoteThread(LoadLibraryW) -> wait -> check module list.
// Linux:    attach every thread with ptrace -> hijack one thread so it calls
//           dlopen(path, RTLD_NOW) directly (path planted on its stack) ->
//           trap on return (int3) -> restore everything -> check
//           /proc/<pid>/maps for the library.
//
// Yama (kernel.yama.ptrace_scope > 0) blocks attaching to non-child
// processes. The loader runs as the invoking user: it tries to relax the
// scope for the session, then proceeds. No sudo re-launch is performed.
// ---------------------------------------------------------------------------
#include "Injector.hpp"
#include "ProcessUtil.hpp"
#include "ConsoleUI.hpp"
#include "Compat.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdarg>
#include <csignal>
#include <sys/syscall.h>
#include <string>
#include <vector>
#include <algorithm>

#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <elf.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0x4
#endif

#define RTLD_NOW_FLAGS 2  // RTLD_NOW

#if !defined(__x86_64__)
#error "The Linux loader currently supports x86_64 only."
#endif

namespace Slinky {

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
bool ReadWholeFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

std::string Dirname(const std::string& p) {
    size_t slash = p.rfind('/');
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

bool ProcessAlive(pid_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
}

int ReadPtraceScope() {
    std::string s;
    if (!ReadWholeFile("/proc/sys/kernel/yama/ptrace_scope", s)) return -1;  // Yama absent
    return atoi(s.c_str());
}

unsigned long long NamespaceId(pid_t pid, const char* ns) {
    struct stat st;
    if (stat(("/proc/" + std::to_string((long)pid) + "/ns/" + ns).c_str(), &st) != 0) return 0;
    return (unsigned long long)st.st_ino;
}

long RealUidOf(pid_t pid) {
    std::string s;
    if (!ReadWholeFile("/proc/" + std::to_string((long)pid) + "/status", s)) return -1;
    size_t at = s.find("Uid:");
    if (at == std::string::npos) return -1;
    return atol(s.c_str() + at + 4);   // the real uid is the first of the four fields
}

// "Permission denied" is one errno behind four different problems on Linux, and the command that fixes one of
// them does nothing for the other three. All four are answerable from /proc without asking the player anything:
// the Yama setting, whose the game is compared to ours, whether the game sits in its own namespaces (a Flatpak or
// Snap launcher - a host process cannot reach into it at all), and that a security module gives the same errno as
// a debugger already holding the process.
std::string PtraceRefusalReason(pid_t pid) {
    std::vector<std::string> parts;

    int scope = ReadPtraceScope();
    if (scope > 0) {
        parts.push_back("the kernel restricts debugging to a program's own children (ptrace_scope=" +
                        std::to_string(scope) + ") - run:  sudo sysctl kernel.yama.ptrace_scope=0");
    }

    long gameUid = RealUidOf(pid);
    if (gameUid >= 0 && gameUid != (long)getuid()) {
        parts.push_back("the game runs as user " + std::to_string(gameUid) + " and this loader as user " +
                        std::to_string((long)getuid()) + " - start both the same way; if the game was started with "
                        "sudo, the loader needs sudo too");
    }

    static const char* const kNamespaces[] = { "pid", "mnt" };
    for (const char* ns : kNamespaces) {
        unsigned long long mine = NamespaceId(getpid(), ns);
        unsigned long long theirs = NamespaceId(pid, ns);
        if (mine && theirs && mine != theirs) {
            parts.push_back(std::string("the game is inside a sandbox with its own ") + ns +
                            " namespace (a Flatpak or Snap launcher) - the loader has to run inside that same "
                            "sandbox, outside it Linux will always refuse");
            break;
        }
    }

    if (parts.empty()) {
        parts.push_back("a security module is refusing it (SELinux on Fedora, AppArmor on Ubuntu) or another "
                        "program such as gdb is already attached to the game");
    }

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += "; ";
        out += parts[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// /proc/<pid>/maps parsing
// ---------------------------------------------------------------------------
struct MapEntry {
    unsigned long long start = 0;
    unsigned long long end = 0;
    unsigned long long pgoff = 0;
    std::string path;
};

bool ParseMaps(pid_t pid, std::vector<MapEntry>& out) {
    std::string maps;
    if (!ReadWholeFile("/proc/" + std::to_string((long)pid) + "/maps", maps)) return false;
    out.clear();
    size_t pos = 0;
    while (pos < maps.size()) {
        size_t eol = maps.find('\n', pos);
        if (eol == std::string::npos) eol = maps.size();
        std::string line = maps.substr(pos, eol - pos);
        pos = eol + 1;
        MapEntry m;
        char perms[8] = { 0 };
        char path[1024] = { 0 };
        int n = sscanf(line.c_str(), "%llx-%llx %7s %llx %*s %*s %1023[^\n]",
                       &m.start, &m.end, perms, &m.pgoff, path);
        if (n < 4) continue;
        if (n == 5) {
            const char* p = path;
            while (*p == ' ') ++p;
            m.path = p;
        }
        out.push_back(m);
    }
    return true;
}

// First PT_LOAD-backed mapping (lowest file offset) of a shared object gives
// us the load bias: bias = mapStart - (p_vaddr & ~0xfff).
bool LibraryLoadBias(const std::vector<MapEntry>& maps, const std::string& file,
                     unsigned long long& outBias) {
    const MapEntry* best = nullptr;
    for (const auto& m : maps) {
        if (m.path != file) continue;
        if (!best || m.pgoff < best->pgoff) best = &m;
    }
    if (!best) return false;

    FILE* f = fopen(file.c_str(), "rb");
    if (!f) return false;
    Elf64_Ehdr eh;
    bool ok = fread(&eh, 1, sizeof(eh), f) == sizeof(eh) &&
              memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 &&
              eh.e_ident[EI_CLASS] == ELFCLASS64;
    if (ok) {
        for (int i = 0; ok && i < eh.e_phnum; ++i) {
            Elf64_Phdr ph;
            if (fseek(f, (long)(eh.e_phoff + (unsigned long)i * eh.e_phentsize), SEEK_SET) != 0 ||
                fread(&ph, 1, sizeof(ph), f) != sizeof(ph)) {
                ok = false;
                break;
            }
            if (ph.p_type == PT_LOAD && (ph.p_offset & ~0xFFFULL) == best->pgoff) {
                outBias = best->start - (ph.p_vaddr & ~0xFFFULL);
                fclose(f);
                return true;
            }
        }
    }
    fclose(f);
    if (!ok) return false;
    outBias = best->start - best->pgoff;  // typical layout: p_vaddr == p_offset
    return true;
}

// ---------------------------------------------------------------------------
// ELF dynamic-symbol lookup ("dlopen" / "dlclose" in the target's own libc)
// The tables are scanned linearly: the hash buckets would only save time, and a
// wrong count derived from them is how a symbol gets missed.
// ---------------------------------------------------------------------------
template <typename T>
bool ReadAt(FILE* f, long off, T& out) {
    return fseek(f, off, SEEK_SET) == 0 && fread(&out, 1, sizeof(T), f) == sizeof(T);
}

bool ResolveSymbolInFile(const std::string& file, const char* symName,
                         unsigned long long bias, unsigned long long& outAddr) {
    FILE* f = fopen(file.c_str(), "rb");
    if (!f) return false;
    bool ok = false;

    Elf64_Ehdr eh;
    if (fread(&eh, 1, sizeof(eh), f) == sizeof(eh) &&
        memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 &&
        eh.e_ident[EI_CLASS] == ELFCLASS64 && eh.e_ident[EI_DATA] == ELFDATA2LSB) {

        long dynOff = -1;
        unsigned long long dynFilesz = 0;
        for (int i = 0; i < eh.e_phnum; ++i) {
            Elf64_Phdr ph;
            if (!ReadAt(f, (long)(eh.e_phoff + (unsigned long)i * eh.e_phentsize), ph)) break;
            if (ph.p_type == PT_DYNAMIC) {
                dynOff = (long)ph.p_offset;
                dynFilesz = ph.p_filesz;
                break;
            }
        }

        if (dynOff >= 0) {
            auto vaddrToFile = [&](unsigned long long v, long& off) -> bool {
                for (int i = 0; i < eh.e_phnum; ++i) {
                    Elf64_Phdr ph;
                    if (!ReadAt(f, (long)(eh.e_phoff + (unsigned long)i * eh.e_phentsize), ph)) break;
                    if (ph.p_type == PT_LOAD && v >= ph.p_vaddr &&
                        v < ph.p_vaddr + ph.p_memsz) {
                        off = (long)(ph.p_offset + (v - ph.p_vaddr));
                        return true;
                    }
                }
                return false;
            };

            unsigned long long dtSymtab = 0, dtStrtab = 0, dtStrsz = 0, dtSyment = sizeof(Elf64_Sym);
            unsigned long long dtGnuHash = 0, dtHash = 0;
            bool sawNull = false;
            for (unsigned long long off = 0; off + sizeof(Elf64_Dyn) <= dynFilesz && !sawNull;
                 off += sizeof(Elf64_Dyn)) {
                Elf64_Dyn dyn;
                if (!ReadAt(f, dynOff + (long)off, dyn)) break;
                switch (dyn.d_tag) {
                    case DT_NULL:      sawNull = true; break;
                    case DT_SYMTAB:    dtSymtab = dyn.d_un.d_ptr; break;
                    case DT_STRTAB:    dtStrtab = dyn.d_un.d_ptr; break;
                    case DT_STRSZ:     dtStrsz = dyn.d_un.d_ptr; break;
                    case DT_SYMENT:    dtSyment = dyn.d_un.d_ptr; break;
                    case DT_GNU_HASH:  dtGnuHash = dyn.d_un.d_ptr; break;
                    case DT_HASH:      dtHash = dyn.d_un.d_ptr; break;
                    default: break;
                }
            }

            long symtabOff = 0, strtabOff = 0;
            if (dtSymtab && dtStrtab && vaddrToFile(dtSymtab, symtabOff) &&
                vaddrToFile(dtStrtab, strtabOff)) {

                // Determine the symbol count from either hash table flavor.
                unsigned long long symCount = 0;
                if (dtHash) {
                    long hOff = 0;
                    if (vaddrToFile(dtHash, hOff)) {
                        Elf64_Word nbucket = 0, nchain = 0;
                        if (ReadAt(f, hOff, nbucket) && ReadAt(f, hOff + 4, nchain))
                            symCount = nchain;
                    }
                } else if (dtGnuHash) {
                    long gOff = 0;
                    if (vaddrToFile(dtGnuHash, gOff)) {
                        Elf64_Word nbuckets = 0, symoffset = 0, bloomSize = 0, bloomShift = 0;
                        if (ReadAt(f, gOff, nbuckets) &&
                            ReadAt(f, gOff + 4, symoffset) &&
                            ReadAt(f, gOff + 8, bloomSize) &&
                            ReadAt(f, gOff + 12, bloomShift) && nbuckets > 0) {
                            long bucketsOff = gOff + 16 + (long)bloomSize * 8;
                            symCount = symoffset;
                            for (Elf64_Word b = 0; b < nbuckets; ++b) {
                                Elf64_Word bucket = 0;
                                if (!ReadAt(f, bucketsOff + (long)b * 4, bucket)) break;
                                if (bucket == 0) continue;
                                if (bucket >= symCount) symCount = bucket + 1;
                                // Walk the chain to find this bucket's highest index.
                                Elf64_Word idx = bucket;
                                long chainsOff = bucketsOff + (long)nbuckets * 4;
                                for (;;) {
                                    Elf64_Word chain = 0;
                                    if (!ReadAt(f, chainsOff + (long)(idx - symoffset) * 4, chain))
                                        break;
                                    ++symCount;
                                    if (chain & 1u) break;
                                    ++idx;
                                    if (idx - symoffset > 1u << 20) break;  // sanity
                                }
                            }
                        }
                    }
                }

                if (symCount > 0 && symCount < (1u << 22)) {
                    for (unsigned long long i = 0; i < symCount; ++i) {
                        Elf64_Sym s;
                        if (!ReadAt(f, symtabOff + (long)i * (long)dtSyment, s)) break;
                        if (s.st_name == 0 || s.st_shndx == SHN_UNDEF) continue;
                        if (s.st_name >= dtStrsz) continue;
                        char name[256] = { 0 };
                        if (fseek(f, strtabOff + (long)s.st_name, SEEK_SET) != 0) break;
                        size_t got = fread(name, 1, sizeof(name) - 1, f);
                        name[got] = '\0';
                        if (strcmp(name, symName) == 0) {
                            outAddr = bias + s.st_value;
                            ok = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    fclose(f);
    return ok;
}

// Resolve a symbol in whichever of the target's libraries exports it
// (dlopen lives in libdl.so.2 before glibc 2.34, in libc.so.6 after).
bool ResolveRemoteSymbol(pid_t pid, const char* symName, unsigned long long& outAddr) {
    std::vector<MapEntry> maps;
    if (!ParseMaps(pid, maps)) return false;

    const char* candidates[] = { "libdl.so", "libc.so.6", "libc.so" };
    std::vector<std::string> files;
    for (const auto& m : maps) {
        if (m.path.empty() || m.pgoff != 0) continue;
        for (const char* cand : candidates) {
            if (m.path.find(cand) != std::string::npos) {
                if (std::find(files.begin(), files.end(), m.path) == files.end())
                    files.push_back(m.path);
            }
        }
    }
    for (const std::string& file : files) {
        unsigned long long bias = 0;
        if (!LibraryLoadBias(maps, file, bias)) continue;
        if (ResolveSymbolInFile(file, symName, bias, outAddr)) return true;
    }
    return false;
}

// Actual path of an already-loaded module in the target (for RTLD_NOLOAD
// lookups: dlopen matches by pathname/inode, so we need the loaded path, not
// our fresh copy in /tmp).
bool FindLoadedModulePath(pid_t pid, const char* namePart, std::string& outPath) {
    std::vector<MapEntry> maps;
    if (!ParseMaps(pid, maps)) return false;
    for (const auto& m : maps) {
        if (m.path.empty()) continue;
        if (m.path.find(namePart) != std::string::npos) {
            outPath = m.path;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Remote call machinery
// ---------------------------------------------------------------------------
// Every failure path records the stage it died in so the console can show
// something actionable (and user bug reports get a useful pointer).
thread_local char g_CallStage[192] = { 0 };

void FailStage(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_CallStage, sizeof(g_CallStage), fmt, args);
    va_end(args);
}

const char* CallStage() { return g_CallStage[0] ? g_CallStage : "unknown stage"; }
struct RemoteSession {
    pid_t pid = -1;
    std::vector<pid_t> attached;   // every tid we successfully attached
    pid_t victim = -1;
    bool wordSaved = false;
    unsigned long long trapAddr = 0;
    unsigned long long origWord = 0;
    bool regsChanged = false;
    user_regs_struct origRegs = {};
};

// ---------------------------------------------------------------------------
// Crash safety.  If the loader dies mid-call (SIGINT/SIGTERM/kill/abort), the
// kernel detaches the target automatically - but our int3 trap byte would
// stay planted and the hijacked victim would resume with garbage registers:
// the game would die on the next SIGTRAP.  The handler below restores the
// word + registers first, using only raw syscalls (async-signal-safe).
// ---------------------------------------------------------------------------
struct EmergencyState {
    volatile sig_atomic_t active;
    pid_t pid;
    pid_t victim;
    int haveWord;
    int haveRegs;
    unsigned long long trapAddr;
    unsigned long long origWord;
    user_regs_struct origRegs;
    int nTids;
    pid_t tids[512];
};
static EmergencyState g_Emergency = {};

static void SnapshotTids(const RemoteSession& s) {
    g_Emergency.nTids = 0;
    for (pid_t tid : s.attached) {
        if (g_Emergency.nTids >= (int)(sizeof(g_Emergency.tids) / sizeof(g_Emergency.tids[0]))) break;
        g_Emergency.tids[g_Emergency.nTids++] = tid;
    }
}

static void EmergencyDetach() {
    if (!g_Emergency.active) return;
    g_Emergency.active = 0;
    if (g_Emergency.victim > 0) {
        if (g_Emergency.haveWord) {
            syscall(SYS_ptrace, PTRACE_POKETEXT, g_Emergency.victim,
                    (void*)g_Emergency.trapAddr, (void*)g_Emergency.origWord);
        }
        if (g_Emergency.haveRegs) {
            syscall(SYS_ptrace, PTRACE_SETREGS, g_Emergency.victim,
                    nullptr, &g_Emergency.origRegs);
        }
    }
    for (int i = 0; i < g_Emergency.nTids; ++i) {
        // Group-stopped tracees only run again on SIGCONT (see SessionCleanup); this is the path that runs when
        // the loader is dying, so a plain detach here would leave the player's game frozen for good.
        syscall(SYS_ptrace, PTRACE_DETACH, g_Emergency.tids[i], nullptr, (void*)(long)SIGCONT);
    }
    if (g_Emergency.pid > 0) syscall(SYS_kill, g_Emergency.pid, SIGCONT);
    g_Emergency.nTids = 0;
}

static void EmergencyHandler(int sig) {
    static const char msg[] =
        "\n[slinky] interrupted mid-injection - restoring the game and detaching\n";
    ssize_t ignored = write(2, msg, sizeof(msg) - 1);
    (void)ignored;
    EmergencyDetach();
    _exit(128 + sig);
}

static void InstallEmergencyHandlers() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = EmergencyHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    // The four ways this process can die on its own. They matter more than the four above: at this moment the
    // game has every thread stopped by PTRACE_ATTACH and an int3 planted in code it shares with the rest of the
    // system, so a loader that dies without putting those back leaves Minecraft frozen for good - the player
    // loses the run, not just the client. Everything EmergencyDetach does is raw syscalls, so it is safe to run
    // from inside these signals.
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
}


bool PtracedPoke(pid_t tid, unsigned long long addr, unsigned long long value) {
    return ptrace(PTRACE_POKEDATA, tid, (void*)addr, (void*)value) == 0;
}

void SessionCleanup(RemoteSession& s) {
    // Best-effort: restore the trap word and registers, then release everyone.
    if (s.wordSaved && s.victim > 0) {
        ptrace(PTRACE_POKETEXT, s.victim, (void*)s.trapAddr, (void*)s.origWord);
        s.wordSaved = false;
    }
    if (s.regsChanged && s.victim > 0) {
        ptrace(PTRACE_SETREGS, s.victim, nullptr, &s.origRegs);
        s.regsChanged = false;
    }
    for (pid_t tid : s.attached) {
        // SIGCONT with the detach, not 0. Every one of these threads was stopped by PTRACE_ATTACH, which is a
        // group-stop, and a tracee detached from a group-stop is not running: it waits for SIGCONT, which nothing
        // was sending. The library then mapped, its constructors ran nowhere, no log line was ever written, and
        // the loader sat out its whole budget over a game that looked merely slow - the reported hang. SIGCONT is
        // the only signal that restarts a group-stopped thread, and it is a no-op for one that is already running.
        ptrace(PTRACE_DETACH, tid, nullptr, (void*)(long)SIGCONT);
    }
    if (!s.attached.empty()) kill(s.pid, SIGCONT);
    s.attached.clear();
    g_Emergency.active = 0;
    g_Emergency.nTids = 0;
    g_Emergency.victim = -1;
}

// Attach every thread of the process (stopping all of them keeps other
// threads from running while the trap byte is planted).
bool AttachAll(RemoteSession& s) {
    std::string taskDir = "/proc/" + std::to_string((long)s.pid) + "/task";
    DIR* d = opendir(taskDir.c_str());
    if (!d) { FailStage("list /proc/<pid>/task"); return false; }
    std::vector<pid_t> tids;
    while (dirent* ent = readdir(d)) {
        char* end = nullptr;
        long tid = strtol(ent->d_name, &end, 10);
        if (*ent->d_name == '\0' || (end && *end != '\0') || tid <= 0) continue;
        tids.push_back((pid_t)tid);
    }
    closedir(d);
    std::sort(tids.begin(), tids.end());

    int lastErrno = 0;
    int attachFails = 0, waitFails = 0;
    for (pid_t tid : tids) {
        if (ptrace(PTRACE_ATTACH, tid, nullptr, nullptr) != 0) {
            if (errno != ESRCH) { lastErrno = errno; ++attachFails; }
            continue;
        }
        // Bounded, never a blocking waitpid. A thread that exits between the /proc scan and the attach reply
        // never delivers a stop, and the old blocking wait sat in this call while the game looked untouched -
        // the one wait in the loader with no deadline, and the only way to get out of it was to close the game.
        // A thread that will not stop within the deadline is released and skipped instead.
        int st = 0;
        bool stopped = false;
        const unsigned long long deadline = GetTickCount64() + 500;
        for (;;) {
            pid_t w = waitpid(tid, &st, WNOHANG | __WALL);
            if (w == tid) { stopped = WIFSTOPPED(st); break; }
            if (w < 0) break;                                  // ESRCH: the thread died after the scan
            if (GetTickCount64() >= deadline) break;
            Sleep(2);
        }
        if (!stopped) {
            ptrace(PTRACE_DETACH, tid, nullptr, (void*)(long)SIGCONT);
            if (!ProcessAlive(tid)) continue;                  // gone: not a failure
            ++waitFails;
            continue;
        }
        s.attached.push_back(tid);
    }
    if (s.attached.empty()) {
        // Every attach refused is a permission question, and "permission" on Linux is four different problems
        // wearing one errno - the old text blamed Yama whatever the cause, which sent players running a command
        // that could not help them. The reason is worked out from /proc instead.
        FailStage("PTRACE_ATTACH refused on 0/%zu threads (attach errors=%d, wait errors=%d). %s",
                  tids.size(), attachFails, waitFails, PtraceRefusalReason(s.pid).c_str());
        errno = lastErrno ? lastErrno : EPERM;
        return false;
    }
    if (waitFails > 0) {
        // Both stages depend on every other thread staying exactly where it was: stage 1 replaces one byte of
        // code that the victim shares with the rest of the process, and that is only safe while nothing else can
        // execute it. A thread that would not stop was released above with SIGCONT, and SIGCONT drops the SIGSTOP
        // the attach had queued for it - so that thread is running, and it will run over the patched byte. In a
        // detached, untraced process that is a plain SIGTRAP with nobody left to intercept it, which ends
        // Minecraft on the spot. Refusing costs the player one retry; continuing can cost them the run.
        FailStage("%d thread(s) of the game would not stop within half a second, so the injection was refused: a "
                  "thread stuck in the kernel (a slow disk, a network share) cannot be left running while the "
                  "loader patches code the game shares with it", waitFails);
        errno = EAGAIN;
        return false;
    }
    return true;
}

struct SyscallInfo {
    bool known = false;
    long nr = -1;
};

SyscallInfo ThreadSyscall(pid_t tid) {
    SyscallInfo info;
    std::string s;
    if (!ReadWholeFile("/proc/" + std::to_string((long)tid) + "/syscall", s)) return info;
    long nr = -1;
    if (sscanf(s.c_str(), "%ld", &nr) == 1) { info.known = true; info.nr = nr; }
    return info;
}

bool IsSleepingSyscall(long nr) {
    switch (nr) {
        case 0:    // read
        case 7:    // poll
        case 23:   // select
        case 35:   // nanosleep
        case 232:  // epoll_wait
        case 270:  // pselect6
        case 271:  // ppoll
        case 281:  // epoll_pwait
            return true;
        default:
            return false;
    }
}

// A viable victim must be PARKED IN THE KERNEL (nr >= 0 in /proc/tid/syscall), and the call it is parked in has
// to be one the hijack can be taken out of without damaging the process.
//
// The check used to be "nr >= 0" on the reasoning that a thread blocked in a syscall holds no userspace lock.
// That is true, but it is not the only lock there is: a thread blocked inside mmap/munmap/mprotect/brk/madvise
// holds the kernel's mmap_lock, and the hijacked call below is dlopen, which takes that same lock in write mode
// - a task write-locking a read lock it already owns never wakes up. The victim is then deadlocked inside our
// own call, the trap never arrives, and the timeout path restores registers into a thread that is still standing
// in the dynamic linker, which is worse for the game than any failure this function could report. clone and exec
// are the same story with their own locks, and exit_group (231) is fatal in a different way: the restore puts
// that thread back on the instruction that tears the whole JVM down, so the injection ends with the game closing
// and the client log empty.
//
// So the acceptable list is spelled out instead of inferred, and everything else - including anything a future
// kernel adds - is refused. The JVM's worker threads park in futex, epoll and timed waits, so a running client
// always offers candidates; when it does not, ChooseVictim fails cleanly.
bool IsParkedInKernel(const SyscallInfo& si) {
    if (!si.known || si.nr < 0) return false;
    return si.nr == 202       // futex
        || IsSleepingSyscall(si.nr)
        || si.nr == 43        // accept
        || si.nr == 45        // recvfrom
        || si.nr == 47;       // recvmsg
}
// (The .so path and shellcode live on a private mmap'd page - nothing is
// planted on the victim's stack beyond the return slot.)

// Pick the thread to hijack: parked in a blocking kernel syscall, with deep
// enough stack for the 64 KiB margin + path below its stack pointer.
bool ChooseVictim(RemoteSession& s, const std::string& needBytes, pid_t& outTid,
                  user_regs_struct& outRegs, unsigned long long& outSlot) {
    std::vector<MapEntry> maps;
    if (!ParseMaps(s.pid, maps)) { FailStage("parse /proc/<pid>/maps for stacks"); return false; }

    std::vector<pid_t> sorted = s.attached;
    // Score once, then sort. The comparator used to read /proc for both sides on every comparison, so its
    // answer changed while std::stable_sort was running and the sort was undefined behaviour in the loader's
    // most delicate moment - a crash or a heap walk there leaves the game traced.
    struct ScoredThread { pid_t tid; int score; };
    std::vector<ScoredThread> scored;
    scored.reserve(sorted.size());
    for (pid_t tid : sorted) {
        SyscallInfo si = ThreadSyscall(tid);
        int score = -1;                                  // userspace, or a call the hijack must not enter
        if (IsParkedInKernel(si)) {
            if (si.nr == 202) score = 1;                 // futex: legal but least friendly
            else score = IsSleepingSyscall(si.nr) ? 3 : 2;
        }
        scored.push_back(ScoredThread{ tid, score });
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const ScoredThread& a, const ScoredThread& b) { return a.score > b.score; });
    sorted.clear();
    for (const ScoredThread& st : scored) sorted.push_back(st.tid);

    (void)needBytes;
    int running = 0, noRegs = 0, noVma = 0, noRoom = 0;

    for (pid_t tid : sorted) {
        SyscallInfo info = ThreadSyscall(tid);
        if (!IsParkedInKernel(info)) { ++running; continue; }

        user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, tid, nullptr, &regs) != 0) { ++noRegs; continue; }

        // VMA containing the stack pointer.
        const MapEntry* vma = nullptr;
        for (const auto& m : maps) {
            if (regs.rsp >= m.start && regs.rsp < m.end) { vma = &m; break; }
        }
        if (!vma) { ++noVma; continue; }

        unsigned long long retSlot = (regs.rsp & ~0xFULL) - 8;   // SysV: rsp % 16 == 8 at entry
        if (retSlot < vma->start + 0x40) { ++noRoom; continue; } // room for both synthetic calls' pushes

        outTid = tid;
        outRegs = regs;
        outSlot = retSlot;
        return true;
    }
    FailStage("choose victim: %zu attached, none parked where the hijack is safe (userspace-running=%d, "
              "getregs-fails=%d, no-stack-vma=%d, stack-too-shallow=%d). A game that is still starting up can be "
              "between waits here - run it again once the window is up",
              sorted.size(), running, noRegs, noVma, noRoom);
    return false;
}

// Perform fn(arg1, arg2) inside the target process; result comes back in rax.
//
// Two-stage design (each piece exists to close a real failure mode):
//   stage 1: hijack the parked victim to call mmap() for a private RWX page.
//            mmap needs no userspace locks, so every other thread can stay
//            FROZEN during this microsecond window - the temporary int3 at
//            the victim's original RIP is never observable, and no lock
//            stall is possible.
//   stage 2: the shared-code byte is restored and everyone is released.
//            [path][shellcode] is written into the private page and the
//            victim calls fn there; the shellcode ends with an int3 that
//            exists nowhere else in the process - no other thread can touch
//            it, and no thread is frozen while fn() runs (dlopen takes the
//            linker/malloc locks normally).
bool RemoteCall(pid_t pid, unsigned long long fn, unsigned long long arg1,
                unsigned long long arg2, const std::string& arg1Str,
                unsigned long long& outResult) {
    outResult = 0;
    g_CallStage[0] = '\0';

    unsigned long long mmapFn = 0;
    if (!ResolveRemoteSymbol(pid, "mmap", mmapFn)) {
        FailStage("resolve mmap in target libc");
        errno = ENOENT;
        return false;
    }

    RemoteSession s;
    s.pid = pid;

    if (!AttachAll(s)) {
        int err = errno;
        SessionCleanup(s);
        errno = err;
        return false;
    }

    pid_t victim = -1;
    user_regs_struct savedRegs;
    unsigned long long unusedSlot = 0;
    if (!ChooseVictim(s, arg1Str, victim, savedRegs, unusedSlot)) {
        SessionCleanup(s);
        errno = EINVAL;
        return false;
    }

    s.victim = victim;
    s.origRegs = savedRegs;

    // Arm the emergency state: a dying loader must always be able to restore
    // the victim and detach everyone.
    g_Emergency.active = 1;
    g_Emergency.pid = pid;
    g_Emergency.victim = victim;
    g_Emergency.haveRegs = 1;
    g_Emergency.origRegs = savedRegs;
    g_Emergency.haveWord = 0;
    SnapshotTids(s);

    // Shared wait loop for both stages: re-inject every real signal, succeed
    // only on our SIGTRAP.
    //
    // It waits on -1 rather than on the victim alone, and that is the whole reason a bootstrap that calls
    // pthread_create used to hang here: a thread born inside a traced process is taken over by the same
    // tracer, and unless somebody waits for it and releases it, it is born stopped. The dlopen then "succeeds"
    // over a library whose worker never ran, no thread ever reaches the trap byte, and the loader sits out its
    // budget while the game looks untouched - which is exactly what the player saw. Strays are detached; only
    // the victim's stops (or the game's own death) decide the outcome.
    bool trapped = false;
    std::string waitWhy;
    // expectedRip is the instruction after the int3 this call planted: the shared-code trap in stage 1, the
    // shellcode's in stage 2. A SIGTRAP from anywhere else is the game's own and is handed straight back - the
    // old code took any trap on the victim as its own and read rax as a page address, which is how a JVM trap
    // became a write into whatever number happened to be in rax.
    auto waitForTrap = [&](unsigned long long budgetMs, unsigned long long expectedRip) {
        const unsigned long long dl = GetTickCount64() + budgetMs;
        trapped = false;
        waitWhy = "no SIGTRAP within budget";
        while (GetTickCount64() < dl) {
            if (!ProcessAlive(pid)) { waitWhy = "target process died while waiting for trap"; return; }
            int st = 0;
            pid_t w = waitpid(-1, &st, WNOHANG | __WALL);
            if (w == 0) { Sleep(10); continue; }
            if (w < 0) { waitWhy = "waitpid error"; return; }
            if (w != victim) {
                if (WIFEXITED(st) || WIFSIGNALED(st)) {
                    if (w == pid) { waitWhy = "target process died while waiting for trap"; return; }
                    continue;
                }
                if (WIFSTOPPED(st)) {
                    int sig = WSTOPSIG(st);
                    int deliver = 0;
                    if (sig == SIGTRAP) {
                        // Ours only when the thread stands on the instruction we patched. Swallowing a JVM trap
                        // stops the game just as surely as a frozen thread does, so anything else goes back.
                        user_regs_struct r;
                        bool ours = ptrace(PTRACE_GETREGS, w, nullptr, &r) == 0 &&
                                    ((unsigned long long)r.rip == expectedRip ||
                                     (unsigned long long)r.rip == s.trapAddr + 1);
                        deliver = ours ? SIGCONT : SIGTRAP;
                    } else if (sig == SIGSTOP) {
                        // A thread born inside a traced process arrives in a group-stop, and only SIGCONT
                        // restarts one: detaching with 0 is how the client's own worker thread used to be left
                        // stopped forever while the library sat mapped and completely silent.
                        deliver = SIGCONT;
                    } else {
                        // Released with the signal it stopped on, delivered: detaching with 0 would swallow a
                        // real SIGIO or a GC signal the JVM was waiting for, and the game would hang looking idle.
                        deliver = sig;
                    }
                    ptrace(PTRACE_DETACH, w, nullptr, (void*)(long)deliver);
                }
                continue;
            }
            if (WIFEXITED(st)) {
                char b[80];
                snprintf(b, sizeof(b), "victim thread exited (code %d) during remote call", WEXITSTATUS(st));
                waitWhy = b;
                return;
            }
            if (WIFSIGNALED(st)) {
                char b[80];
                snprintf(b, sizeof(b), "victim thread killed by signal %d during remote call", WTERMSIG(st));
                waitWhy = b;
                return;
            }
            if (WIFSTOPPED(st)) {
                int sig = WSTOPSIG(st);
                if (sig == SIGTRAP) {
                    user_regs_struct r;
                    if (ptrace(PTRACE_GETREGS, victim, nullptr, &r) == 0 &&
                        (unsigned long long)r.rip == expectedRip) { trapped = true; return; }
                    ptrace(PTRACE_CONT, victim, nullptr, (void*)(long)SIGTRAP);
                    continue;
                }
                if (sig == SIGSTOP) { ptrace(PTRACE_CONT, victim, nullptr, (void*)(long)SIGCONT); continue; }
                ptrace(PTRACE_CONT, victim, nullptr, (void*)(long)sig);
            }
        }
    };

    auto releaseOthers = [&]() {
        for (auto it = s.attached.begin(); it != s.attached.end(); ) {
            if (*it == victim) { ++it; continue; }
            // Group-stop again: without SIGCONT these threads stay stopped after the detach and the game never
            // runs another instruction - including the client's constructor, which is what the loader is waiting for.
            ptrace(PTRACE_DETACH, *it, nullptr, (void*)(long)SIGCONT);
            it = s.attached.erase(it);
        }
        if (!s.attached.empty()) kill(pid, SIGCONT);
        SnapshotTids(s);
    };

    // ---------------- stage 1: remote mmap ---------------------------------
    errno = 0;
    long word = ptrace(PTRACE_PEEKTEXT, victim, (void*)savedRegs.rip, nullptr);
    if (errno != 0) {
        int e = errno;
        FailStage("stage1: read word at victim rip=%llx (PEEKTEXT): %s",
                  (unsigned long long)savedRegs.rip, strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }
    s.trapAddr = savedRegs.rip;
    s.origWord = (unsigned long long)word;
    s.wordSaved = true;
    g_Emergency.trapAddr = s.trapAddr;
    g_Emergency.origWord = s.origWord;
    g_Emergency.haveWord = 1;

    unsigned long long trapWord = (s.origWord & ~0xFFULL) | 0xCCULL;
    if (ptrace(PTRACE_POKETEXT, victim, (void*)savedRegs.rip, (void*)trapWord) != 0) {
        int e = errno;
        FailStage("stage1: plant int3 at rip=%llx (POKETEXT): %s",
                  (unsigned long long)savedRegs.rip, strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }

    unsigned long long stage1Ret = (savedRegs.rsp & ~0xFULL) - 8;
    if (!PtracedPoke(victim, stage1Ret, savedRegs.rip)) {  // return into the trap
        int e = errno;
        FailStage("stage1: write return address at %llx (POKEDATA): %s", stage1Ret, strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }

    user_regs_struct st1 = savedRegs;
    st1.rdi = 0;                       // addr: let the kernel choose
    st1.rsi = 0x2000;                  // len: 8 KiB scratch page
    st1.rdx = 7;                       // PROT_READ|PROT_WRITE|PROT_EXEC
    st1.rcx = 0x22;                    // MAP_PRIVATE|MAP_ANONYMOUS
    st1.r8  = (unsigned long long)-1;  // fd
    st1.r9  = 0;                       // offset
    st1.rax = 0;
    st1.rsp = stage1Ret;
    st1.rip = mmapFn;
    if (ptrace(PTRACE_SETREGS, victim, nullptr, &st1) != 0) {
        int e = errno;
        FailStage("stage1: set mmap registers (SETREGS): %s", strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }
    s.regsChanged = true;

    if (ptrace(PTRACE_CONT, victim, nullptr, nullptr) != 0) {
        int e = errno;
        FailStage("stage1: resume victim tid=%d (PTRACE_CONT): %s", (int)victim, strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }

    waitForTrap(5000, s.trapAddr + 1);
    if (!trapped) {
        FailStage("stage1 mmap: %s", waitWhy.c_str());
        SessionCleanup(s);
        errno = ETIMEDOUT;
        return false;
    }

    user_regs_struct after1;
    if (ptrace(PTRACE_GETREGS, victim, nullptr, &after1) != 0) {
        int e = errno;
        FailStage("stage1: read mmap result (GETREGS): %s", strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }
    unsigned long long page = after1.rax;

    // Restore the shared-code byte IMMEDIATELY, before anyone else can run
    // over it, then put the victim back into its original state.
    if (ptrace(PTRACE_POKETEXT, victim, (void*)s.trapAddr, (void*)s.origWord) != 0) {
        int e = errno;
        FailStage("stage1: restore word at rip=%llx (POKETEXT): %s",
                  (unsigned long long)s.trapAddr, strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }
    s.wordSaved = false;
    g_Emergency.haveWord = 0;
    if (ptrace(PTRACE_SETREGS, victim, nullptr, &s.origRegs) != 0) {
        int e = errno;
        FailStage("stage1: restore original registers (SETREGS): %s", strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }
    s.regsChanged = false;

    if ((long long)page < 0) {
        FailStage("stage1: remote mmap failed (rax=0x%llx)", page);
        SessionCleanup(s);
        errno = ENOMEM;
        return false;
    }
    page &= ~0xFFFULL;

    // The shared-code trap is gone - release everyone else.  From here on no
    // thread is frozen, so stage 2 cannot deadlock on the linker/malloc lock.
    releaseOthers();

    // ---------------- write path + shellcode into the private page ---------
    std::string blob = arg1Str;
    blob.push_back('\0');
    while (blob.size() % 8 != 0) blob.push_back('\0');
    for (size_t off = 0; off < blob.size(); off += 8) {
        unsigned long long chunk = 0;
        memcpy(&chunk, blob.data() + off, 8);
        if (!PtracedPoke(victim, page + off, chunk)) {
            int e = errno;
            FailStage("stage2: write path at page+%zx (POKEDATA): %s", off, strerror(e));
            SessionCleanup(s);
            errno = e;
            return false;
        }
    }

    const unsigned long long scAddr = page + 0x1000;
    std::vector<unsigned char> sc;
    auto emit = [&](std::initializer_list<unsigned char> bytes) {
        sc.insert(sc.end(), bytes);
    };
    if (!arg1Str.empty()) {
        // lea rdi, [rip + path]   (disp computed after we know final layout)
        emit({ 0x48, 0x8D, 0x3D, 0x00, 0x00, 0x00, 0x00 });
        int32_t disp = (int32_t)((long long)page - (long long)(scAddr + 7));
        memcpy(sc.data() + 3, &disp, 4);
    } else {
        // movabs rdi, arg1
        emit({ 0x48, 0xBF });
        for (int i = 0; i < 8; ++i) emit({ (unsigned char)((arg1 >> (8 * i)) & 0xFF) });
    }
    // mov rsi, arg2   (flags/handles used here always fit in imm32)
    emit({ 0x48, 0xC7, 0xC6 });
    {
        int32_t v = (int32_t)arg2;
        for (int i = 0; i < 4; ++i) emit({ (unsigned char)((v >> (8 * i)) & 0xFF) });
    }
    // movabs rax, fn ; call rax ; int3
    emit({ 0x48, 0xB8 });
    for (int i = 0; i < 8; ++i) emit({ (unsigned char)((fn >> (8 * i)) & 0xFF) });
    emit({ 0xFF, 0xD0 });
    emit({ 0xCC });
    const unsigned long long trapRip = scAddr + sc.size();   // where the CPU stands after the int3

    while (sc.size() % 8 != 0) sc.push_back(0x90);
    for (size_t off = 0; off < sc.size(); off += 8) {
        unsigned long long chunk = 0;
        memcpy(&chunk, sc.data() + off, 8);
        if (!PtracedPoke(victim, scAddr + off, chunk)) {
            int e = errno;
            FailStage("stage2: write shellcode at page+0x1000+%zx (POKEDATA): %s", off, strerror(e));
            SessionCleanup(s);
            errno = e;
            return false;
        }
    }

    // ---------------- stage 2: run the shellcode ---------------------------
    user_regs_struct st2 = savedRegs;
    st2.rsp = (savedRegs.rsp & ~0xFULL);  // 16-byte aligned; shellcode's `call` fixes SysV alignment
    st2.rip = scAddr;
    st2.rax = 0;
    if (ptrace(PTRACE_SETREGS, victim, nullptr, &st2) != 0) {
        int e = errno;
        FailStage("stage2: set shellcode registers (SETREGS): %s", strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }
    s.regsChanged = true;

    if (ptrace(PTRACE_CONT, victim, nullptr, nullptr) != 0) {
        int e = errno;
        FailStage("stage2: resume victim tid=%d (PTRACE_CONT): %s", (int)victim, strerror(e));
        SessionCleanup(s);
        errno = e;
        return false;
    }

    waitForTrap(20000, trapRip);
    if (!trapped) {
        FailStage("stage2 remote call: %s", waitWhy.c_str());
        SessionCleanup(s);
        errno = ETIMEDOUT;
        return false;
    }

    user_regs_struct after2;
    if (ptrace(PTRACE_GETREGS, victim, nullptr, &after2) == 0) {
        outResult = after2.rax;
    } else {
        FailStage("stage2: read call result (GETREGS)");
        SessionCleanup(s);
        errno = EIO;
        return false;
    }

    // ---- restore the victim exactly as we found it ------------------------
    SessionCleanup(s);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Yama / ptrace access
//
// We run as the invoking user (no sudo re-launch). If the kernel is using
// Yama's restricted ptrace mode we try to switch it off for this session; on
// systems where that requires root the attempt simply fails and the normal
// injection error path reports it.
// ---------------------------------------------------------------------------
static void TryRelaxYamaScope() {
    if (geteuid() == 0) return;
    if (ReadPtraceScope() <= 0) return;

    int fd = open("/proc/sys/kernel/yama/ptrace_scope", O_WRONLY);
    if (fd < 0) return;
    const char value[] = "0\n";
    (void)write(fd, value, sizeof(value) - 1);
    close(fd);
}

// One file into this run's private staging folder: the game process opens what is in there, never what sits
// next to the loader, so a second run cannot read or replace it (the same rule that puts the Windows staging
// in %TEMP%\slinky-<pid>).
static bool StageFile(const std::string& source, const std::string& dest) {
    int in = open(source.c_str(), O_RDONLY);
    if (in < 0) {
        ConsoleUI::PrintError("Reading bundled library", errno);
        return false;
    }
    struct stat srcStat;
    if (fstat(in, &srcStat) != 0) {
        close(in);
        ConsoleUI::PrintError("Reading bundled library", errno);
        return false;
    }
    unlink(dest.c_str());
    int out = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        ConsoleUI::PrintError("Writing bundled library", errno);
        return false;
    }
    fchmod(out, 0644);   // a umask of 077 must not decide whether the game can open this
    char buf[65536];
    ssize_t r;
    unsigned long long total = 0;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                close(in); close(out);
                ConsoleUI::PrintError("Writing bundled library", errno);
                return false;
            }
            off += w;
        }
        total += (unsigned long long)r;
    }
    close(in);
    if (r < 0) {
        close(out);
        ConsoleUI::PrintError("Reading bundled library", errno);
        return false;
    }
    // The whole image or nothing: a truncated .so maps as far as its headers and then dies with a message that
    // names a symbol, not the full disk that caused it. fsync also puts it on the disk before the game is asked
    // to map it from another thread.
    if ((unsigned long long)srcStat.st_size != total || fsync(out) != 0) {
        int e = errno ? errno : EIO;
        close(out);
        ConsoleUI::PrintError("Writing bundled library", e);
        return false;
    }
    if (close(out) != 0) {
        ConsoleUI::PrintError("Writing bundled library", errno);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool Injector::ExtractBundledSo(std::string& outSoPath, std::string& outLogPath) {
    InstallEmergencyHandlers();   // before any ptrace work starts

    char self[4096] = { 0 };
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) {
        ConsoleUI::PrintError("Finding own executable", errno);
        return false;
    }
    self[n] = '\0';
    std::string exeDir = Dirname(self);

    // Side-by-side layout, exactly like the shipped Windows dist
    // (slinky-loader.exe + slinky-native.dll in one folder).
    std::string candidates[] = {
        exeDir + "/libslinky-native.so",
        exeDir + "/slinky-native.so",
    };
    std::string source;
    for (const std::string& c : candidates) {
        if (access(c.c_str(), R_OK) == 0) { source = c; break; }
    }
    if (source.empty()) {
        char reason[512];
        snprintf(reason, sizeof(reason),
                 "libslinky-native.so is not next to the loader (%s).", exeDir.c_str());
        ConsoleUI::PrintFailure(reason);
        return false;
    }

    std::string tmpDir = "/tmp/slinky-" + std::to_string((long)getpid());
    int mkErr = 0;
    if (mkdir(tmpDir.c_str(), 0700) != 0) {
        mkErr = errno;
        if (mkErr != EEXIST) {
            ConsoleUI::PrintError("Creating temp folder", mkErr);
            return false;
        }
    }
    if (mkErr == EEXIST) {
        // The name is this process's id, which another local process can learn, and everything staged in here is
        // code that runs inside the game. A folder that already answers to that name is therefore only usable if it
        // is provably ours: ours to write, not a symlink to somewhere else, and not readable by a stranger.
        struct stat st;
        if (lstat(tmpDir.c_str(), &st) != 0) {
            ConsoleUI::PrintError("Checking the temp folder", errno);
            return false;
        }
        if (!S_ISDIR(st.st_mode)) {
            ConsoleUI::PrintFailure("The temp folder slinky needs is not a folder. Remove it and run again:");
            fprintf(stderr, "  %s\n", tmpDir.c_str());
            return false;
        }
        if (st.st_uid != getuid() || (st.st_mode & 0777) != 0700) {
            ConsoleUI::PrintFailure("Another user owns the temp folder slinky needs, so the client will not be "
                                    "staged there. Remove it and run again:");
            fprintf(stderr, "  %s\n", tmpDir.c_str());
            return false;
        }
    }

    outSoPath = tmpDir + "/libslinky-native.so";
    outLogPath = tmpDir + "/slinky-native.log";
    // A stale log from an earlier run is not a log, it is an answer to a different question: the monitor matches
    // "FINAL STATUS" anywhere in the file, so one old RUNNING line would be read as this run's success. If it
    // cannot be removed the run must not start.
    if (unlink(outLogPath.c_str()) != 0 && errno != ENOENT) {
        ConsoleUI::PrintError("Replacing the client log", errno);
        return false;
    }

    if (!StageFile(source, outSoPath)) return false;

    // The client is a second file: the bootstrap dlopens libslinky-engine.so from the folder it was loaded
    // from, and both Minecraft versions are played through that image. There is nothing to fall back to any more
    // - the Java payload that used to live inside the bootstrap is gone - so a folder that has the bootstrap and
    // not the engine is an incomplete install, and saying so here is far clearer than the bootstrap's own
    // "cannot open shared object file" several seconds later.
    std::string engineSource = exeDir + "/libslinky-engine.so";
    if (access(engineSource.c_str(), R_OK) != 0) {
        char reason[512];
        snprintf(reason, sizeof(reason),
                 "libslinky-engine.so is not next to the loader (%s).", exeDir.c_str());
        ConsoleUI::PrintFailure(reason);
        return false;
    }
    if (!StageFile(engineSource, tmpDir + "/libslinky-engine.so")) return false;

    // Create the log now, empty and world-readable, instead of leaving it to the client's very first write.
    // This is the difference between a run that fails out loud and the reported hang: if this folder is not
    // writable by whoever runs the game, an empty file proves it here, and a client whose log open fails can
    // never tell the loader anything at all - the loader would wait two minutes for a line no code could write.
    int logFd = open(outLogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logFd < 0) {
        ConsoleUI::PrintError("Creating the client log", errno);
        return false;
    }
    fchmod(logFd, 0644);
    close(logFd);
    return true;
}

bool Injector::InjectDll(pid_t pid, const std::string& soPath) {
    std::string dir = Dirname(soPath);

    // If the loader was started as root anyway, hand the staging dir over to
    // the game's user so the engine can write its log file there.
    if (geteuid() == 0) {
        struct stat pst;
        if (stat(("/proc/" + std::to_string((long)pid)).c_str(), &pst) == 0) {
            // Every file, not just the folder and the bootstrap. The engine image and the log used to be left
            // owned by root, so the game (running as its own user) could read the image and could not open the
            // log - which is silence, not an error, all the way out to the loader. A failure to hand them over is
            // fatal and says so; it used to be ignored.
            const std::string staged[] = {
                dir,
                soPath,
                dir + "/libslinky-engine.so",
                dir + "/slinky-native.log",
            };
            for (const std::string& f : staged) {
                if (chown(f.c_str(), pst.st_uid, pst.st_gid) != 0) {
                    ConsoleUI::PrintError("Handing the staged files to the game's user", errno);
                    return false;
                }
            }
        }
    }

    // --- ptrace preconditions first (the unload path needs ptrace too) ----
    // Best effort: if Yama is restricting ptrace, try to relax it before the
    // first attach. A failure here is reported by the attach itself.
    TryRelaxYamaScope();

    unsigned long long dlopenFn = 0, dlcloseFn = 0;
    if (!ResolveRemoteSymbol(pid, "dlopen", dlopenFn)) {
        const int err = errno;
        if (err == EPERM || err == EACCES) {
            // Reading the game's memory is the step Linux refuses, and the name in the message - "dlopen" - is
            // the least useful thing in it: the player is not missing a library, the kernel is guarding a
            // process. Which guard, and what to do about that one, is worked out below.
            ConsoleUI::PrintFailure(("Linux refused to let this program look inside the game: " +
                                     PtraceRefusalReason(pid)).c_str());
            return false;
        }
        ConsoleUI::PrintError("Finding libc dlopen", err ? err : ENOENT);
        return false;
    }
    // dlclose is optional: without it a fresh injection still works, only
    // unloading a previous instance becomes unavailable.
    (void)ResolveRemoteSymbol(pid, "dlclose", dlcloseFn);

    // A previous instance must be unloaded first (Windows does the same
    // before every injection).  Fresh games skip this entirely.
    if (ProcessUtil::IsModuleLoaded(pid, "slinky-native")) {
        // The client is a second image, and while it is mapped the bootstrap can be holding the thread
        // that is waiting to unmap it. Closing the file that thread is standing in - which is exactly what the
        // dlclose below does - takes the game down with it, so a client that is still running has to be stopped
        // from inside the game. A client that was already unloaded is not caught here: unmapping the engine is
        // that thread's last act, and by the time the loader sees the file gone the thread is gone too.
        if (ProcessUtil::IsModuleLoaded(pid, "libslinky-engine.so")) {
            ConsoleUI::PrintFailure(
                "slinkyFX is still running in the game. Unload it there (the unload key) and inject again.");
            return false;
        }
        if (!dlcloseFn) {
            ConsoleUI::PrintFailure("Could not unload previous instance (dlclose missing).");
            return false;
        }
        std::string loadedPath;
        if (FindLoadedModulePath(pid, "slinky-native", loadedPath)) {
            unsigned long long handle = 0;
            if (RemoteCall(pid, dlopenFn, 0, RTLD_NOW_FLAGS | RTLD_NOLOAD, loadedPath, handle) && handle) {
                // RTLD_NOLOAD may or may not bump the reference count depending on
                // glibc: close until the mapping is gone (a still-mapped library
                // keeps its handle valid, so a second close is only attempted
                // while we can prove the handle is live).
                for (int i = 0; i < 2; ++i) {
                    unsigned long long ignored = 0;
                    RemoteCall(pid, dlcloseFn, handle, 0, std::string(), ignored);
                    std::string still;
                    if (!FindLoadedModulePath(pid, "slinky-native", still)) break;
                    Sleep(100);
                }
            }
        }
        // Wait up to 3s for the library to disappear (parity with the
        // Windows UnloadModule polling).
        bool gone = false;
        for (int waited = 0; waited < 3000; waited += 50) {
            if (!ProcessUtil::IsModuleLoaded(pid, "slinky-native")) { gone = true; break; }
            Sleep(50);
        }
        if (!gone) {
            ConsoleUI::PrintFailure("Could not unload previous instance.");
            return false;
        }
        Sleep(300);
    }

    unsigned long long handle = 0;
    if (!RemoteCall(pid, dlopenFn, 0, RTLD_NOW_FLAGS, soPath, handle)) {
        char reason[512];
        snprintf(reason, sizeof(reason), "Remote call failed: %s.", CallStage());
        ConsoleUI::PrintFailure(reason);
        return false;
    }
    if (handle == 0) {
        ConsoleUI::PrintFailure("dlopen failed in the game process; check the library is readable.");
        return false;
    }

    if (!ProcessUtil::WaitForModuleLoad(pid, "slinky-native", 5000)) {
        ConsoleUI::PrintFailure("Library not in game after load attempt.");
        return false;
    }

    return true;
}

namespace {

// The client writes its own account of startup beside the file it was loaded from, in the folder this loader
// made for the run. When startup does not finish that file is the only thing that says why, and a player can
// copy a dozen lines but cannot guess a folder name out of a status line, so both go out here - and a run
// that wrote nothing says that too, instead of leaving the gap between the two files unexplained.
void PrintLogTail(const std::string& logPath) {
    FILE* fp = fopen(logPath.c_str(), "r");
    if (!fp) {
        fprintf(stderr, "\nNothing was written to %s - the client never got as far as keeping a log.\n",
                logPath.c_str());
        return;
    }

    std::vector<std::string> lines;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len > 0) {
            lines.push_back(line);
            if (lines.size() > 30) lines.erase(lines.begin());
        }
    }
    fclose(fp);

    if (lines.empty()) {
        fprintf(stderr, "\nThe log at %s is empty.\n", logPath.c_str());
        return;
    }

    fprintf(stderr, "\n--- last lines of %s ---\n", logPath.c_str());
    for (const std::string& l : lines) fprintf(stderr, "%s\n", l.c_str());
    fprintf(stderr, "--- end of log ---\n");
}

// A run that is working and a run that has stopped look identical here: one status line for the whole two and a
// half minutes. The client's log grows while it works, so the newest line it wrote becomes the status. It is cut
// short on purpose - the status is the last line of the redrawn block, and a line that wraps moves the cursor off
// the row the redraw counts on.
void ShowClientProgress(const std::string& log, std::string& shown) {
    std::string line;
    size_t end = log.size();
    while (end > 0) {
        size_t nl = log.rfind('\n', end - 1);
        size_t start = (nl == std::string::npos) ? 0 : nl + 1;
        std::string seg = log.substr(start, end - start);
        size_t a = seg.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { end = start; continue; }   // blank line; the one above it counts
        line = seg.substr(a, seg.find_last_not_of(" \t\r\n") - a + 1);
        break;
    }
    if (line.empty() || line == shown) return;
    if (line.front() == '[') {
        size_t close = line.find(']');
        if (close != std::string::npos && close + 2 < line.size()) line = line.substr(close + 2);
    }
    if (line.size() > 60) line.resize(60);
    shown = line;
    ConsoleUI::SetStatusText(shown.c_str());
}

} // namespace

bool Injector::MonitorStartup(pid_t pid, const std::string& logPath) {
    // The status line stays on "Injecting into Minecraft..." until the client starts writing, and then follows
    // the log - see ShowClientProgress.
    const unsigned long long start = GetTickCount64();
    // The client's own start-up waits up to two minutes for the game to reach its client thread, and the old
    // 150 s budget left almost no room on a slow machine (an i5 with a cold disk and Forge): the loader declared
    // failure while the client was still doing exactly what it was built to do. Three and a half minutes covers
    // that window plus a slow start.
    const unsigned long long timeoutMs = 210000;

    struct { const char* needle; const char* key; } rules[] = {
        { "FINAL STATUS: RUNNING",         "RUNNING" },
        { "FINAL STATUS: RELOADED",        "RELOADED" },
        { "FINAL STATUS: ALREADY_RUNNING", "ALREADY_RUNNING" },
        { "FINAL STATUS: FAILED",          "FAILED" },
    };

    std::string shown;
    std::string content;
    bool announced = false;
    unsigned long long lastNotice = 0;
    while (GetTickCount64() - start < timeoutMs) {
        if (!ProcessAlive(pid)) {
            PrintLogTail(logPath);   // before the failure screen, which ends in a countdown and takes the tail with it
            ConsoleUI::PrintFailure("Game process closed before the client finished starting.");
            return false;
        }

        const bool haveLog = ReadWholeFile(logPath, content);
        if (haveLog) {
            for (auto& rule : rules) {
                if (content.find(rule.needle) == std::string::npos) continue;
                std::string key(rule.key);
                if (key == "RUNNING" || key == "RELOADED") return true;
                if (key == "ALREADY_RUNNING") {
                    ConsoleUI::PrintFailure("Already running. Unload from menu first.");
                    return false;
                }
                PrintLogTail(logPath);
                ConsoleUI::PrintFailure("Minecraft could not start the client.");
                return false;
            }
            ShowClientProgress(content, shown);
        }

        if (!announced) {
            // Injection is finished and the game has not said anything yet. Saying so is the difference between
            // "stuck injecting" and "waiting for the game, and here is the file being watched".
            ConsoleUI::SetStatusText("Waiting for the client to start");
            announced = true;
        }
        const unsigned long long elapsed = GetTickCount64() - start;
        if (shown.empty() && elapsed >= 10000 && elapsed - lastNotice >= 5000) {
            lastNotice = elapsed;
            char notice[96];
            snprintf(notice, sizeof notice, "Waiting for the client (%llus, %s)", elapsed / 1000,
                     !haveLog ? "log missing" : content.empty() ? "log empty" : "log has data");
            ConsoleUI::SetStatusText(notice);
        }
        Sleep(150);
    }

    // Evidence first, verdict second. PrintFailure ends in a five-second countdown and the process exits; a log
    // tail printed after it was the one thing the player could have sent us, and it scrolled away with the screen.
    PrintLogTail(logPath);
    struct stat logStat;
    if (stat(logPath.c_str(), &logStat) != 0) {
        ConsoleUI::PrintFailure("The client never wrote its log, so it never started inside the game.");
    } else if (logStat.st_size == 0) {
        ConsoleUI::PrintFailure("The client opened its log but wrote nothing. It stopped before start-up.");
    } else {
        ConsoleUI::PrintFailure("Library loaded but the client never confirmed startup.");
    }
    return false;
}

} // namespace Slinky
