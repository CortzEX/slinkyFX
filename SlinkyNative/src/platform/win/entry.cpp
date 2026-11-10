// Windows entry of the client DLL: it is injected into the game process (LoadLibrary), so DllMain only starts the
// engine thread (slinky_setup runs JNI and file I/O, which must not happen under the loader lock).
#include "slinky/platform.h"
#include "slinky/win32.h"

extern "C" double time_now();   // engine/protected/files.cpp: the process counter clock the frame code uses too

// The engine thread is the only thread that talks to JVMTI, and it lives for as long as the DLL does:
// slinky_setup runs on it and stays attached (see init.cpp), the unload hands the hook removal back to it,
// and only after that succeeded does it free the state and the module itself.
// Requests come from the game's render thread, which owns the OpenGL context the GUI was built with:
//   engine_disarm_step()    -> the engine thread hands one hooked class back, the answer is 0/1/error
//   engine_leave_and_free() -> the render thread is done with the frame; the engine thread frees everything
// Each request is answered before the frame goes on, because this thread is standing inside the game's own
// rewritten call site: the next frame - and the next step - would come from here.
enum { ERR_NO_ENGINE_THREAD = 0x801, ERR_DISARM_TIMED_OUT = 0x802 };

static HANDLE engine_work;      // render thread -> engine thread: take the hooks out
static HANDLE engine_done;      // engine thread -> render thread: engine_disarm_answer holds the result
static HANDLE engine_leave;     // render thread -> engine thread: the client is down to nothing, free the DLL
static int engine_disarm_answer;

// Whether an engine owns this process is what the loader has to know before it injects, and the module list
// cannot answer it: after an unload the image often stays mapped with nothing running in it. The name lives
// as long as the engine thread holds its handle, so it goes out exactly when a new client may take over.
static HANDLE engine_live_marker;

// 1 = another engine is already in this process.
static int live_marker_take(void) {
    wchar_t name[48];
    const wchar_t prefix[] = L"Local\\slinky-engine-";
    int n = 0;
    for (int i = 0; prefix[i]; i++) name[n++] = prefix[i];
    DWORD pid = GetCurrentProcessId();
    char digits[12];
    int d = 0;
    do { digits[d++] = (char)('0' + (int)(pid % 10)); pid /= 10; } while (pid);
    while (d) name[n++] = (wchar_t)digits[--d];
    name[n] = 0;
    // This thread is new, so the last error is still 0 and only CreateEventW can have set it.
    HANDLE h = CreateEventW(0, 1, 0, name);
    if (!h) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); return 1; }
    engine_live_marker = h;
    return 0;
}

static void free_and_exit(HMODULE self) {
    slinky_teardown();   // release what start-up built (init.cpp)
    slinky_logf("unload: state freed");
    slinky_detach();     // ... and leave the JVM before this thread takes the module with it
    slinky_logf("FINAL STATUS: UNLOADED");
    if (engine_live_marker) CloseHandle(engine_live_marker);   // from here the game is free to take a new client
    engine_live_marker = 0;
    FreeLibraryAndExitThread(self, 0);
}

static DWORD engine_thread(LPVOID self) {
    // First word from the client, before anything that can take a minute or hang: start-up reads the JVM's
    // pointers, walks ~1500 JNI lookups and loads the config files from disk. Until this line existed, a Windows
    // run that stopped anywhere in there produced an empty log, which looks exactly like a DLL that never loaded
    // - and the loader's advice for that case ("the client never got as far as keeping a log") was then wrong.
    slinky_logf("engine: thread up");
    if (live_marker_take()) {
        slinky_logf("FINAL STATUS: ALREADY_RUNNING");
        return 0;   // the other engine holds the game; this image stays inert and its marker closed
    }
    engine_work = CreateEventW(nullptr, 0, 0, nullptr);
    engine_done = CreateEventW(nullptr, 0, 0, nullptr);
    engine_leave = CreateEventW(nullptr, 0, 0, nullptr);

    int err = slinky_setup(self);
    if (err) {
        // The number is the only thing that separates "no JVM", "no game classes" and "JVMTI refused us", and it
        // used to be dropped on this platform.
        slinky_logf("engine: setup failed (%d)", err);
        slinky_logf("FINAL STATUS: FAILED");
        // A start-up that died after arming the hooks must not free the state under the game: without a
        // successful disarm the DLL stays resident, inert (every native checks the shutdown flag).
        int hoff = slinky_disarm();
        if (hoff) {
            slinky_logf("startup failed and the hooks stayed armed (%d): staying resident", hoff);
            return 0;
        }
        free_and_exit((HMODULE)self);
        return 0;
    }
    slinky_logf("FINAL STATUS: RUNNING");

    for (;;) {
        // With a pass half finished this wait is not infinite. The game can stop painting - minimised, a
        // world being switched, a window closed - with half its classes vanilla and the other half still
        // calling into this DLL, and nothing else in the process holds the JVMTI environment that can hand
        // the rest back. A second without a frame means the frames are gone, so this thread finishes the
        // pass itself; a render thread that later comes back finds it done and tears down as usual.
        if (WaitForSingleObject(engine_work, slinky_disarm_in_flight() ? 1000 : INFINITE) == WAIT_TIMEOUT) {
            int answer;
            do {
                answer = slinky_disarm_step();
            } while (answer == 1);
            slinky_logf("unload: no frame came for a second, the classes went back here instead (%s)",
                        answer ? "a step failed" : "done");
            continue;   // engine_done stays clear: only a request that was really posted gets an answer
        }
        double start = time_now();
        engine_disarm_answer = slinky_disarm_step();
        int answer = engine_disarm_answer;
        slinky_logf("unload: restore step took %d ms (%s)", (int)((time_now() - start) * 1000.0 + 0.5),
                    !answer ? "the game is vanilla" : answer == 1 ? "more classes to go" : "failed");
        SetEvent(engine_done);
        // 1 = the game is not vanilla yet and the render thread asks again on its next frame; an error means
        // the hooks stayed in and everything has to remain allocated, so the engine thread waits here for
        // another click exactly as it did when the whole pass was one call.
        if (!answer) break;
        if (answer != 1) slinky_logf("unload: the hooks are still in, everything stays as it is - try again");
    }
    WaitForSingleObject(engine_leave, INFINITE);
    Sleep(1000);   // the frames that entered a native before the hooks went out have to leave first
    slinky_logf("unload: engine thread awake, freeing the client");
    free_and_exit((HMODULE)self);
    return 0;   // not reached: FreeLibraryAndExitThread ends the thread
}

