// ---------------------------------------------------------------------------
// ConsoleUI.cpp (Linux) - same banner/phase block/status behaviour as the
// Windows build; Win32 console APIs replaced with pthreads + ANSI escapes.
// ---------------------------------------------------------------------------
#include "ConsoleUI.hpp"
#include "Compat.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <mutex>
#include <thread>
#include <string>
#include <random>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Slinky {

namespace {
std::mutex s_Mutex;
std::thread s_AnimThread;
} // namespace

char ConsoleUI::s_StatusText[512] = { 0 };
char ConsoleUI::s_PhaseText[32] = { 0 };
std::atomic<bool> ConsoleUI::s_Running(false);
std::atomic<bool> ConsoleUI::s_Frozen(false);
std::atomic<bool> ConsoleUI::s_Ended(false);

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
    // The tab title is just the executable path.
    char exePath[4096] = { 0 };
    ssize_t n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n <= 0) {
        n = snprintf(exePath, sizeof(exePath), "slinky-loader");
    }
    exePath[n] = '\0';
    printf("\x1b]0;%s\x07", exePath);   // SetConsoleTitleW equivalent

    strncpy(s_PhaseText, "SCANNING", sizeof(s_PhaseText) - 1);
    s_PhaseText[sizeof(s_PhaseText) - 1] = '\0';
    SetStatusText("Waiting for Minecraft");
}

void ConsoleUI::Shutdown() {
    StopAnimation();
    printf("\x1b[?25h");
    fflush(stdout);
}

void ConsoleUI::SetPhase(Phase phase) {
    const char* text = phase == Phase::Injecting ? "INJECTING"
                     : phase == Phase::Done      ? "DONE"
                     : phase == Phase::Failed    ? "FAILED"
                                                 : "SCANNING";
    std::lock_guard<std::mutex> lock(s_Mutex);
    strncpy(s_PhaseText, text, sizeof(s_PhaseText) - 1);
    s_PhaseText[sizeof(s_PhaseText) - 1] = '\0';
}

void ConsoleUI::SetStatusText(const char* text) {
    // The block redraws the status on a single line; anything that would
    // wrap (newlines, escapes, very long cmdline-derived titles) corrupts the
    // animation, so sanitize to printable ASCII and cap to the terminal width.
    char clean[512];
    size_t n = 0;
    for (const char* p = text; *p && n < sizeof(clean) - 1; ++p) {
        unsigned char c = (unsigned char)*p;
        clean[n++] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    clean[n] = '\0';

    int cols = 76;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 12) cols = ws.ws_col - 8;
    // The three dots and the terminator are written at cols-1..cols+2, so the row can never be wider than the
    // buffer they land in - a terminal of 518 columns or more used to write past it.
    if (cols > (int)sizeof(clean) - 3) cols = (int)sizeof(clean) - 3;
    if (n > (size_t)cols) {
        clean[cols - 1] = '.';
        clean[cols] = '.';
        clean[cols + 1] = '.';
        clean[cols + 2] = '\0';
    }

    std::lock_guard<std::mutex> lock(s_Mutex);
    strncpy(s_StatusText, clean, sizeof(s_StatusText) - 1);
    s_StatusText[sizeof(s_StatusText) - 1] = '\0';
}

// Statuses ending in '"' or '.' are final answers; anything else is an
// in-progress step and gets the animated dots on the status line.
static bool StatusAnimates(const char* status) {
    size_t len = strlen(status);
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

void ConsoleUI::PrintError(const char* action, int errorCode) {
    char errorStr[512] = "unknown";
    if (errorCode != 0) {
        const char* msg = strerror(errorCode);
        if (msg) {
            strncpy(errorStr, msg, sizeof(errorStr) - 1);
            errorStr[sizeof(errorStr) - 1] = '\0';
        }
    }

    char line[768];
    snprintf(line, sizeof(line), "%s failed with error %d: %s", action, errorCode, errorStr);
    FinishFailure(line);
}

// Failure ending: freeze on the red error dog and ground, then count down.
void ConsoleUI::FinishFailure(const char* reason) {
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_Ended) return;
        s_Ended = true;
        s_Frozen = true;
    }

    SetPhase(Phase::Failed);
    StopAnimation();

    char middle[256];
    OneLine(reason, middle, sizeof(middle));
    if (middle[0] == '\0') {
        strncpy(middle, "Injection failed", sizeof(middle) - 1);
        middle[sizeof(middle) - 1] = '\0';
    }

    char status[640];
    for (int seconds = 5; seconds >= 1; --seconds) {
        snprintf(status, sizeof(status),
                 "Failed to inject. Loader will close in "
                 "%s%ds%s.",
                 kCountdownOrange, seconds, kStatusGray);
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            PrintFrame(s_PhaseText, status, 0, middle, true, 0);
        }
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
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_Ended) return;
        s_Ended = true;
        // Leave s_Frozen false so the animation thread keeps moving the dog/ground.
    }

    SetPhase(Phase::Done);

    char status[640];
    for (int seconds = 5; seconds >= 1; --seconds) {
        snprintf(status, sizeof(status),
                 "Injected successfully. Loader will close in "
                 "%s%ds%s.",
                 kCountdownOrange, seconds, kStatusGray);
        SetStatusText(status);
        Sleep(1000);
    }

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Frozen = true;
    }
    StopAnimation();

    printf("\x1b[?25h");
    fflush(stdout);
}

void ConsoleUI::StartAnimation() {
    if (s_Running.exchange(true)) return;
    printf("\x1b[?25l");
    // Draw the first frame immediately so the cursor is positioned on the
    // status line and no leftover banner lines sit above the animation.
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        PrintFrame(s_PhaseText, s_StatusText, 0);
    }
    s_AnimThread = std::thread(AnimationThread);
}

void ConsoleUI::StopAnimation() {
    if (!s_Running.exchange(false)) return;
    if (s_AnimThread.joinable()) s_AnimThread.join();
    printf("\x1b[?25h");
    fflush(stdout);
}

void ConsoleUI::AnimationThread() {
    int frame = 0;

    while (s_Running) {
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            if (s_Frozen) break;
            PrintFrame(s_PhaseText, s_StatusText, frame / 4, kLicenseLine, false, frame / 2);
        }

        frame++;
        Sleep(50);
    }

    // Freeze the block on the last status unless an ending block took over.
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (!s_Frozen) {
        PrintFrame(s_PhaseText, s_StatusText, 0);
    }
}

} // namespace Slinky
