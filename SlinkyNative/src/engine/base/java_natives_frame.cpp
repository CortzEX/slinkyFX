// 0x7000: Java native entry points (SlinkyHooks13): wrap the JNIEnv in a Jvm handle and forward
#include "slinky/engine.h"
#include "slinky/jids.h"
extern "C" JIds *java_natives_world_cached_jni_ids;
extern "C" void slinky_logf(const char *fmt, ...);
extern "C" char *ptr_to_frame_module_settings;   // -> module settings block
FN(void, render_world_events, (JNIEnv *env, int key, bool down));
FN(void, hooks_first_frame_init, ());   // engine/protected/hooks.cpp: load the OpenGL entry points, build the menu
FN(void, tick_rotation, (Jvm *jvm));
FN(void, set_local_player_max_hurt_time, (Jvm *jvm));
FN(unsigned char, world_is_loaded, (Jvm *jvm));
// The unload sequence sets java_natives_frame_client_shutting_down_natives_check ("client is shutting down") before slinky_teardown frees every
// global about a second later. The JVM can still dispatch any of these natives in that window (the
// retransform is not guaranteed to have unwired every patched call site: see the java_set_render_manager_player_view_y/serialize_settings
// unload crashes in hs_err pid6708/pid2260), so every entry reachable through the hooks checks the flag
// first and does nothing.
extern "C" unsigned char java_natives_frame_client_shutting_down_natives_check;
extern "C" void hook_post_world_clear_1_7(JNIEnv *env, jclass, int key) { if (!java_natives_frame_client_shutting_down_natives_check) render_world_events(env, key, false); }
extern "C" void hook_post_world_clear_1_8(JNIEnv *env, jclass, int key) { if (!java_natives_frame_client_shutting_down_natives_check) render_world_events(env, key, true); }
extern "C" unsigned char hook_is_display_active_prerender(JNIEnv *env, jclass) {
    if (java_natives_frame_client_shutting_down_natives_check) return 1;
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    tick_rotation(&jvm);
    if (ptr_to_frame_module_settings[0x14] == 1) set_local_player_max_hurt_time(&jvm);
    // The value the game asked for, and it is not a detail: this native stands in for Display.isActive() inside
    // the render loop, which decides whether the window still has the mouse, whether the game pauses itself, and
    // whether the cursor gets handed to a screen. Returning nothing left the game reading whatever the last call
    // happened to leave in the register.
    return world_is_loaded(&jvm);
}

struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
FN(unsigned char, is_silent_rotation_active, ());
FN(Rot, get_silent_rotation, ());
FN(void *, get_minecraft, (Jvm *jvm));
FN(void *, get_local_player, (Jvm *jvm, void *minecraft));
FN(void *, get_minecraft_render_global, (Jvm *jvm, void *minecraft));
FN(void, set_render_manager_boolean, (Jvm *jvm, void *render_manager, bool on));
FN(void, apply_rotation_to_player, (Jvm *jvm, void *player));
FN(void, set_silent_rotation_active, (Jvm *jvm, void *player, bool on));
FN(void, call_orient_camera, (Jvm *jvm, void *object, float t));
FN(Rot, get_entity_rotation, (Jvm *jvm, void *entity));
FN(Rot, get_entity_prev_rotation, (Jvm *jvm, void *entity));
FN(void, set_entity_rotation, (Jvm *jvm, void *entity, Rot rotation));
FN(void, set_entity_prev_rotation, (Jvm *jvm, void *entity, Rot rotation));
FN(void, set_active_render_info, (Jvm *jvm, void *active_render_info, bool b));
// camera orientation hook: with silent rotations active, orient using the server rotation
extern "C" void hook_orient_camera(JNIEnv *env, jclass, void *receiver, float t) {
    if (java_natives_frame_client_shutting_down_natives_check) return;
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    if (is_silent_rotation_active()) {
        void *minecraft = get_minecraft(&jvm);
        void *player = get_local_player(&jvm, minecraft);
        set_render_manager_boolean(&jvm, get_minecraft_render_global(&jvm, minecraft), true);
        apply_rotation_to_player(&jvm, player);
        set_silent_rotation_active(&jvm, player, true);
        call_orient_camera(&jvm, receiver, t);
        set_silent_rotation_active(&jvm, player, false);
    } else {
        call_orient_camera(&jvm, receiver, t);
    }
}
// same for the other hook: temporarily swap in the silent rotation around the call
extern "C" void hook_update_active_render_info(JNIEnv *env, jclass, void *receiver, unsigned char b) {
    if (java_natives_frame_client_shutting_down_natives_check) return;
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    if (is_silent_rotation_active()) {
        void *minecraft = get_minecraft(&jvm);
        void *player = get_local_player(&jvm, minecraft);
        Rot rotation = get_entity_rotation(&jvm, player);
        Rot entity_prev_rotation = get_entity_prev_rotation(&jvm, player);
        Rot silent_rotation = get_silent_rotation();
        set_entity_rotation(&jvm, player, silent_rotation);
        set_entity_prev_rotation(&jvm, player, silent_rotation);
        set_active_render_info(&jvm, receiver, b != 0);
        set_entity_rotation(&jvm, player, rotation);
        set_entity_prev_rotation(&jvm, player, entity_prev_rotation);
    } else {
        set_active_render_info(&jvm, receiver, b != 0);
    }
}

