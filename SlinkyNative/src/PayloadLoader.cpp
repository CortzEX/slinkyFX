// PayloadLoader.cpp - hands the game over to the client.
//
// There is one client on both systems and both Minecraft versions: the C++ engine, which on Linux is a second
// image (libslinky-engine.so) that the loader staged next to this library. What is left here is the work only
// this library can do - find that file, take hold of it once and only once, and unmap it after its thread has
// finished. The Java payload this file used to unpack is gone: the engine is the client, so the jars, their
// embedding, and the class-definition machinery that fed them to the game went with them.
#include "PayloadLoader.hpp"
#include "JvmManager.hpp"
#include "Logger.hpp"
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#endif

// Directory separator and engine image name: the same pair of ideas spelled the two ways.
#ifdef _WIN32
#define SLINKY_DIRSEP L"\\"
#define SLINKY_DIRSEP_CH L'\\'
#define SLINKY_ENGINE_IMAGE L"slinky-engine.dll"
#else
#define SLINKY_DIRSEP L"/"
#define SLINKY_DIRSEP_CH L'/'
#define SLINKY_ENGINE_IMAGE L"libslinky-engine.so"
#endif

namespace SlinkyNative {

namespace {

std::wstring GetModuleDirectory(HMODULE hModule) {
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD len = 0;
    while ((len = GetModuleFileNameW(hModule, buf.data(), (DWORD)buf.size())) >= buf.size() - 1) {
        if (buf.size() >= 32767) break;
        buf.resize(buf.size() * 2);
    }
    if (len == 0) return std::wstring();
    std::wstring path(buf.data(), len);
    size_t lastSlash = path.find_last_of(SLINKY_DIRSEP_CH);
    if (lastSlash != std::wstring::npos) {
        path.resize(lastSlash);
    }
    return path;
}

} // namespace

InjectionStatus PayloadLoader::LoadAndStart(HMODULE hModule, const MinecraftInfo& mcInfo) {
    JNIEnv* env = JvmManager::GetEnv();
    if (!env) {
        Logger::Log(L"FINAL STATUS: FAILED");
        return InjectionStatus::Failed;
    }
    // The version and naming scheme the detector found are reported in its own log lines. Nothing here branches
    // on them any more, because there is only the one client left to start.
    (void)mcInfo;
    return LoadNativeEngine(hModule);
}

#ifdef _WIN32
InjectionStatus PayloadLoader::LoadNativeEngine(HMODULE hModule) {
    std::wstring dllDir = GetModuleDirectory(hModule);
    if (dllDir.empty()) dllDir = std::wstring(L".");

    std::wstring enginePath = dllDir + SLINKY_DIRSEP + SLINKY_ENGINE_IMAGE;

    // If the engine is already loaded (reload / second injection), just report success.
    HMODULE already = GetModuleHandleW(enginePath.c_str());
    if (!already) {
        HMODULE hEngine = LoadLibraryW(enginePath.c_str());
        if (!hEngine) {
            Logger::Log(L"native engine failed to load: %ls (error %lu)", enginePath.c_str(), GetLastError());
            Logger::Log(L"FINAL STATUS: FAILED");
            return InjectionStatus::Failed;
        }
        already = hEngine;
    }

    Logger::Log(L"native engine loaded (%ls)", enginePath.c_str());
    Logger::Log(L"Slinky is running");
    Logger::Log(L"FINAL STATUS: RUNNING");
    return InjectionStatus::Success;
}
#else

namespace {

// The engine never unmaps itself: its thread is the last thing running inside the image, and a thread that
// frees the code it is standing in takes the process down with it. So the engine only says that it has
// finished - in the variable it exports for this one purpose - and the module that loaded it does the unmap.
// That module is this one, and this is the wait.
struct EngineImage {
    void *image;
    volatile int *exited;
};

void *ReapEngineImage(void *arg) {
    EngineImage *engine = static_cast<EngineImage *>(arg);
    // The flag is the only reason this thread exists, so the caller has to have found it. Waiting without it
    // would mean unmapping the image while its thread may still be inside - which is the one thing the whole
    // handshake is there to avoid.
    while (!*engine->exited) usleep(50 * 1000);
    dlclose(engine->image);
    delete engine;
    return nullptr;
}

} // namespace

InjectionStatus PayloadLoader::LoadNativeEngine(HMODULE hModule) {
    std::wstring engineDir = GetModuleDirectory(hModule);
    if (engineDir.empty()) engineDir = std::wstring(L".");
    std::wstring enginePath = engineDir + SLINKY_DIRSEP + SLINKY_ENGINE_IMAGE;
    std::string native = w32compat::WideToUtf8(enginePath);

    // RTLD_NOLOAD asks whether the file is already in the process without taking a second hold of it. Two
    // holds would not mean two clients - an image already mapped does not run its start-up again - it would
    // mean one client that this call pretends to have started. Saying it here also saves the loader from
    // waiting out its budget for a status line that was never going to be written.
    void *resident = dlopen(native.c_str(), RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (resident) {
        dlclose(resident);   // a probe only; the client already running keeps the hold it took at load
        Logger::Log(L"Slinky is already active; nothing was started");
        Logger::Log(L"FINAL STATUS: ALREADY_RUNNING");
        return InjectionStatus::AlreadyRunning;
    }

    // RTLD_LOCAL: the engine's five handshake symbols are the whole of what it offers the process, and the
    // rest stays inside it - nothing here may answer a lookup the JVM or another library meant to make.
    void *image = dlopen(native.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!image) {
        const char *err = dlerror();
        Logger::Log(L"native engine failed to load: %ls (%hs)", enginePath.c_str(), err ? err : "no message from the loader");
        Logger::Log(L"FINAL STATUS: FAILED");
        return InjectionStatus::Failed;
    }

    // dlopen has run the image's constructor, and that constructor is what starts the engine thread. What
    // happens next is the engine's own - including the FINAL STATUS line the loader waits for, which it writes
    // into the same slinky-native.log as this file, because the loader staged both into one folder and each
    // writes beside itself. Nothing here reports a status: from this side the engine is only a promise.
    Logger::Log(L"native engine loaded (%hs)", native.c_str());

    volatile int *exited = static_cast<volatile int *>(dlsym(image, "slinky_engine_exited"));
    if (!exited) {
        // The build checks that the engine exports this name, so reaching here means the two files in front of
        // us are not the pair that was made together. Leave the image mapped: it is inert after an unload and
        // harmless to the game, and unmapping it from here would be a guess about where its thread is.
        Logger::Log(L"native engine will stay mapped after unload: it does not carry slinky_engine_exited");
        return InjectionStatus::Success;
    }

    EngineImage *engine = new EngineImage{image, exited};
    pthread_t reaper;
    if (pthread_create(&reaper, nullptr, ReapEngineImage, engine) == 0) {
        pthread_detach(reaper);
    } else {
        // No wait, so no unmap: the same as above - after an unload the image stays mapped, inert (every native
        // checks the shutdown flag) and harmless to the game, but a second client would need the game restarted.
        Logger::Log(L"native engine will stay mapped after unload: no thread could wait for it to finish");
        delete engine;
    }
    return InjectionStatus::Success;
}
#endif

} // namespace SlinkyNative