// Post one restore step and wait for its answer: 0 = the game has its original code back, 1 = more frames to
// go, anything else = the error that stopped the pass. The wait is only as long as one class, and it has to
// stay a wait: this thread is standing inside the game's own rewritten call site, so it is where the next
// frame - and therefore the next step - comes from.
extern "C" int engine_disarm_step(void) {
    if (!engine_work || !engine_done) return ERR_NO_ENGINE_THREAD;
    SetEvent(engine_work);
    if (WaitForSingleObject(engine_done, 30000) != WAIT_OBJECT_0) return ERR_DISARM_TIMED_OUT;
    return engine_disarm_answer;
}

extern "C" void engine_leave_and_free(void) {
    if (engine_leave) SetEvent(engine_leave);
}

// The DLL's own handle, so slinky-native.log lands next to the staged DLL and not next to javaw.exe:
// GetModuleFileNameW(NULL) returns the process executable, and the loader only reads the staging directory.
static HMODULE slinky_self;

extern "C" BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, void *) {
    if (reason == 1 /* DLL_PROCESS_ATTACH */) {
        slinky_self = (HMODULE)self;
        DisableThreadLibraryCalls(self);
        CloseHandle(CreateThread(0, 0, engine_thread, self, 0, 0));
    }
    return 1;
}

// ---- diagnostics: debugger output, the process's stderr handle and the DLL's own slinky-native.log ----
// (launchers do not keep the game's stderr, and an unload problem must stay visible after the exit)
extern "C" {
__declspec(dllimport) DWORD WINAPI GetModuleFileNameW(HMODULE m, wchar_t *buf, DWORD n);
}
static void get_dll_log_path(wchar_t *out, DWORD max_len) {
    out[0] = 0;
    DWORD len = GetModuleFileNameW(slinky_self, out, max_len);
    if (!len || len >= max_len) return;
    // Find last backslash and replace the file name with slinky-native.log.
    int last = -1;
    for (int i = 0; out[i]; i++) if (out[i] == L'\\' || out[i] == L'/') last = i;
    if (last < 0) return;
    const wchar_t name[] = L"slinky-native.log";
    int j = last + 1;
    for (int i = 0; name[i] && j + 1 < (int)max_len; i++) out[j++] = name[i];
    out[j] = 0;
}
extern "C" void slinky_log_write(const char *line) {
    char buf[600];
    int n = 0;
    for (const char *p = "slinkyFX: "; *p; p++) buf[n++] = *p;
    for (; *line && n < 597; line++) buf[n++] = *line;
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = 0;
    OutputDebugStringA(buf);
    DWORD done;
    HANDLE err = GetStdHandle((DWORD)-12 /* STD_ERROR_HANDLE */);
    if (err && err != INVALID_HANDLE) WriteFile(err, buf, (DWORD)n, &done, 0);
    // The loader asks the same question with a 4096-wide buffer, and for the same reason: a TEMP folder under
    // OneDrive or a long profile name passes MAX_PATH, and 520 was narrow enough that GetModuleFileNameW answered
    // "too long" here and the engine wrote its log nowhere at all. The loader then waited out its whole budget on
    // a file that no code was able to create - a silent hang, on the machines that most need the log.
    wchar_t path[4096];
    get_dll_log_path(path, 4096);
    if (path[0]) {
        HANDLE f = CreateFileW(path, 4 /* FILE_APPEND_DATA */, FILE_SHARE_READ | 2 /* FILE_SHARE_WRITE */,
                              0, 4 /* OPEN_ALWAYS */, FILE_ATTRIBUTE_NORMAL, 0);
        if (f != INVALID_HANDLE) {
            WriteFile(f, buf, (DWORD)n, &done, 0);
            CloseHandle(f);
        }
    }
}

// ---- OpenGL: wglGetProcAddress for extensions, opengl32 itself for GL 1.1 ----
typedef void *(WINAPI *WglGetProcAddress)(const char *);
extern "C" void *slinky_gl_get_proc(const char *name) {
    static HMODULE gl;
    static WglGetProcAddress wgl;
    if (!gl) {
        gl = GetModuleHandleA("opengl32");
        if (!gl) gl = LoadLibraryA("opengl32.dll");
        if (!gl) return 0;
        wgl = (WglGetProcAddress)GetProcAddress(gl, "wglGetProcAddress");
    }
    void *p = wgl ? wgl(name) : 0;
    if (p && p != (void *)1 && p != (void *)2 && p != (void *)3 && p != (void *)-1) return p;   // wgl failure values
    return GetProcAddress(gl, name);
}