// ---- tick hooks: build the module Ctx, dispatch pre-event (0xe), run the game method, dispatch post (0xf) ----
extern "C" char java_natives_world_module_manager[], java_natives_frame_module_manager_target_state[];
extern "C" void *init_profile_list, *java_natives_world_key_state_table_one_byte;
extern "C" char *ptr_to_gui_module_descriptor;
struct HCtx { Jvm *jvm; void *module_manager; void *profile_list; void *minecraft; void *world; void *player; void *key_state_table; void *target; unsigned char in_game; unsigned char padding_41[3]; int padding_44; };
struct HEvent { int type; int padding_04; HCtx *ctx; unsigned char flag, cancel; char rest[0x36]; };
FN(void *, get_world, (Jvm *jvm, void *minecraft));
FN(unsigned char, is_in_game, (Jvm *jvm, void *minecraft));
FN(void, tick_pre_click, (HCtx *ctx, void *state));
FN(void, dispatch_event, (void *mgr, HEvent *e));
FN(void, java_call_minecraft_click_mouse, (Jvm *jvm, void *minecraft));
FN(void, java_call_minecraft_right_click_mouse, (Jvm *jvm, void *minecraft));
FN(void, tick_click, (HCtx *ctx));
FN(void, tick_click_done, (HCtx *ctx));
static inline __attribute__((always_inline)) void make_ctx(HCtx *ctx, Jvm *jvm, void *minecraft) {
    void *world = get_world(jvm, minecraft);
    void *player = get_local_player(jvm, minecraft);
    ctx->jvm = jvm;
    ctx->module_manager = java_natives_world_module_manager;
    ctx->profile_list = init_profile_list;
    ctx->minecraft = minecraft;
    ctx->world = world;
    ctx->player = player;
    ctx->key_state_table = java_natives_world_key_state_table_one_byte;
    ctx->target = java_natives_frame_module_manager_target_state;
    ctx->in_game = ptr_to_gui_module_descriptor[0x14] ? 0 : is_in_game(jvm, minecraft);
    ctx->padding_41[0] = 0; ctx->padding_41[1] = 0; ctx->padding_41[2] = 0;
    *(int *)&ctx->padding_41[0] = 0;
    ctx->padding_44 = 0;
}
// engine/jni/silent_rotation_and_chat_colors.cpp
extern "C" int attack_press_slot(void *jvm, void *minecraft);
extern "C" void note_input_press(int code);
extern "C" void hook_click_mouse(JNIEnv *env, jclass, void *minecraft) {
    if (java_natives_frame_client_shutting_down_natives_check) return;
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    HCtx ctx;
    make_ctx(&ctx, &jvm, minecraft);
    // The player attacked, whatever key or button they bound it to and however fast the press was: arm the
    // require-mouse-pressed gates before anything gets a chance to cancel this click.
    note_input_press(attack_press_slot(&jvm, minecraft));
    tick_pre_click(&ctx, java_natives_frame_module_manager_target_state);
    HEvent e = {};
    e.type = 0xe;
    e.ctx = &ctx;
    e.flag = 1;
    dispatch_event(java_natives_world_module_manager, &e);
    if (e.cancel) return;
    java_call_minecraft_click_mouse(&jvm, minecraft);
    e.type = 0xf;
    dispatch_event(java_natives_world_module_manager, &e);
}
extern "C" void hook_right_click_mouse(JNIEnv *env, jclass, void *minecraft) {
    if (java_natives_frame_client_shutting_down_natives_check) return;
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    HCtx ctx;
    make_ctx(&ctx, &jvm, minecraft);
    HEvent e = {};
    e.type = 0xe;
    e.ctx = &ctx;

    e.cancel = 0;
    dispatch_event(java_natives_world_module_manager, &e);
    tick_click(&ctx);
    if (e.cancel) return;
    java_call_minecraft_right_click_mouse(&jvm, minecraft);
    e.type = 0xf;
    dispatch_event(java_natives_world_module_manager, &e);
    tick_click_done(&ctx);
}

