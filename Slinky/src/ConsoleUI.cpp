#include "ConsoleUI.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <random>
#include <string>

namespace Slinky {

CRITICAL_SECTION ConsoleUI::s_CriticalSection;
char ConsoleUI::s_StatusText[512] = { 0 };
char ConsoleUI::s_PhaseText[32] = { 0 };
std::atomic<bool> ConsoleUI::s_Running(false);
std::atomic<bool> ConsoleUI::s_Frozen(false);
std::atomic<bool> ConsoleUI::s_Ended(false);
HANDLE ConsoleUI::s_hAnimationThread = nullptr;
DWORD ConsoleUI::s_OriginalConsoleMode = 0;
HANDLE ConsoleUI::s_hConsole = INVALID_HANDLE_VALUE;

// The console's output codepage on the way in, so it goes back on the way out.
static UINT s_OriginalOutputCP = 0;

// Palette: bright white for the dog art/ground, slinky orange for phase header
// and countdown seconds, bright red for failure, neutral greys for the arrow,
// license and status lines.
static const char* const kArtWhite = "\x1b[97m";
static const char* const kPhaseOrange = "\x1b[38;5;208m";
static const char* const kFailRed = "\x1b[91m";
static const char* const kCountdownOrange = "\x1b[38;5;208m";
static const char* const kArrowGray = "\x1b[38;2;204;204;204m";
static const char* const kLicenseGray = "\x1b[38;2;153;153;153m";
static const char* const kStatusGray = "\x1b[38;2;176;176;176m";
static const char* const kReset = "\x1b[0m";

static const char* const kLicenseLine = "Your license never expires";

// The walking dog logo from the original loader. Six lines per frame; the
// trailing spaces keep the right edge stable while the legs move.
static const int kArtLines = 6;
static const int kLineWidth = 30;
static const char* const kDog[4][kArtLines] = {
    {
        R"( _                            )",
        R"(((                  ____      )",
        R"( \\________________( ( e\____o)",
        R"( (                     _____/ )",
        R"(  \___________________/       )",
        R"(  /   \           \  /        )"
    },
    {
        R"(  _                           )",
        R"( ((                 ____      )",
        R"( ||________________( ( e\____o)",
        R"( (                     _____/ )",
        R"(  \___________________/       )",
        R"(   )  |           |   )       )"
    },
    {
        R"(   _                          )",
        R"(  //                ____      )",
        R"( //________________( ( e\____o)",
        R"( (                     _____/ )",
        R"(  \___________________/       )",
        R"(   \  /           /   \       )"
    },
    {
        R"(  _                           )",
        R"( ((                 ____      )",
        R"( ||________________( ( e\____o)",
        R"( (                     _____/ )",
        R"(  \___________________/       )",
        R"(  |   )           )   |       )"
    }
};

static const char* const kDogError[kArtLines] = {
    R"(                              )",
    R"(                    ____      )",
    R"(  _________________( ( x\____o)",
    R"(//                     _____/ )",
    R"(||____________________/       )",
    R"(  |   |           |  |        )"
};

// Animated ground: a 25-character base of random '_'/'-' characters that is
// shifted one position per animation frame, exactly like the original TUI.
static const int kGroundLength = 25;
static const std::string& GroundBase() {
    static std::string base;
    if (base.empty()) {
        std::mt19937 rng(1);
        std::uniform_int_distribution<int> dist(0, 1);
        for (int i = 0; i < kGroundLength; ++i) {
            base += (dist(rng) == 0) ? '_' : '-';
        }
    }
    return base;
}

static std::string GroundLine(int frame) {
    const std::string& base = GroundBase();
    if (base.empty()) return std::string(kGroundLength, '_');
    int shift = ((frame % (int)base.size()) + (int)base.size()) % (int)base.size();
    return base.substr(shift) + base.substr(0, shift);
}

static void PrintLine(const char* color, const char* text) {
    printf("%s%s%s", color, text, kReset);
    int pad = kLineWidth - (int)strlen(text);
    for (int i = 0; i < pad; ++i) putchar(' ');
    printf("\x1b[K\n");
}

static void PrintArt(int frame, bool failed) {
    const char* const* lines = failed ? kDogError : kDog[((frame % 4) + 4) % 4];
    const char* color = failed ? kFailRed : kArtWhite;

    for (int row = 0; row < kArtLines; ++row) {
        PrintLine(color, lines[row]);
    }
    PrintLine(color, GroundLine(frame).c_str());
}

void ConsoleUI::PrintBanner() {
    PrintArt(0, false);
    fflush(stdout);
}

void ConsoleUI::Initialize() {
    InitializeCriticalSection(&s_CriticalSection);
    s_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    if (s_hConsole != INVALID_HANDLE_VALUE) {
        GetConsoleMode(s_hConsole, &s_OriginalConsoleMode);
        SetConsoleMode(s_hConsole, s_OriginalConsoleMode | 0x0004);
    }

    // Windows writes a console in the locale's legacy codepage, and every message that comes from the system -
    // "Access is denied." in the player's own language - is turned into UTF-8 bytes before it is printed. On a
    // console still set to 850 or 932 those bytes arrive as a row of accents nobody wrote. Asking the console
    // for UTF-8 is what makes the two agree, and the number it had before goes back on the way out.
    s_OriginalOutputCP = GetConsoleOutputCP();
    if (s_OriginalOutputCP != CP_UTF8) SetConsoleOutputCP(CP_UTF8);

    // The tab title is left to the console host: it defaults to the
    // executable path, which is exactly what the original loader showed.

    strncpy_s(s_PhaseText, "SCANNING", _TRUNCATE);
    strncpy_s(s_StatusText, "Waiting for Minecraft", _TRUNCATE);
}

void ConsoleUI::Shutdown() {
    StopAnimation();
    printf("\x1b[?25h");
    if (s_hConsole != INVALID_HANDLE_VALUE && s_OriginalConsoleMode != 0) {
        SetConsoleMode(s_hConsole, s_OriginalConsoleMode);
    }
    if (s_OriginalOutputCP != 0 && s_OriginalOutputCP != CP_UTF8) SetConsoleOutputCP(s_OriginalOutputCP);
    s_OriginalOutputCP = 0;
    DeleteCriticalSection(&s_CriticalSection);
}

void ConsoleUI::SetPhase(Phase phase) {
    const char* text = phase == Phase::Injecting ? "INJECTING"
                     : phase == Phase::Done      ? "DONE"
                     : phase == Phase::Failed    ? "FAILED"
                                                 : "SCANNING";
    EnterCriticalSection(&s_CriticalSection);
    strncpy_s(s_PhaseText, text, _TRUNCATE);
    LeaveCriticalSection(&s_CriticalSection);
}

void ConsoleUI::SetStatusText(const char* text) {
    EnterCriticalSection(&s_CriticalSection);
    strncpy_s(s_StatusText, text, _TRUNCATE);
    LeaveCriticalSection(&s_CriticalSection);
}

// Statuses ending in '"' or '.' are final answers; anything else is an
// in-progress step and gets the animated dots on the status line.
static bool StatusAnimates(const char* status) {
    size_t len = strlen(status);
    if (len == 0) return true;
    // Ignore trailing ANSI escapes when looking for the final punctuation.
    while (len > 0 && status[len - 1] == 'm') {
        const char* esc = strrchr(status, '\x1b');
        if (!esc) break;
        len = (size_t)(esc - status);
    }
    if (len == 0) return true;
    const char last = status[len - 1];
    return last != '"' && last != '.';
}

// The block drawn under the banner: white arrow + phase header, blank line,
// the gray license line, blank line, then the current status. The caller has
// already placed the cursor at the header line.
static void PrintPhaseLines(const char* phase, const char* status, int dotsFrame,
                            const char* middle, bool failed) {
    static const char* const dots[] = { "", ".", "..", "..." };
    const char* suffix = StatusAnimates(status) ? dots[dotsFrame & 3] : "";
    const char* phaseColor = failed ? kFailRed : kPhaseOrange;

    printf("%s>%s %s%s%s\x1b[K\n",
           kArrowGray, kReset, phaseColor, phase, kReset);
    printf("\x1b[K\n");
    printf("%s%s%s\x1b[K\n",
           kLicenseGray, middle ? middle : kLicenseLine, kReset);
    printf("\x1b[K\n");
    printf("%s%s%s%s\x1b[0m\x1b[K",
           kStatusGray, status, suffix, kReset);
    fflush(stdout);
}

// Redraw the dog, the ground and the phase block in place. The cursor must be
// on the status line when this is called.
static void PrintFrame(const char* phase, const char* status, int dotsFrame,
                       const char* middle = kLicenseLine, bool failed = false,
                       int artFrame = 0) {
    // Art (6) + ground (1) + blank (1) + block (5) = 13 lines drawn;
    // 12 lines sit above the status line.
    printf("\x1b[%dA\r", kArtLines + 1 + 5);
    PrintArt(artFrame, failed);
    printf("\x1b[K\n");
    PrintPhaseLines(phase, status, dotsFrame, middle, failed);
}

// Copy a message into a single printable line so it can live inside the
// five-line block without breaking the redraw geometry.
static void OneLine(const char* in, char* out, size_t outSize) {
    if (outSize == 0) return;
    size_t n = 0;
    for (const char* p = in ? in : ""; *p && n + 1 < outSize; ++p) {
        unsigned char c = (unsigned char)*p;
        out[n++] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    out[n] = '\0';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
}

void ConsoleUI::PrintError(const char* action, DWORD errorCode) {
    wchar_t* msgBuf = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msgBuf, 0, nullptr);

    char errorStr[512] = "unknown";
    if (msgBuf) {
        WideCharToMultiByte(CP_UTF8, 0, msgBuf, -1, errorStr, 512, nullptr, nullptr);
        LocalFree(msgBuf);

        size_t len = strlen(errorStr);
        while (len > 0 && (errorStr[len - 1] == '\r' || errorStr[len - 1] == '\n')) {
            errorStr[--len] = '\0';
        }
    }

    char line[768];
    sprintf_s(line, "%s failed with error %lu: %s", action, errorCode, errorStr);
    FinishFailure(line);
}

// Failure ending: freeze on the red error dog and ground, then count down.
void ConsoleUI::FinishFailure(const char* reason) {
    EnterCriticalSection(&s_CriticalSection);
    if (s_Ended) {
        LeaveCriticalSection(&s_CriticalSection);
        return;
    }
    s_Ended = true;
    s_Frozen = true;
    LeaveCriticalSection(&s_CriticalSection);

    SetPhase(Phase::Failed);
    StopAnimation();

    char middle[256];
    OneLine(reason, middle, sizeof(middle));
    if (middle[0] == '\0') strncpy_s(middle, "Injection failed", _TRUNCATE);

    char status[640];
    for (int seconds = 5; seconds >= 1; --seconds) {
        sprintf_s(status,
                  "Failed to inject. Loader will close in "
                  "%s%ds%s.",
                  kCountdownOrange, seconds, kStatusGray);
        EnterCriticalSection(&s_CriticalSection);
        PrintFrame(s_PhaseText, status, 0, middle, true, 0);
        LeaveCriticalSection(&s_CriticalSection);
        Sleep(1000);
    }

    printf("\x1b[?25h");
    fflush(stdout);
}

void ConsoleUI::PrintFailure(const char* message) {
    FinishFailure(message);
}

// Success ending: keep the dog walking and the ground scrolling while the
// countdown runs. The animation thread continues; we just update the status.
void ConsoleUI::FinishSuccess() {
    EnterCriticalSection(&s_CriticalSection);
    if (s_Ended) {
        LeaveCriticalSection(&s_CriticalSection);
        return;
    }
    s_Ended = true;
    // Leave s_Frozen false so the animation thread keeps moving the dog/ground.
    LeaveCriticalSection(&s_CriticalSection);

    SetPhase(Phase::Done);

    char status[640];
    for (int seconds = 5; seconds >= 1; --seconds) {
        sprintf_s(status,
                  "Injected successfully. Loader will close in "
                  "%s%ds%s.",
                  kCountdownOrange, seconds, kStatusGray);
        SetStatusText(status);
        Sleep(1000);
    }

    EnterCriticalSection(&s_CriticalSection);
    s_Frozen = true;
    LeaveCriticalSection(&s_CriticalSection);
    StopAnimation();

    printf("\x1b[?25h");
    fflush(stdout);
}

void ConsoleUI::StartAnimation() {
    if (s_Running) return;
    s_Running = true;
    printf("\x1b[?25l");
    // Draw the first frame immediately so the cursor is positioned on the
    // status line and no leftover banner lines sit above the animation.
    EnterCriticalSection(&s_CriticalSection);
    PrintFrame(s_PhaseText, s_StatusText, 0);
    LeaveCriticalSection(&s_CriticalSection);
    s_hAnimationThread = CreateThread(nullptr, 0, AnimationThread, nullptr, 0, nullptr);
}

void ConsoleUI::StopAnimation() {
    if (!s_Running) return;
    s_Running = false;
    if (s_hAnimationThread) {
        WaitForSingleObject(s_hAnimationThread, 1000);
        CloseHandle(s_hAnimationThread);
        s_hAnimationThread = nullptr;
    }
    printf("\x1b[?25h");
    fflush(stdout);
}

DWORD WINAPI ConsoleUI::AnimationThread(LPVOID lpParam) {
    (void)lpParam;
    int frame = 0;

    while (s_Running) {
        EnterCriticalSection(&s_CriticalSection);
        if (s_Frozen) {
            LeaveCriticalSection(&s_CriticalSection);
            break;
        }
        char currentStatus[512] = { 0 };
        char currentPhase[32] = { 0 };
        strncpy_s(currentStatus, s_StatusText, _TRUNCATE);
        strncpy_s(currentPhase, s_PhaseText, _TRUNCATE);
        PrintFrame(currentPhase, currentStatus, frame / 4, kLicenseLine, false, frame / 2);
        LeaveCriticalSection(&s_CriticalSection);

        frame++;
        Sleep(50);
    }

    // Freeze the block on the last status unless an ending block took over.
    EnterCriticalSection(&s_CriticalSection);
    if (!s_Frozen) {
        char currentStatus[512] = { 0 };
        char currentPhase[32] = { 0 };
        strncpy_s(currentStatus, s_StatusText, _TRUNCATE);
        strncpy_s(currentPhase, s_PhaseText, _TRUNCATE);
        PrintFrame(currentPhase, currentStatus, 0);
    }
    LeaveCriticalSection(&s_CriticalSection);

    return 0;
}

} // namespace Slinky
