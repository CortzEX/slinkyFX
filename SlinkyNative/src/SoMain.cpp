// ---------------------------------------------------------------------------
// SoMain.cpp - Linux entry point for libslinky-native.so.
//
// Counterpart of DllMain.cpp: instead of DLL_PROCESS_ATTACH + CreateThread,
// the constructor attribute spawns a detached worker thread that runs the
// exact same sequence (Logger -> JvmManager -> MinecraftDetector ->
// PayloadLoader).  GCC has no SEH, so the __try/__except guard of the
// Windows build is not available here; the worker keeps its body fail-soft
// (every stage logs FINAL STATUS: FAILED instead of unwinding).
// ---------------------------------------------------------------------------
#include <dlfcn.h>
#include <pthread.h>
#include "Logger.hpp"
#include "JvmManager.hpp"
#include "MinecraftDetector.hpp"
#include "PayloadLoader.hpp"

namespace {

// The handle is this library's own base address, and every path the worker builds - the log file and the
// engine image - hangs off the folder it was loaded from. The loader stages both .so files into a folder of
// its own and watches the log inside that folder, so a worker that asked the running executable where it was
// would write beside java and the loader would wait out its whole budget for a line that is never coming.
void* SlinkyNativeWorker(void* imageHandle) {
    HMODULE self = static_cast<HMODULE>(imageHandle);
    SlinkyNative::Logger::Initialize(self);
    SlinkyNative::Logger::Log(L"worker: start-up begins");

    if (!SlinkyNative::JvmManager::Initialize()) {
        SlinkyNative::Logger::Log(L"worker: could not take hold of the JVM");
        SlinkyNative::Logger::Log(L"FINAL STATUS: FAILED");
        return nullptr;
    }

    SlinkyNative::MinecraftInfo mcInfo;
    if (!SlinkyNative::MinecraftDetector::Detect(mcInfo)) {
        SlinkyNative::Logger::Log(L"worker: this process does not look like a supported Minecraft");
        SlinkyNative::Logger::Log(L"FINAL STATUS: FAILED");
        return nullptr;
    }

    // Same return handling as RunWorker in DllMain.cpp: LoadAndStart logs the
    // FINAL STATUS line itself (RUNNING / ALREADY_RUNNING / FAILED).
    SlinkyNative::PayloadLoader::LoadAndStart(self, mcInfo);
    return nullptr;
}

} // namespace

__attribute__((constructor))
static void SlinkyNative_OnLoad() {
    Dl_info info;
    void* self = dladdr((void*)SlinkyNativeWorker, &info) ? info.dli_fbase : nullptr;

    // Logging begins here, on the thread that ran the constructor, and writes its first line before anything
    // else can fail. The worker used to be the only writer, so a worker that never got scheduled - a thread born
    // inside a traced process is born stopped - left the library mapped, inert and completely silent while the
    // loader waited out its whole budget. One line from here separates "the image never ran" from "it ran and
    // the worker did not", which are different bugs with the same symptom.
    SlinkyNative::Logger::Initialize(self);
    SlinkyNative::Logger::Log(L"bootstrap: image loaded, starting worker");

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    int rc = pthread_create(&thread, &attr, SlinkyNativeWorker, self);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        // This used to be discarded on the grounds that the logger was not up yet. It is up now, and a worker
        // that was never created is exactly the silence the loader cannot interpret.
        SlinkyNative::Logger::Log(L"bootstrap: could not start its worker thread (error %d)", rc);
        SlinkyNative::Logger::Log(L"FINAL STATUS: FAILED");
    }
}

// Parity with DLL_PROCESS_DETACH.
__attribute__((destructor))
static void SlinkyNative_OnUnload() {
    SlinkyNative::JvmManager::Shutdown();
    SlinkyNative::Logger::Shutdown();
}