// ---- main per-frame hook: context, GUI frame (events 9/10), module ticks, unload ----
FN(void, hook_game_window, ());
FN(void *, get_timer, (Jvm *jvm, void *minecraft));
FN(void, invoke_callback, (void *x));
FN(void, apply_module_state, (void *mgr, void *mod, int v));
FN(double, time_now, ());
FN(int, display_width, (Jvm *jvm));
FN(int, display_height, (Jvm *jvm));
FN(unsigned char, is_event_handled, (void *mgr, int type));
FN(unsigned char, resize_render_target, (void *gui, int w, int h));
FN(double, gui_get_scale_entry, ());
FN(void, set_render_scale, (void *gui, double t));
FN(void, begin_frame_framebuffer, (void *gui, double t));
FN(void, clear_frame, (void *gui));
FN(void, begin_frame, (void *gui, double t));
FN(void, draw_framebuffer_texture_over_screen, (void *gui));
FN(void, end_frame, (void *gui));
FN(unsigned char, overlay_usable, ());
FN(unsigned char, has_queued_modules, (void *mgr));
FN(void, apply_module_toggles, (HCtx *ctx));
FN(void, tick_modules, (HCtx *ctx, void *timer, double now));
FN(void, tick_netty, (Jvm *jvm));
FN(unsigned char, is_unload_requested_flag, ());
FN(int, save_settings, (void *module_manager, void *profile_list));
FN(void, queue_module_toggle, (void *module_manager, void *module, unsigned char enabled));
struct Pair16 { long long a, b; };
FN(void, unregister_channel_class, (Jvm *jvm, Pair16 *p));
FN(void, tick_post_frame, (HCtx *ctx, void *state));
extern "C" char gui_state_block[];            // GUI state; [0x4cb] = unload requested
FN(void, restore_game_window, ());
FN(void, input_poll_client_state, (Jvm *jvm, void *minecraft));   // platform/linux/input.cpp: the LWJGL/GLFW state poll
FN(void, gui_render_target_destroy, ());
FN(void, java_set_minecraft_in_game_focus, (Jvm *jvm, void *minecraft, int value));
FN(void *, java_get_minecraft_current_screen, (Jvm *jvm, void *minecraft));
FN(void, java_set_minecraft_current_screen, (Jvm *jvm, void *minecraft, void *screen));
FN(void, java_call_static_mouse_consume_delta, (Jvm *jvm));
extern "C" unsigned char gui_owns_cursor;   // the ClickGUI holds Minecraft.inGameHasFocus cleared (logic/visual/gui.cpp)
// The ClickGUI is not one of the game's screens, so the game keeps doing what it does without one: losing the
// window makes it pause itself and put its "back to game" menu up behind ours, and it is still there when the
// window comes back. If the menu holds the cursor and the game shows a screen with the window away, that is the
// pause, and it comes down on the way back - not while still away, where the game would simply put it up again
// every frame. A screen the player opened themselves (chat, a chest, a pause before the menu) stays: it only
// ever appears with the window in front, and then this does nothing.
static void dismiss_foreign_screen(Jvm *jvm, void *minecraft) {
    static unsigned char pause_while_away;   // the game paused itself with the window away, ours to clear
    void *screen = java_get_minecraft_current_screen(jvm, minecraft);
    if (!screen || !gui_owns_cursor) { pause_while_away = 0; return; }
    if (!world_is_loaded(jvm)) { pause_while_away = 1; return; }   // isActive gone: leave it up, no point fighting
    if (pause_while_away) java_set_minecraft_current_screen(jvm, minecraft, 0);
    pause_while_away = 0;
}
// This drain only reaches clients whose deltas do come from LWJGL. Vanilla is the only reader of
// Mouse.getDX()/getDY() and empties them on every frame it holds the cursor, so clearing the focus flag stops
// the reading too and leaves the movement of the last poll standing in LWJGL's hands - and the cursor warp
// that the grab on the way out queues is only measured at the next pump, which vanilla would spend as one
// frame of look. Empty the accumulator after every pump while the menu holds the cursor, and for the frames
// just after it lets go. A MouseHelper that samples a device of its own (RawInput does, on a background
// thread) is beyond anything here, and that path is what the game's own grab resets (logic/visual/gui.cpp).
static unsigned char frame_menu_held_cursor;
static unsigned char frame_delta_drain_left;
// the engine thread (platform/win/entry.cpp) owns the JVMTI environment, so the hook removal and the release of
// the module have to run there; this thread only waits for the result and does what needs its OpenGL context
FN(int, engine_disarm_step, ());
FN(void, engine_leave_and_free, ());
FN(bool, java_call_mouse_helper_grab_cursor, (Jvm *jvm, void *minecraft));
FN(void, java_call_static_mouse_set_grabbed, (Jvm *jvm, bool grab));
typedef void (*GlPushAttrib)(unsigned);
typedef void (*GlPopAttrib)();
extern "C" GlPushAttrib *ptr_to_pGlPushAttrib;
extern "C" GlPopAttrib *ptr_to_pGlPopAttrib;
extern "C" const double threshold_0_00833333;
extern "C" double frame_start_time;
extern "C" unsigned char frame_prev_module_state, frame_input_flag, frame_repass_needed, frame_spinlock, java_natives_frame_client_shutting_down_natives_check;
extern "C" int frame_resize_cooldown, render_target_width, render_target_height;
extern "C" void *java_natives_frame_module_key_binds_key_module, *frame_render_target;
extern "C" Pair16 init_slinkychannelhandler_class_constructor;
struct ModEnt { char padding_00[0x1e8]; void (*on_shutdown)(HEvent *); };
struct Mgr { ModEnt **data; unsigned long long count; };
struct GuiEvent { int type; int padding_04; HCtx *ctx; int flags; float dt; char rest[0x30]; };
struct GuiEvent2 { long long type; HCtx *ctx; void *gui; float dt; char rest[0x2c]; };
static inline __attribute__((always_inline)) void spin_lock() {
    while (__atomic_exchange_n(&frame_spinlock, 1, __ATOMIC_SEQ_CST)) __builtin_ia32_pause();
}

