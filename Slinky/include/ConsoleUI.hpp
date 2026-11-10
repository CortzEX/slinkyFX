#pragma once

#include <windows.h>
#include <string>
#include <atomic>

namespace Slinky {

class ConsoleUI {
public:
    // The header above the status block: SCANNING while the loader looks for
    // the game, INJECTING from the first injection step on, DONE once the
    // client is in, FAILED when injection did not complete.
    enum class Phase { Scanning, Injecting, Done, Failed };

    static void Initialize();
    static void Shutdown();

    static void PrintBanner();
    static void StartAnimation();
    static void StopAnimation();

    static void SetPhase(Phase phase);
    static void SetStatusText(const char* text);

    // Success ending: switches the block to the DONE header and runs the
    // "Injected successfully. Loader will close in Ns." countdown, then
    // returns so the caller can exit (closing the console window).
    static void FinishSuccess();

    // Failure ending: same block layout as FinishSuccess, with the FAILED
    // header and "Failed to inject. Loader will close in Ns." countdown.
    // `reason` is shown in the license line position (may be null).
    static void FinishFailure(const char* reason);

    // Diagnostic helpers. Both end the run through FinishFailure, so every
    // failure path shows the same countdown.
    static void PrintError(const char* action, DWORD errorCode);
    static void PrintFailure(const char* message);

private:
    static DWORD WINAPI AnimationThread(LPVOID lpParam);

    static CRITICAL_SECTION s_CriticalSection;
    static char s_StatusText[512];
    static char s_PhaseText[32];
    static std::atomic<bool> s_Running;
    static std::atomic<bool> s_Frozen;
    static std::atomic<bool> s_Ended;
    static HANDLE s_hAnimationThread;
    static DWORD s_OriginalConsoleMode;
    static HANDLE s_hConsole;
};

} // namespace Slinky
