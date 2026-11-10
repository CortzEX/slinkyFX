// GUI - ClickGUI helper callbacks (scale table, input filter, open/close, layout).
#include "slinky/engine.h"

struct V4 { float x, y, z, w;
#ifdef __linux__
    ~V4() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct InputEvt { int type; int code; int padding_08; unsigned char down; };   // +0x00 / +0x04 / +0x0c
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };  // +0x10 / +0x18
struct Notice { int kind; int padding_04; int value; char rest[0x3c]; };
extern "C" unsigned gui_scale_value;         // "scale"
extern "C" const float gui_scale_table[];   // scale table
extern "C" unsigned char gui_gui_open;    // gui open
extern "C" char gui_module_descriptor[];           // GUI module descriptor
extern "C" int gui_toggle_key;              // toggle key
extern "C" unsigned char gui_allow_input_value;    // "allow_input"
extern "C" unsigned char gui_compact_value;    // "compact"
extern "C" unsigned char gui_wide_value;    // "wide"
extern "C" char gui_state_block[];           // gui state
extern "C" V4 gui_accent_colour;
extern "C" unsigned char gui_compact_mode;
extern "C" unsigned char gui_wide_mode;
extern "C" unsigned char gui_shutting_down;    // shutting down
extern "C" float gui_accent_color_setting_field_0x8;
extern "C" float gui_accent_color_setting_field_0xc;
extern "C" float gui_accent_color_setting_field_0x10;

FN(unsigned char, handle_gui_input, (void *state, void *evt));
FN(void, queue_module_toggle, (void *module_manager, void *module, int enabled));
FN(unsigned char, java_call_static_mouse_is_grabbed, (void *jvm));
FN(void, java_call_static_mouse_set_grabbed, (void *jvm, int x));
FN(unsigned char, java_call_mouse_helper_grab_cursor, (void *jvm, void *minecraft));
FN(int, java_get_static_display_width, (void *jvm));
FN(int, java_get_static_display_height, (void *jvm));
FN(void, java_call_static_mouse_set_cursor_position, (void *jvm, int x, int y));
FN(double, time_now, ());                 // high-resolution seconds
FN(void, set_tab_animation, (void *state, double v));   // stores a double at state + 0x498
FN(unsigned char, is_in_game, (void *jvm, void *object));
FN(void, java_set_minecraft_in_game_focus, (void *jvm, void *object, int value));
FN(void *, java_get_minecraft_current_screen, (void *jvm, void *object));
FN(void *, get_local_player, (void *jvm, void *object));
FN(void, close_popup_now, (void *state));
FN(void, reset_drag_state, (void *state));
FN(int, save_settings, (void *module_manager, void *profile_list));
FN(void, dispatch_event, (void *jvm, Notice *n));
FN(V4, nvg_rgb_f, (float a, float b, float c));
FN(void, draw_gui_frame, (void *state, void *event, void *other_object, void *y));

extern "C" unsigned char gui_owns_cursor, gui_focus_before_open;
extern "C" void slinky_logf(const char *fmt, ...);
static unsigned char grab_path_logged;

// ---- the camera must not follow the cursor while the menu owns it ----
// Vanilla turns the camera from MouseHelper.mouseDeltaX/mouseDeltaY in its game loop, gated on
// Minecraft.inGameHasFocus. The menu is not a GuiScreen, so nothing else ever clears that flag, and the view
// drifts behind the cursor while it is being used inside the menu. Where the helper reads those deltas from is
// a separate question and mods answer it differently - gui_on_toggle has the way back out.
static void take_cursor(void *jvm, void *minecraft) {
    gui_focus_before_open = is_in_game(jvm, minecraft) ? 1 : 0;   // read the real flag before the override is armed
    gui_owns_cursor = 1;
    java_set_minecraft_in_game_focus(jvm, minecraft, 0);
}

static void give_cursor_back(void *jvm, void *minecraft) {
    if (!gui_owns_cursor)
        return;
    gui_owns_cursor = 0;
    // A screen of the game's own is drawn with the system cursor and keeps the camera still. Handing the mouse
    // back to the world over one left the player a menu they could see but not click and a view that turned
    // while they aimed at nothing - so the screen keeps the cursor, and because the focus flag goes to 0 here,
    // is_in_game() says no to the grab the close path asks for next.
    if (java_get_minecraft_current_screen(jvm, minecraft)) {
        java_set_minecraft_in_game_focus(jvm, minecraft, 0);
        return;
    }
    // a world can have loaded while the menu was open with no focus to save; that one still ends focused
    java_set_minecraft_in_game_focus(jvm, minecraft, gui_focus_before_open || get_local_player(jvm, minecraft) != 0);
}

extern "C" float gui_get_scale() {
    return gui_scale_table[gui_scale_value];
}