// ---- the unload, over the frames it takes ----
// The click is answered by the frame that heard it. From the frame after that the game gets one hooked class
// back per frame (engine/protected/init.cpp slinky_disarm_step): RetransformClasses takes the whole VM into a
// global safepoint, so all nine in one call is one stop the player watches - measured at ~260 ms of frozen
// window with the menu already gone in front of it. One per frame keeps every stop under one frame and lets the
// game paint its own vanilla frame in between, so the client is out of sight the moment it is clicked off.
// The cursor goes back on the first frame of that pass, before the first stop, not after the last one.
// The shutdown flag is deliberately NOT raised on the click. These natives are the bodies of the game's own
// methods - the class files say so - so an inert hook_update_entities is a world that stops updating, an inert
// hook_update_controller a player that stops moving, an inert hook_send_packet packets that stop leaving. The
// flag can only go up once the classes are back; until then the client keeps working exactly as it did, which
// is why the restore hands the drawing classes back before the logic ones (classfile_hooks.cpp disarm_class) -
// the overlays are gone a couple of frames into the pass even though the natives are still answering calls.
static unsigned char unload_detach_frame;   // 1 = the click was heard, 2 = classes are going back

static int ms_since(double from) { return (int)((time_now() - from) * 1000.0 + 0.5); }

