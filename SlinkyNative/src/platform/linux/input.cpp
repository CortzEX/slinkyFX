// Linux input: the counterpart of the Win32 window hook in src/engine/protected/input.cpp.
//
// X11 hands this process no window procedure to subclass, so nothing here is hooked into the game. The pump runs
// at the end of the frame hook (engine/base/java_natives_frame.cpp, right after the module ticks) and reads the
// state LWJGL 2 measured during this frame's poll, then feeds the same InputEvent records to the same client
// handler the window procedure feeds on Windows. That is the whole input path for the ClickGUI, the key binds
// and the key-state table the modules gate on.
//
// Two choices are forced by LWJGL 2's shape rather than by taste:
//  - The state is read, never drained. Mouse.next()/Keyboard.next() would take events out of the one queue the
//    game itself empties every tick, and vanilla would lose the keys and clicks sitting in it. So keys are read
//    as an edge diff of Keyboard.keyDownBuffer, which means a press and release inside a single frame can pass
//    unnoticed, and a held key does not repeat the way Windows' WM_KEYDOWN auto-repeat makes it.
//  - A synthetic click cannot be posted as an event, because X11 events come from the X server. So
//    post_synthetic_click tells the client handler (exactly what the private Windows message used to do), presses
//    the button in the state array the game polls, and then runs the click through the game's own entry point.
//    The state array is rewritten in full by every poll, so the buttons the engine holds are pressed down again
//    on every frame instead of once.
//
// One difference is not fixable from here: the game reads the wheel from its own event queue, which only the X
// server writes, so scrolling the menu while a Minecraft screen is open also scrolls what the game scrolls.
#include "slinky/engine.h"
#include "slinky/jcall.h"

// ---- the event record the client handler takes; identical to protected/input.cpp ----
enum InputType { IN_MOVE = 1, IN_KEY = 2, IN_WHEEL = 3, IN_RAW = 4 };
struct InputEvent {
    int type;
    union {
        struct { int x, y; } move;                 // IN_MOVE: window position, y counted from the top
        struct {
            int code;                              // IN_KEY: client key code / mouse button (1 L, 2 R, 3 M, 4/5 X1/X2)
            char text[4];                          //   character input: UTF-8 (code 0)
            unsigned char down;
            unsigned char synthetic;               //   posted by the auto clicker
            char pad[2];
        } key;
        struct { float delta; } wheel;             // IN_WHEEL: notches (+ away from the user)
    };
};
typedef bool (*InputHandler)(InputEvent *e);
extern "C" InputHandler input_input_handler_hooked_into_game;

FN(char, key_code_to_char, (int key));                                                  // engine/math/vec_aabb.cpp
FN(void *, get_current_screen, (Jvm *jvm, void *minecraft));                            // engine/jni/classfile_hooks.cpp
FN(void *, java_get_gui_container_the_slot, (Jvm *jvm, void *gui_container));
FN(int, java_get_slot_slot_number, (Jvm *jvm, void *slot));
FN(void, java_gui_container_handle_mouse_click, (Jvm *jvm, void *object, int a, int b, int c));
FN(int, java_get_static_display_height, (Jvm *jvm));

// The game's own click entry points. The class-file hooks sit at Minecraft's call sites, not at the methods, so
// calling one of these runs the same pipeline a real click runs - pre-click event, the modules' chance to cancel
// it, vanilla's body with its own cooldown counters, post-click event.
extern "C" void hook_click_mouse(JNIEnv *env, jclass, void *minecraft);
extern "C" void hook_right_click_mouse(JNIEnv *env, jclass, void *minecraft);

