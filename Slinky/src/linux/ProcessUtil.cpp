// ---------------------------------------------------------------------------
// ProcessUtil.cpp (Linux) - /proc + X11 implementation of the target search.
// The scoring/fallback policy lives in Main.cpp exactly like on Windows.
// ---------------------------------------------------------------------------
#include "ProcessUtil.hpp"
#include "Compat.hpp"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <dirent.h>
#include <unistd.h>
#ifdef SLINKY_HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace Slinky {

namespace {

bool ReadWholeFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

std::string Basename(const std::string& p) {
    size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::string Cmdline(pid_t pid) {
    std::string raw;
    if (!ReadWholeFile("/proc/" + std::to_string((long)pid) + "/cmdline", raw)) return std::string();
    for (auto& c : raw) if (c == '\0') c = ' ';
    while (!raw.empty() && raw.back() == ' ') raw.pop_back();
    return raw;
}

bool IsJavaProcess(pid_t pid) {
    std::string base = "/proc/" + std::to_string((long)pid);
    std::string comm;
    if (ReadWholeFile(base + "/comm", comm)) {
        while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.pop_back();
        if (comm == "java" || comm == "javaw") return true;
    }
    std::string raw;
    if (ReadWholeFile(base + "/cmdline", raw)) {
        size_t end = raw.find('\0');
        std::string exe = raw.substr(0, end == std::string::npos ? raw.size() : end);
        std::string baseName = Basename(exe);
        if (baseName == "java" || baseName == "javaw") return true;
    }
    return false;
}

bool MapsHas(pid_t pid, const char* needle) {
    std::string maps;
    if (!ReadWholeFile("/proc/" + std::to_string((long)pid) + "/maps", maps)) return false;
    // Case-insensitive search, like _wcsicmp/wcsstr on Windows.
    std::string hay = maps, ned = needle;
    for (auto& c : hay) c = (char)tolower((unsigned char)c);
    for (auto& c : ned) c = (char)tolower((unsigned char)c);
    return hay.find(ned) != std::string::npos;
}

#ifdef SLINKY_HAVE_X11
// ---- X11 window enumeration ------------------------------------------------

unsigned long WindowPid(Display* dpy, Window w) {
    Atom atomPid = XInternAtom(dpy, "_NET_WM_PID", True);
    if (atomPid == None) return 0;
    Atom type = None;
    int fmt = 0;
    unsigned long nitems = 0, bytes = 0;
    unsigned char* prop = nullptr;
    if (XGetWindowProperty(dpy, w, atomPid, 0, 1, False, XA_CARDINAL,
                           &type, &fmt, &nitems, &bytes, &prop) != Success || !prop) {
        return 0;
    }
    unsigned long pid = 0;
    if (type == XA_CARDINAL && fmt == 32 && nitems >= 1) pid = *(unsigned long*)prop;
    XFree(prop);
    return pid;
}

std::string WindowTitle(Display* dpy, Window w) {
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
    Atom atomName = XInternAtom(dpy, "_NET_WM_NAME", True);
    if (atomName != None && utf8 != None) {
        Atom type = None;
        int fmt = 0;
        unsigned long nitems = 0, bytes = 0;
        unsigned char* prop = nullptr;
        if (XGetWindowProperty(dpy, w, atomName, 0, 4096, False, utf8,
                               &type, &fmt, &nitems, &bytes, &prop) == Success && prop) {
            std::string title;
            if (type == utf8 && fmt == 8) title.assign((const char*)prop, nitems);
            XFree(prop);
            if (!title.empty()) return title;
        }
    }
    char* legacy = nullptr;
    if (XFetchName(dpy, w, &legacy) && legacy) {
        std::string title(legacy);
        XFree(legacy);
        if (!title.empty()) return title;
    }
    return std::string();
}
#endif // SLINKY_HAVE_X11

} // namespace

std::vector<pid_t> ProcessUtil::GetJavaProcessIds() {
    std::vector<pid_t> pids;
    DIR* dir = opendir("/proc");
    if (!dir) return pids;
    while (dirent* ent = readdir(dir)) {
        char* end = nullptr;
        long pid = strtol(ent->d_name, &end, 10);
        if (*ent->d_name == '\0' || (end && *end != '\0') || pid <= 0) continue;
        if (IsJavaProcess((pid_t)pid)) pids.push_back((pid_t)pid);
    }
    closedir(dir);
    return pids;
}

std::vector<TargetWindow> ProcessUtil::FindMinecraftWindows() {
    std::vector<pid_t> javaPids = GetJavaProcessIds();
    std::vector<TargetWindow> windows;
    if (javaPids.empty()) return windows;

#ifdef SLINKY_HAVE_X11
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy) {
        Atom atomList = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
        if (atomList == None) atomList = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", True);
        if (atomList != None) {
            Atom type = None;
            int fmt = 0;
            unsigned long nitems = 0, bytes = 0;
            unsigned char* prop = nullptr;
            Window root = DefaultRootWindow(dpy);
            if (XGetWindowProperty(dpy, root, atomList, 0, 4096, False, XA_WINDOW,
                                   &type, &fmt, &nitems, &bytes, &prop) == Success && prop) {
                if (type == XA_WINDOW && fmt == 32) {
                    Window* wins = (Window*)prop;
                    for (unsigned long i = 0; i < nitems; ++i) {
                        XWindowAttributes attrs;
                        if (!XGetWindowAttributes(dpy, wins[i], &attrs)) continue;
                        if (attrs.map_state != IsViewable) continue;   // IsWindowVisible
                        pid_t pid = (pid_t)WindowPid(dpy, wins[i]);
                        if (pid <= 0) continue;
                        if (std::find(javaPids.begin(), javaPids.end(), pid) == javaPids.end()) continue;
                        std::string title = WindowTitle(dpy, wins[i]);
                        if (title.empty()) continue;                    // GetWindowTextLength > 0
                        TargetWindow tw;
                        tw.pid = pid;
                        tw.title = title;
                        // Same evidence as the command-line candidates below: the title alone says which game the
                        // window belongs to, the cmdline says which process actually loaded lwjgl.
                        tw.hint = title + " " + Cmdline(pid);
                        if (tw.hint.size() > 8192) tw.hint.resize(8192);
                        tw.fromTitle = true;
                        windows.push_back(tw);
                    }
                }
                XFree(prop);
            }
        }
        XCloseDisplay(dpy);
    }
#endif // SLINKY_HAVE_X11

    // Fallback: no X11 (or no java windows) - synthesize candidates from
    // cmdline so scoring still has something to work with.
    if (windows.empty()) {
        for (pid_t pid : javaPids) {
            std::string cl = Cmdline(pid);
            if (cl.empty()) continue;
            TargetWindow tw;
            tw.pid = pid;
            tw.hint = cl;                       // scored whole
            if (tw.hint.size() > 8192) tw.hint.resize(8192);
            if (cl.size() > 200) cl.resize(200);
            tw.title = cl;                      // shown short
            tw.fromTitle = false;
            windows.push_back(tw);
        }
    }
    return windows;
}

bool ProcessUtil::IsModuleLoaded(pid_t pid, const char* moduleName) {
    return MapsHas(pid, moduleName);
}

bool ProcessUtil::WaitForModuleLoad(pid_t pid, const char* moduleName, unsigned timeoutMs) {
    unsigned elapsed = 0;
    while (elapsed < timeoutMs) {
        if (IsModuleLoaded(pid, moduleName)) return true;
        Sleep(50);
        elapsed += 50;
    }
    return false;
}

} // namespace Slinky