// Everything after the flag is freed by the engine thread, so it has to go up here - but the instance can be
// gone (disconnect or world switch during the restore), and then the pipeline, the module list and the screen
// it would have posted to are the JVM's business, not ours. What is always ours is the save, the window
// subclass, the render target and the word that lets the engine thread free the DLL: an unload that stopped
// here left all of that behind, held the staging folder open, and parked the engine thread waiting for a
// hand-off nobody was going to make - a repeat click then cost the game a thirty-second wait on an answer
// that could never come.
static void unload_teardown(JNIEnv *env) {
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    void *minecraft = get_minecraft(&jvm);
    HCtx ctx;
    __builtin_memset(&ctx, 0, sizeof ctx);
    // unload: save settings, notify and disable every module, tear down
    double phase = time_now();
    int saved = save_settings(java_natives_world_module_manager, init_profile_list);
    slinky_logf("unload: settings saved=%d (%d ms)", saved, ms_since(phase));
    if (minecraft) {
        make_ctx(&ctx, &jvm, minecraft);
        HEvent e3 = {};
        e3.type = 0x1a;
        e3.ctx = &ctx;
        Mgr *mgr = (Mgr *)java_natives_world_module_manager;
        phase = time_now();
        for (unsigned long long i = 0; i < mgr->count; i++)
            if (mgr->data[i]->on_shutdown) mgr->data[i]->on_shutdown(&e3);
        for (unsigned long long i = 0; i < mgr->count; i++) queue_module_toggle(java_natives_world_module_manager, mgr->data[i], 0);
        slinky_logf("unload: shutdown notified and toggles queued (%d ms)", ms_since(phase));
    }
    // The tap is taken out of the pipeline with a Java call, and netty waits for its own event loop to flush
    // pending handler callbacks before a pipeline change from a foreign thread completes. That event loop is
    // inside on_packet, spinning on frame_spinlock, so holding the lock across this call deadlocks both threads
    // and the game stops painting. The shutdown flag below makes on_packet inert first, so no lock is needed.
    // The Java hooks are already out by this point: removal failing means the caller does not get here, and
    // freeing under the game while its patched code still calls in is the use-after-free hs_err pid6708
    // (setRenderY) and pid2260 (serialize_settings) showed.
    phase = time_now();
    spin_lock();
    java_natives_frame_client_shutting_down_natives_check = 1;
    Pair16 p = init_slinkychannelhandler_class_constructor;
    frame_spinlock = 0;
    if (minecraft) {
        unregister_channel_class(&jvm, &p);
        apply_module_toggles(&ctx);
        tick_post_frame(&ctx, java_natives_frame_module_manager_target_state);
    }
    slinky_logf("unload: packet tap removed and modules applied (%d ms)", ms_since(phase));
    phase = time_now();
    restore_game_window();
    gui_render_target_destroy();   // the NanoVG context and its framebuffer belong to this thread's OpenGL context
    slinky_logf("unload: window and render target released (%d ms)", ms_since(phase));
    slinky_logf("unload: handing the client back on the engine thread");
    engine_leave_and_free();
}