// LWJGL 2 KEY_* -> the client's key code, the space windows_key_to_client_key() produces on Windows (0 is the
// text event, 1..5 the mouse buttons). The codes that have no virtual key of their own there - the numpad
// operators, Super, Menu, Pause, Print Screen - are absent here as well.
struct KeyMap { unsigned short lw; unsigned char client; };
static const KeyMap key_map[] = {
    {30, 26}, {48, 27}, {46, 28}, {32, 29}, {18, 30}, {33, 31}, {34, 32}, {35, 33}, {23, 34}, {36, 35},
    {37, 36}, {38, 37}, {50, 38}, {49, 39}, {24, 40}, {25, 41}, {16, 42}, {19, 43}, {31, 44}, {20, 45},
    {22, 46}, {47, 47}, {17, 48}, {45, 49}, {21, 50}, {44, 51},                                   // a-z
    {2, 7}, {3, 8}, {4, 9}, {5, 10}, {6, 11}, {7, 12}, {8, 13}, {9, 14}, {10, 15}, {11, 6},       // 1-9, 0
    {82, 16}, {79, 17}, {80, 18}, {81, 19}, {75, 20}, {76, 21}, {77, 22}, {71, 23}, {72, 24}, {73, 25},
    {59, 67}, {60, 68}, {61, 69}, {62, 70}, {63, 71}, {64, 72}, {65, 73}, {66, 74}, {67, 75}, {68, 76},
    {87, 77}, {88, 78}, {100, 79}, {101, 80}, {102, 81}, {103, 82},                               // F1-F16
    {39, 52}, {13, 53}, {51, 54}, {12, 55}, {52, 56}, {53, 57}, {41, 58}, {26, 59},
    {43, 60}, {27, 61}, {40, 62},                                                                 // ; , = - . / ` [ \ ] '
    {203, 63}, {200, 64}, {205, 65}, {208, 66},                                                   // left up right down
    {14, 83}, {15, 84}, {28, 85}, {156, 85}, {58, 86}, {1, 87}, {57, 88},
    {201, 89}, {209, 90}, {207, 91}, {199, 92}, {210, 94}, {211, 95}, {69, 96}, {70, 97},
    {42, 98}, {54, 99}, {29, 100}, {157, 101}, {56, 102}, {184, 103},                             // back .. right alt
};
static const unsigned key_count = sizeof key_map / sizeof key_map[0];
enum { LW_LSHIFT = 42, LW_RSHIFT = 54, MOUSE_BUTTONS = 5 };

static unsigned char kbd_reported[key_count];      // last state handed to the handler, per table entry
static unsigned char btn_reported[MOUSE_BUTTONS];  //   ditto, per mouse button
static unsigned char btn_forced;                   // buttons the engine is holding down (bit = button index)
static unsigned char *btn_state;                   // the button array the last pump read, for immediate presses
static unsigned char pending_clicks[8];            // presses post_synthetic_click queued for this frame's pump
static unsigned char pending_count;
static int last_move_x = -1, last_move_y = -1;

// the direct ByteBuffer a private static field of the input class points at, or 0
static unsigned char *polled_state(Jvm *jvm, jclass cls, jfieldID id, long *capacity) {
    if (!cls || !id) return 0;
    jobject buf = JCALLN(jvm, GetStaticObjectField, cls, id);
    if (!buf) return 0;                                    // not created yet, or already destroyed
    unsigned char *p = (unsigned char *)JCALLN(jvm, GetDirectBufferAddress, buf);
    if (!p) return 0;
    *capacity = JCALLN(jvm, GetDirectBufferCapacity, buf);
    return p;
}

// A press the game has to act on. Over the window procedure on Windows this was a message that went to the game
// and let it decide; here the decision is made in the open, from the same two facts vanilla uses: which screen
// is open, and whether the game holds the cursor.
static void deliver_click(Jvm *jvm, void *minecraft, int button, bool shift) {
    void *screen = get_current_screen(jvm, minecraft);
    if (screen) {
        if (!JCALLN(jvm, IsInstanceOf, screen, ids->GuiContainer)) return;
        void *slot = java_get_gui_container_the_slot(jvm, screen);   // the slot the game has under the cursor
        if (!slot) return;
        java_gui_container_handle_mouse_click(jvm, screen, java_get_slot_slot_number(jvm, slot), button,
                                             shift ? 1 : 0);
        return;
    }
    if (!JCALLN(jvm, GetBooleanField, minecraft, ids->Minecraft_inGameHasFocus)) return;
    if (button == 0) hook_click_mouse(jvm->env, 0, minecraft);
    else hook_right_click_mouse(jvm->env, 0, minecraft);
}

// ---- nothing to install: the pump reads the game's own state, so it needs no hook of its own ----
extern "C" void hook_game_window() {
}

// ---- undo what the pump left holding (called once while unloading) ----
extern "C" void restore_game_window() {
    if (btn_state) {
        for (int i = 0; i < MOUSE_BUTTONS; i++)
            if ((btn_forced >> i) & 1) btn_state[i] = 0;
    }
    btn_forced = 0;
    btn_state = 0;
    pending_count = 0;
    __builtin_memset(btn_reported, 0, sizeof btn_reported);
    __builtin_memset(kbd_reported, 0, sizeof kbd_reported);
    last_move_x = last_move_y = -1;
}

