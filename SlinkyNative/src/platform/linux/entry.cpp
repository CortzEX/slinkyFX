// Linux entry of the client image: the bootstrap dlopen's it into the game process, so the constructor only
// starts the engine thread (slinky_setup runs JNI and file I/O, which must not happen while the loader holds
// its lock - the same reason DllMain does the same thing on Windows).
//
// The thread's work is the Windows one, request for request: the engine thread is the only thread that talks to
// JVMTI, the unload hands the class restore back to it one step per frame, and the teardown frees what start-up
// built after the last step. The Win32 events it waits on are the pthread ones in src/platform/linux/win32.cpp,
// so the handshake below reads the same on both platforms.
//
// One difference is deliberate: nothing unmaps this image. FreeLibraryAndExitThread leaves the thread that freed
// the module; here the engine thread can only finish and say so, and the bootstrap that dlopen'd it does the
// dlclose once it has (see slinky_engine_exited).
#define _GNU_SOURCE 1
#include "slinky/platform.h"
#include "slinky/win32.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

extern "C" double time_now();   // engine/protected/files.cpp: the process counter clock the frame code uses too

enum { ERR_NO_ENGINE_THREAD = 0x801, ERR_DISARM_TIMED_OUT = 0x802 };

static HANDLE engine_work;      // render thread -> engine thread: take the hooks out
static HANDLE engine_done;      // engine thread -> render thread: engine_disarm_answer holds the result
static HANDLE engine_leave;     // render thread -> engine thread: the client is down to nothing, free the image
static int engine_disarm_answer;

// Whether an engine owns this process is what the loader has to know before it injects, and the module list
// cannot answer it: after an unload the image often stays mapped with nothing running in it. The lock is held by
// a descriptor of this process, so it goes out exactly when a new client may take over - including when the
// process dies, which a file left behind on a crash would not have done.
static int engine_live_marker = -1;

static int live_marker_take(void) {
    char path[64];
    if ((size_t)snprintf(path, sizeof path, "/tmp/slinky-engine-%lu", (unsigned long)getpid()) >= sizeof path)
        return 0;
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return errno == EWOULDBLOCK ? 1 : 0;   // 1 = another engine is already in this process
    }
    engine_live_marker = fd;
    return 0;
}

static void live_marker_release(void) {
    if (engine_live_marker >= 0) {
        flock(engine_live_marker, LOCK_UN);
        close(engine_live_marker);
        engine_live_marker = -1;
    }
}

// 1 from the moment the engine thread has left the image: the bootstrap waits for this before it dlclose()s,
// because unmapping while a thread still runs inside it is how the game takes a segfault instead of an unload.
extern "C" volatile int slinky_engine_exited = 0;

static void free_and_exit(void *self) {
    slinky_teardown();   // release what start-up built (init.cpp)
    slinky_logf("unload: state freed");
    slinky_detach();     // ... and leave the JVM before this thread finishes
    slinky_logf("FINAL STATUS: UNLOADED");
    live_marker_release();   // from here the game is free to take a new client
    (void)self;
}

static DWORD engine_thread(void *self) {
    // First words into the same log the loader watches: until this line existed the engine's first write happened
    // deep inside slinky_setup, so a slow start and a dead start looked identical for minutes.
    slinky_logf("engine: thread up");
    if (live_marker_take()) {
        slinky_logf("FINAL STATUS: ALREADY_RUNNING");
        slinky_engine_exited = 1;
        return 0;   // the other engine holds the game; this image stays inert and its marker closed
    }
    engine_work = CreateEventW(nullptr, 0, 0, nullptr);
    engine_done = CreateEventW(nullptr, 0, 0, nullptr);
    engine_leave = CreateEventW(nullptr, 0, 0, nullptr);

    int err = slinky_setup(self);
    if (err) {
        slinky_logf("engine: setup failed (%d)", err);
        slinky_logf("FINAL STATUS: FAILED");
        // A start-up that died after arming the hooks must not free the state under the game: without a
        // successful disarm the image stays resident, inert (every native checks the shutdown flag).
        int hoff = slinky_disarm();
        if (hoff) {
            // Deliberately no slinky_engine_exited here. That flag is the bootstrap's permission to dlclose, and
            // dlclose unmaps the code the game is still calling: the hooks are in, so the classes patched by the
            // half-finished start-up still jump into this image on every frame. Staying mapped and resident is
            // the only safe reading of "the hooks stayed armed", and the reaper waits for a flag that never comes.
            slinky_logf("startup failed and the hooks stayed armed (%d): staying resident", hoff);
            return 0;
        }
        free_and_exit(self);
        slinky_engine_exited = 1;
        return 0;
    }
    slinky_logf("FINAL STATUS: RUNNING");

    for (;;) {
        // With a pass half finished this wait is not infinite. The game can stop painting - minimised, a world
        // being switched, a window closed - with half its classes vanilla and the other half still calling into
        // this image, and nothing else in the process holds the JVMTI environment that can hand the rest back.
        // A second without a frame means the frames are gone, so this thread finishes the pass itself; a render
        // thread that later comes back finds it done and tears down as usual.
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
        // 1 = the game is not vanilla yet and the render thread asks again on its next frame; an error means the
        // hooks stayed in and everything has to remain allocated, so the engine thread waits here for another
        // click exactly as it did when the whole pass was one call.
        if (!answer) break;
        if (answer != 1) slinky_logf("unload: the hooks are still in, everything stays as it is - try again");
    }
    WaitForSingleObject(engine_leave, INFINITE);
    Sleep(1000);   // the frames that entered a native before the hooks went out have to leave first
    slinky_logf("unload: engine thread awake, freeing the client");
    free_and_exit(self);
    slinky_engine_exited = 1;   // last word inside the image: the bootstrap may dlclose it now
    return 0;
}