extern "C" void hook_update_display(JNIEnv *env) {
    if (java_natives_frame_client_shutting_down_natives_check) return;   // the client is shutting down; this frame must not touch anything the
                               // unload thread is about to free (save_settings of a second pass crashed here)
    hook_game_window();
    if (!java_natives_world_cached_jni_ids) return;   // nothing is hooked up yet, this frame is the game's own
    // The set-up this frame reaches first also gets the OpenGL entry points and the menu running. It goes here
    // rather than only inside the tick hooks because the tick sites are the ones a modded client can rewrite.
    hooks_first_frame_init();
    if (unload_detach_frame) {
        Jvm djvm = {java_natives_world_cached_jni_ids, env};
        void *minecraft = get_minecraft(&djvm);
        // The pass runs to the end whatever the game does underneath it: the classes already handed back are
        // vanilla, the rest still call into this DLL, and only these frames - or the engine thread's watchdog -
        // can hand the remainder back. So a missing instance costs the steps that need one, not the unload.
        // The pump is what the rewritten Display.update call site would have done; without it the window stops
        // swapping, and every frame of this pass has to swap.
        tick_netty(&djvm);
        if (unload_detach_frame == 1) {
            unload_detach_frame = 2;
            // Nothing of ours is drawn from here on, so the cursor goes back to the game now: the helper does
            // the grab and, on top of it, resets its own delta source, and the movement made inside the menu is
            // thrown away in this same frame - it is read at the top of the next one, so the game never sees it.
            if (minecraft && gui_owns_cursor && is_in_game(&djvm, minecraft) &&
                !java_get_minecraft_current_screen(&djvm, minecraft)) {
                java_set_minecraft_in_game_focus(&djvm, minecraft, 1);
                if (!java_call_mouse_helper_grab_cursor(&djvm, minecraft))
                    java_call_static_mouse_set_grabbed(&djvm, 1);
            }
            java_call_static_mouse_consume_delta(&djvm);
        }
        double phase = time_now();
        int step = engine_disarm_step();
        slinky_logf("unload: disarm=%d (%d ms)", step, ms_since(phase));
        if (step == 1) return;   // one class back per frame; the next frame takes the next one
        unload_detach_frame = 0;
        if (step) return;        // the hooks are still in: everything stays allocated, the click can be repeated
        unload_teardown(env);
        return;
    }
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    void *minecraft = get_minecraft(&jvm);
    void *player = get_local_player(&jvm, minecraft);
    void *world = get_world(&jvm, minecraft);
    void *timer = get_timer(&jvm, minecraft);
    if (!minecraft || !player || !world || !timer) {
        // Not fully loaded yet (e.g. the main menu). The game's own Display.update() sites are rewritten into
        // this native, so the pump still has to happen here or the window stops swapping and the game hangs.
        tick_netty(&jvm);
        return;
    }
    HCtx ctx;
    ctx.jvm = &jvm; ctx.module_manager = java_natives_world_module_manager; ctx.profile_list = init_profile_list; ctx.minecraft = minecraft; ctx.world = world; ctx.player = player;
    ctx.key_state_table = java_natives_world_key_state_table_one_byte; ctx.target = java_natives_frame_module_manager_target_state;
    char *cfg = ptr_to_gui_module_descriptor;
    ctx.in_game = cfg[0x14] ? 0 : is_in_game(&jvm, minecraft);
    // While the menu owns the cursor the camera must not follow it: vanilla turns the view from
    // Mouse.getDX()/getDY() in its own loop, gated on Minecraft.inGameHasFocus, and a window focus change
    // puts that flag back on. This frame runs whether or not the game thinks it has focus.
    if (cfg[0x14] && gui_owns_cursor) java_set_minecraft_in_game_focus(&jvm, minecraft, 0);
    dismiss_foreign_screen(&jvm, minecraft);
    *(int *)&ctx.padding_41[0] = 0;
    ctx.padding_44 = 0;
    if (!ctx.in_game && frame_prev_module_state) {
        invoke_callback(java_natives_frame_module_key_binds_key_module);
        if (!world_is_loaded(&jvm)) __builtin_memset(java_natives_world_key_state_table_one_byte, 0, 0x68);
    }
    frame_prev_module_state = ctx.in_game;
    if (*(int *)(cfg + 0x1c) == 0) apply_module_state(java_natives_world_module_manager, ptr_to_gui_module_descriptor, 0x63);
    double now = time_now();
    int W = display_width(&jvm);
    int H = display_height(&jvm);
    if (0.0 > frame_start_time) frame_start_time = now;
    if (frame_render_target && overlay_usable() && !is_event_handled(java_natives_world_module_manager, 0xa)) {
        render_target_width = W;
        render_target_height = H;
        if (resize_render_target(frame_render_target, W, H)) {
            frame_resize_cooldown = 0xa;
        } else if (frame_resize_cooldown > 0) {
            frame_resize_cooldown = frame_resize_cooldown - 1;
        }
        void *gui = frame_render_target;
        set_render_scale(gui, gui_get_scale_entry());
        double dt = now - frame_start_time;
        if (dt > threshold_0_00833333) {
            float fdt = (float)dt;
            GuiEvent e = {};
            e.type = 9;
            e.ctx = &ctx;
            e.dt = fdt;
            dispatch_event(java_natives_world_module_manager, (HEvent *)&e);
            int flags = e.flags;
            frame_input_flag = flags & 1;
            if (flags & 1) {
                if ((flags & 2) || frame_repass_needed || frame_resize_cooldown > 0) {
                    unsigned char again = (unsigned)(flags & 2) >> 1;
                    (*ptr_to_pGlPushAttrib)(0xffffffff);
                    begin_frame_framebuffer(frame_render_target, now);
                    GuiEvent2 e2 = {0xa, &ctx, frame_render_target, fdt, {}};
                    dispatch_event(java_natives_world_module_manager, (HEvent *)&e2);
                    clear_frame(frame_render_target);
                    (*ptr_to_pGlPopAttrib)();
                    frame_repass_needed = again;
                }
            }
            frame_start_time = now;
        }
        if (frame_input_flag) {
            (*ptr_to_pGlPushAttrib)(0xffffffff);
            begin_frame(frame_render_target, now);
            draw_framebuffer_texture_over_screen(frame_render_target);
            end_frame(frame_render_target);
            (*ptr_to_pGlPopAttrib)();
        } else {
            frame_repass_needed = 1;
        }
    }
    if (has_queued_modules(java_natives_world_module_manager)) {
        spin_lock();
        apply_module_toggles(&ctx);
        frame_spinlock = 0;
    }
    tick_modules(&ctx, timer, now);
    tick_netty(&jvm);
#ifdef __linux__
    // LWJGL 2 measures the frame's input inside the pump above and X11 hands no window procedure to read it from,
    // so the state poll that the Win32 build does through the subclassed window - and the button overlay the auto
    // clicker presses through - has to run here, after the poll and before the game reads the state next tick.
    input_poll_client_state(&jvm, minecraft);
#endif
    // last point in the frame that still precedes the next one's camera read: the pump above is where
    // LWJGL measures this frame's cursor movement, so emptying the delta here leaves vanilla at most the
    // movement of a single frame to spend when it takes the mouse back
    if (gui_owns_cursor || frame_menu_held_cursor)
        frame_delta_drain_left = 3;
    else if (frame_delta_drain_left)
        frame_delta_drain_left = frame_delta_drain_left - 1;
    frame_menu_held_cursor = gui_owns_cursor;
    if (frame_delta_drain_left) java_call_static_mouse_consume_delta(&jvm);
    if (!is_unload_requested_flag()) return;
    gui_state_block[0x4cb] = 0;   // consume the click immediately: one sequence per click, no repass if
                              // the user clicks again while this one is still running
    // The frame that follows paints the game with nothing of ours on it and only then starts the teardown, so
    // the stall the hook removal causes is never shown with the menu frozen in front of it.
    unload_detach_frame = 1;
}