extern "C" unsigned char gui_handle_input_event(InputEvt *evt, unsigned char shift, void *other_object) {
    if (gui_gui_open == 1 && handle_gui_input(gui_state_block, evt))
        return 1;
    int type = evt->type;
    if (type == 2 && evt->down == 1 && evt->code != 0) {
        unsigned char open = gui_gui_open;
        int code = evt->code;
        if (!shift && code == gui_toggle_key) {
            queue_module_toggle(other_object, gui_module_descriptor, open ^ 1);
            return 1;
        }
        if (code == 0x57 && open) {
            queue_module_toggle(other_object, gui_module_descriptor, 0);
            return 1;
        }
    }
    if (gui_gui_open != 1)
        return 0;
    // Moves and raw input go on to LWJGL: the menu is not a GuiScreen, so these messages are the only thing
    // keeping LWJGL's cursor state current, and a client whose deltas do come from them would otherwise
    // measure the whole trip made inside the menu as one delta when the menu lets go. The camera stays still
    // while the menu is up because of the focus flag (take_cursor) and the delta the frame hook empties,
    // and the helper that reads its deltas elsewhere is reset on the way out (gui_on_toggle).
    // The scroll wheel stays swallowed: passing it on would let the menu's scrolling change the held item.
    if (type != 2)
        return type == 1 || type == 4 ? 0 : 1;
    if (!gui_allow_input_value)
        return 1;
    if ((unsigned)evt->code <= 5)
        return 1;
    return 0;
}

extern "C" void gui_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (java_call_static_mouse_is_grabbed(ctx->jvm)) {
        java_call_static_mouse_set_grabbed(ctx->jvm, 0);
        return;
    }
    EPILOG_NOP();
}

extern "C" void gui_on_toggle(EvHook *hook) {
    if (hook->ev != gui_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    void *jvm = ctx->jvm;
    if (hook->on == 1) {
        take_cursor(jvm, ctx->minecraft);
        if (java_call_static_mouse_is_grabbed(jvm)) {
            int display_width = java_get_static_display_width(ctx->jvm);
            int hgt = java_get_static_display_height(ctx->jvm);
            java_call_static_mouse_set_cursor_position(ctx->jvm, display_width / 2, hgt / 2);
            java_call_static_mouse_set_grabbed(ctx->jvm, 0);
        }
        set_tab_animation(gui_state_block, time_now());
        return;
    }
    give_cursor_back(jvm, ctx->minecraft);
    // Grab through the game's own helper rather than LWJGL. It does the same setGrabbed(true) and, on top of
    // it, resets whatever delta source the helper reads from. RawInput replaces this class with one fed by a
    // device-sampling thread of its own, which neither these window messages nor Mouse.getDX()/getDY() reach,
    // so without that reset the entire trip the cursor made inside the menu is still queued when focus comes
    // back - and the game spends all of it on the camera in the one frame in which it first reads it.
    if (is_in_game(jvm, ctx->minecraft)) {
        unsigned char via_helper = java_call_mouse_helper_grab_cursor(ctx->jvm, ctx->minecraft);
        if (!via_helper)
            java_call_static_mouse_set_grabbed(ctx->jvm, 1);
        if (!grab_path_logged) {
            grab_path_logged = 1;
            slinky_logf("gui: cursor handed back through %s", via_helper ? "MouseHelper" : "LWJGL");
        }
    }
    close_popup_now(gui_state_block);
    reset_drag_state(gui_state_block);
    if (gui_shutting_down)
        return;
    int code = save_settings(ctx->module_manager, ctx->profile_list);
    if (!code)
        return;
    Notice n = {};
    n.kind = 0xc;
    n.value = code;
    dispatch_event(((Ctx *)ctx)->module_manager, &n);
}

extern "C" void gui_on_hud_tick(unsigned char *h) {
    h[0x10] |= 3;
}

extern "C" void gui_on_hud_render(EvHook *hook) {
    Ctx *ctx = hook->ctx;
    float x = gui_accent_color_setting_field_0x8, y = gui_accent_color_setting_field_0xc, z = gui_accent_color_setting_field_0x10;
    gui_accent_colour = nvg_rgb_f(x, y, z);
    gui_compact_mode = gui_compact_value;
    gui_wide_mode = gui_wide_value;
    void *event = hook->ev;
    void *module_manager = ctx->module_manager;
    void *profile_list = ctx->profile_list;
    draw_gui_frame(gui_state_block, event, module_manager, profile_list);
}

extern "C" void gui_on_shutdown(Hook *hook) {
    give_cursor_back(hook->ctx->jvm, hook->ctx->minecraft);   // the client is ours to hand back, menu open or not
    gui_shutting_down = 1;
}