// ---- synthetic mouse click: left (or right) button down (or up), the auto clicker's only input write ----
extern "C" void post_synthetic_click(bool left, bool down) {
    int button = left ? 0 : 1;
    InputEvent e = {};
    e.type = IN_KEY;
    e.key.code = button + 1;
    e.key.down = down;
    e.key.synthetic = 1;
    bool consumed = input_input_handler_hooked_into_game && input_input_handler_hooked_into_game(&e);
    if (down) {
        if (consumed) return;                              // the menu took the click: the game never sees it
        btn_forced |= (unsigned char)(1u << button);
        btn_reported[button] = 1;
        if (btn_state) btn_state[button] = 1;
        if (pending_count < sizeof pending_clicks) pending_clicks[pending_count++] = (unsigned char)button;
    } else {
        btn_forced &= (unsigned char)~(1u << button);
        btn_reported[button] = 0;
        if (btn_state) btn_state[button] = 0;
    }
}

// ---- the frame's input: cursor, wheel, keys, buttons, then the presses the modules queued ----
extern "C" void input_poll_client_state(Jvm *jvm, void *minecraft) {
    if (!input_input_handler_hooked_into_game) return;

    long keys_capacity = 0, buttons_capacity = 0;
    const unsigned char *keys = polled_state(jvm, (jclass)jvm->ids->Keyboard, jvm->ids->Keyboard_keyDownBuffer, &keys_capacity);
    btn_state = polled_state(jvm, (jclass)jvm->ids->Mouse, jvm->ids->Mouse_buttons, &buttons_capacity);
    bool shift = keys && keys_capacity > LW_RSHIFT && (keys[LW_LSHIFT] || keys[LW_RSHIFT]);

    if (jvm->ids->Mouse_getX && jvm->ids->Mouse_getY) {
        int x = JCALL2(jvm, CallStaticIntMethod, (jclass)ids->Mouse, ids->Mouse_getX);
        int y = JCALL2(jvm, CallStaticIntMethod, (jclass)ids->Mouse, ids->Mouse_getY);
        if (x != last_move_x || y != last_move_y) {
            last_move_x = x;
            last_move_y = y;
            InputEvent e = {};
            e.type = IN_MOVE;
            e.move.x = x;
            e.move.y = java_get_static_display_height(jvm) - 1 - y;   // LWJGL counts from the bottom, the GUI from the top
            input_input_handler_hooked_into_game(&e);
        }
    }
    if (jvm->ids->Mouse_getDWheel) {
        int notches = JCALL2(jvm, CallStaticIntMethod, (jclass)ids->Mouse, ids->Mouse_getDWheel);
        if (notches) {
            InputEvent e = {};
            e.type = IN_WHEEL;
            e.wheel.delta = (float)notches / 120.0f;                  // Button4/5 are one notch, as WM_MOUSEWHEEL's 120
            input_input_handler_hooked_into_game(&e);
        }
    }

    for (unsigned i = 0; i < key_count; i++) {
        if (!keys || key_map[i].lw >= keys_capacity) continue;
        unsigned char down = keys[key_map[i].lw] ? 1 : 0;
        if (down == kbd_reported[i]) continue;
        kbd_reported[i] = down;
        InputEvent e = {};
        e.type = IN_KEY;
        e.key.code = key_map[i].client;
        e.key.down = down;
        input_input_handler_hooked_into_game(&e);
        char c = down ? key_code_to_char(key_map[i].client) : 0;      // the WM_CHAR that a press also produces
        if (!c) continue;
        if (shift && c >= 'a' && c <= 'z') c -= 'a' - 'A';
        InputEvent text = {};
        text.type = IN_KEY;
        text.key.down = 1;
        text.key.text[0] = c;
        input_input_handler_hooked_into_game(&text);
    }

    for (int i = 0; i < MOUSE_BUTTONS; i++) {
        if ((btn_forced >> i) & 1) {
            btn_reported[i] = 1;                                      // ours; post_synthetic_click already reported it
            continue;
        }
        if (!btn_state || i >= buttons_capacity) break;
        unsigned char down = btn_state[i] ? 1 : 0;
        if (down == btn_reported[i]) continue;
        btn_reported[i] = down;
        InputEvent e = {};
        e.type = IN_KEY;
        e.key.code = i + 1;
        e.key.down = down;
        input_input_handler_hooked_into_game(&e);
    }
    if (btn_state) {
        for (int i = 0; i < MOUSE_BUTTONS; i++)
            if ((btn_forced >> i) & 1 && i < buttons_capacity) btn_state[i] = 1;
    }

    for (unsigned char n = 0; n < pending_count; n++)
        deliver_click(jvm, minecraft, pending_clicks[n], shift);
    pending_count = 0;
}