// ---- set the key bind of a module: re-register it in the bind map, then tell the modules (event 0x18) ----
FN(void, key_map_remove, (void *map, int key, void *mod));   // unregister
FN(void, key_map_add, (void *map, int key, void *mod));   // register
struct BindEvent { int type; int padding_04; void *mgr; void *mod; char rest[0x30]; int tail; };
extern "C" void set_module_bind(void *mod, int key) {
    int *slot = (int *)((char *)mod + 0x1c);
    key_map_remove(java_natives_frame_module_key_binds_key_module, *slot, mod);
    *slot = key;
    key_map_add(java_natives_frame_module_key_binds_key_module, key, mod);
    BindEvent ev = {};
    ev.type = 0x18;
    ev.mgr = java_natives_world_module_manager;
    ev.mod = mod;
    dispatch_event(java_natives_world_module_manager, (HEvent *)&ev);
}

// ---- key handlers of the two key maps (java_natives_frame_profile_key_binds_key_profile: key -> profile name, java_natives_frame_module_key_binds_key_module: key -> module) ----
// Both are called from the input handler on_input_event through key_map_dispatch as handler(item, key, down).
FN(int, load_profile_name, (const char *name, void *mgr));              // load profile `name`; 0 = ok
FN(void *, find_profile, (void *profiles, const char *name));       // profile entry by name (+0x38: key bind)
FN(void *, key_map_get, (void *map, int key, void *val));          // item registered under `key`
FN(void *, libc_calloc, (unsigned long long n, unsigned long long size));
FN(unsigned long long, libc_strlen, (const char *str));
FN(void *, libc_memcpy, (void *dest, const void *src, unsigned long long n));
extern "C" void *java_natives_frame_profile_key_binds_key_profile;
extern "C" unsigned char frame_spinlock, java_natives_frame_client_shutting_down_natives_check;
extern "C" const char str_failed_to_load_profile[];   // "Failed to load profile"
struct Ev48 { int type; int padding_04; union { const char *name; struct { int err; int pad2; const char *msg; }; }; char rest[0x38]; };
extern "C" const char ptr_to_str_failed_to_set_bind[];   // prepared "Failed to set bind" event (0x48 bytes)
// profile bind pressed: load the profile; a failure unbinds it and is reported (event 0xc), success is event 0xb
extern "C" unsigned char on_profile_bind(const char *name, int key, bool down) {
    if (!down) return 0;
    int err = load_profile_name(name, java_natives_world_module_manager);
    if (!err) {
        Ev48 e = {};
        e.type = 0xb;
        e.name = name;
        dispatch_event(java_natives_world_module_manager, (HEvent *)&e);
        return 1;
    }
    char *prof = (char *)find_profile(init_profile_list, name);
    if (prof) *(int *)(prof + 0x38) = 0;
    Ev48 e = {};
    e.type = 0xc;
    e.err = err;
    e.msg = str_failed_to_load_profile;
    dispatch_event(java_natives_world_module_manager, (HEvent *)&e);
    key_map_remove(java_natives_frame_profile_key_binds_key_profile, key, 0);
    return 0;
}