// Post one restore step and wait for its answer: 0 = the game has its original code back, 1 = more frames to go,
// anything else = the error that stopped the pass. The wait is only as long as one class, and it has to stay a
// wait: this thread is standing inside the game's own rewritten call site, so it is where the next frame - and
// therefore the next step - comes from.
extern "C" int engine_disarm_step(void) {
    if (!engine_work || !engine_done) return ERR_NO_ENGINE_THREAD;
    SetEvent(engine_work);
    if (WaitForSingleObject(engine_done, 30000) != WAIT_OBJECT_0) return ERR_DISARM_TIMED_OUT;
    return engine_disarm_answer;
}

extern "C" void engine_leave_and_free(void) {
    if (engine_leave) SetEvent(engine_leave);
}

// ---- diagnostics: the process's stderr and this image's own slinky-native.log ----
// (launchers do not keep the game's stderr, and an unload problem must stay visible after the exit)
static char slinky_self_path[4096];

static void find_own_path(void) {
    if (slinky_self_path[0]) return;
    Dl_info info;
    if (dladdr((void *)engine_thread, &info) && info.dli_fname) {
        snprintf(slinky_self_path, sizeof slinky_self_path, "%s", info.dli_fname);
    }
}

static void get_image_log_path(char *out, size_t max_len) {
    out[0] = 0;
    find_own_path();
    if (!slinky_self_path[0]) return;
    snprintf(out, max_len, "%s", slinky_self_path);
    char *slash = strrchr(out, '/');
    if (!slash) { out[0] = 0; return; }
    if ((size_t)(out + max_len - slash - 1) < sizeof("slinky-native.log")) { out[0] = 0; return; }
    strcpy(slash + 1, "slinky-native.log");
}

extern "C" void slinky_log_write(const char *line) {
    char buf[600];
    int n = 0;
    for (const char *p = "slinkyFX: "; *p; p++) buf[n++] = *p;
    for (; *line && n < 597; line++) buf[n++] = *line;
    buf[n++] = '\n';
    buf[n] = 0;
    ssize_t ignored = write(2, buf, (size_t)n);   // the game's stderr, when a launcher kept it
    (void)ignored;
    char path[4200];
    get_image_log_path(path, sizeof path);
    // Two ways the client ends up with no log at all, and both used to pass in silence: the folder its own image
    // sits in cannot be named, or it can be named and is not writable (a read-only /tmp, a game running as
    // another user, the sticky bit). The loader is waiting on that file, so the only copy of the answer is the
    // game's stderr - which most launchers throw away, and that is precisely why this says it once, here.
    if (!path[0]) {
        static bool warned_path = false;
        if (!warned_path) {
            warned_path = true;
            static const char m[] = "slinkyFX: cannot work out where to write its log\n";
            ignored = write(2, m, sizeof m - 1);
        }
        return;
    }
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        static bool warned_open = false;
        if (!warned_open) {
            warned_open = true;
            char m[4400];
            int m_len = snprintf(m, sizeof m, "slinkyFX: cannot write its log at %s (%s)\n", path, strerror(errno));
            if (m_len > 0) ignored = write(2, m, (size_t)m_len);
        }
        return;
    }
    {
        size_t done = 0;
        while (done < (size_t)n) {
            ssize_t w = write(fd, buf + done, (size_t)n - done);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                break;
            }
            done += (size_t)w;
        }
        close(fd);
    }
}

// ---- OpenGL: glXGetProcAddress for everything the driver exports, libGL itself for the GL 1.1 core ----
// The GUI is drawn on the render thread's current context, which the game made with glX; dlopen'ing libGL here
// does not touch that context, it only finds the entry points.
extern "C" void *slinky_gl_get_proc(const char *name) {
    static void *gl;
    static void *(*glx_get_proc)(const unsigned char *);
    if (!gl) {
        gl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
        if (!gl) gl = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!gl) return 0;
        void *p = dlsym(gl, "glXGetProcAddressARB");
        if (!p) p = dlsym(gl, "glXGetProcAddress");
        glx_get_proc = (void *(*)(const unsigned char *))p;
    }
    void *addr = glx_get_proc ? glx_get_proc((const unsigned char *)name) : 0;
    if (addr) return addr;
    return dlsym(gl, name);   // the 1.1 core is in libGL itself, and glX returns null for it
}

// The bootstrap dlopen's the image, so start-up is its constructor. Loading the same file twice is not a second
// start: the loader raises the refcount and the constructor does not run again. A *different* copy of the client
// in a process that already has one is what the live marker in engine_thread turns away.
__attribute__((constructor)) static void slinky_on_load(void) {
    void *self = 0;
    Dl_info info;
    if (dladdr((void *)engine_thread, &info)) self = info.dli_fbase;
    pthread_t t;
    int rc = pthread_create(&t, 0, (void *(*)(void *))engine_thread, self);
    if (!rc) {
        pthread_detach(t);
    } else {
        // The bootstrap has already written "native engine loaded" by now, so without this the loader is left
        // waiting on a client that was never started, over a log that says the opposite.
        slinky_logf("engine: could not start its thread (%d)", rc);
        slinky_logf("FINAL STATUS: FAILED");
    }
}