// module bind pressed (hold mode: enabled while the key is down; otherwise toggles on press)
extern "C" unsigned char on_module_bind(char *mod, int key, bool down) {
    if (mod[0x20] == 1) {
        queue_module_toggle(java_natives_world_module_manager, mod, down);
        mod[0x21] = down;
    } else if (down) {
        queue_module_toggle(java_natives_world_module_manager, mod, mod[0x14] ^ 1);
    }
    return 1;
}

// bind profile `p` to `key` (0 = unbind): a key serves one profile, the previous owner loses its bind
extern "C" void bind_profile_to_key(char *p, int key) {
    void *map = java_natives_frame_profile_key_binds_key_profile;

    key_map_remove(map, *(int *)(p + 0x38), 0);
    void *other = key_map_get(java_natives_frame_profile_key_binds_key_profile, key, 0);
    if (other) {
        char *o = (char *)find_profile(init_profile_list, (const char *)other);
        if (o) {
            *(int *)(o + 0x38) = 0;
            key_map_remove(java_natives_frame_profile_key_binds_key_profile, key, 0);
        }
    }
    if (key) {
        const char *name = *(const char **)p;
        unsigned long long n = libc_strlen(name);
        char *copy = (char *)libc_calloc(n + 1, 1);
        if (!copy) {
            Ev48 e;
            __builtin_memcpy(&e, ptr_to_str_failed_to_set_bind, 0x48);
            dispatch_event(java_natives_world_module_manager, (HEvent *)&e);
            return;
        }
        libc_memcpy(copy, name, n);
        key_map_add(java_natives_frame_profile_key_binds_key_profile, key, copy);
    }
    *(int *)(p + 0x38) = key;
}

// ---- SlinkyChannelHandler.onPacket(Object packet, boolean outbound, long time, int id): int ----
// Netty thread -> client: serialised with the frame code through the lock at frame_spinlock, ignored once the
// client is shutting down (java_natives_frame_client_shutting_down_natives_check). The modules see it as event 2 (inbound) / 3 (outbound) and may change
// `result` (checked at +0x24 by the handler).
struct PktEv { int type; int padding_04; Jvm *jvm; void *pkt; long long time; int id; int result; char rest[0x20]; int tail; };
extern "C" int on_packet(JNIEnv *env, jobject, void *pkt, unsigned char outbound, long long time, int id) {
    bool busy;
    for (;;) {
        busy = __atomic_exchange_n(&frame_spinlock, 1, __ATOMIC_SEQ_CST) != 0;

        if (!busy) break;
        __builtin_ia32_pause();
    }
    int result = 0;
    if (!java_natives_frame_client_shutting_down_natives_check) {
        Jvm jvm = {java_natives_world_cached_jni_ids, env};
        PktEv e = {};
        e.type = outbound ? 3 : 2;
        e.jvm = &jvm;
        e.pkt = pkt;
        e.time = time;
        e.id = id;
        dispatch_event(java_natives_world_module_manager, (HEvent *)&e);
        result = e.result;
    }
    frame_spinlock = 0;
    return result;
}
