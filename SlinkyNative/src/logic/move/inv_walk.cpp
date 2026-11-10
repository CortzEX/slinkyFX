// Inv Walk - keep moving while an inventory / container screen is open.
// Only the small hooks are recoded here; the big tick handler (inv_walk_tick_keep_movement)
//   +0x98: on disable restore the movement keys (0x39ff0)
//   0x39ce0: GUI click hook (mode "silent": queue clicks while the screen is closed)
//   0x39e30: tick: release queued input while a screen is open
#include "slinky/engine.h"
#include "slinky/jids.h"

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct GuiHook { char padding_00[8]; void *a; void *screen_obj; void *screen; unsigned char handled; };
extern "C" char inv_walk_module_descriptor[];           // Inv Walk module descriptor
extern "C" unsigned inv_walk_mode;         // "mode"
extern "C" void *inv_walk_queued_clicks[0x20];      // queued clicks
extern "C" unsigned long long inv_walk_queued_count;   // queued count
extern "C" int inv_walk_tick_counter;              // tick counter
extern "C" int inv_walk_phase;              // state
extern "C" int inv_walk_tick_last_screen_activity;              // tick of last screen activity
extern "C" unsigned char inv_walk_replaying_queued_clicks;    // replaying queued clicks


extern "C" unsigned inv_walk_stop_on_click;         // "stop_on_click"
extern "C" float inv_walk_stop_duration_value;            // "stop_duration"
extern "C" unsigned inv_walk_stop_non_inv_value;         // "stop_non_inv"
extern "C" unsigned char inv_walk_keys_released_last_tick;    // keys released last tick
FN(void *, get_current_screen, (void *jvm, void *object));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_back_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_left_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_right_key, (void *jvm, void *game_settings));
FN(void *, get_held_item, (void *jvm, void *item));
FN(void *, java_get_game_settings_key_17_or_8, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(unsigned char, java_jni_is_instance_of_gui_inventory, (void *jvm, void *screen));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(void *, java_call_minecraft_get_net_handler, (void *jvm, void *minecraft));
FN(void *, java_get_net_handler_play_client_net_manager, (void *jvm, void *net_handler_play_client));
FN(void *, java_get_inventory_player_item_stack, (void *jvm, void *inv));
FN(void *, java_get_entity_player_inventory_container, (void *jvm, void *entity_player));
FN(void *, java_get_container_player_craft_matrix, (void *jvm, void *container_player));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *inv, int slot));
FN(void *, java_new_c_init, (void *jvm, int x));
FN(void, java_call_entity_set_sprinting, (void *jvm, void *entity, bool on));
FN(unsigned char, release_global_refs, (void *jvm, void *minecraft));
FN(void *, java_jni_is_instance_of_c16_packet_client_status, (void *jvm, void *screen));
FN(int, java_get_c16_packet_client_status_status, (void *jvm, void *c16_packet_client_status));
FN(void *, java_jni_is_instance_of_c0_epacket_click_window, (void *jvm, void *screen));
FN(void *, java_get_c16_enum_state_open_inventory, (void *jvm));
FN(void *, java_new_c16_packet_client_status, (void *jvm, void *object));
FN(void, java_call_generic_future_listener, (void *jvm, void *generic_future_listener, void *other_object));
FN(void *, java_jni_is_instance_of_c, (void *jvm, void *screen));
FN(unsigned char, keyboard_has_events, (void *jvm));
FN(unsigned char, keyboard_has_next, (void *jvm));

extern "C" void inv_walk_on_toggle(EvHook *hook) {
    if (hook->on || hook->ev != inv_walk_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    // The tick handler holds these keys down (or forces them up) synthetically and unregisters on
    // disable, so without this a key the module was still "pressing" stays that way until the user
    // taps it for real, and phase/keys_released would make the next enable skip its own resync.
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *keys[6] = { java_get_game_settings_back_key(ctx->jvm, obj),
                      java_get_game_settings_left_key(ctx->jvm, obj),
                      java_get_game_settings_right_key(ctx->jvm, obj),
                      java_get_game_settings_forward_key(ctx->jvm, obj),
                      get_held_item(ctx->jvm, obj),
                      java_get_game_settings_key_17_or_8(ctx->jvm, obj) };
    for (int i = 0; i < 6; i++)
        java_set_key_binding_pressed(ctx->jvm, keys[i], java_get_key_binding_key_code(ctx->jvm, keys[i]));
    inv_walk_keys_released_last_tick = 0;
    inv_walk_phase = 0;
    inv_walk_replaying_queued_clicks = 0;
    release_global_refs(ctx->jvm, ctx->minecraft);
}

extern "C" void inv_walk_on_gui_screen(GuiHook *hook) {
    // Declared before the first goto: MSVC lets a jump step over an initialisation, the SysV C++ front end
    // makes it an error, and the same file has to build for both.
    unsigned long long n;
    void *c16_packet_client_status, *jvm;
    if (__builtin_expect(inv_walk_mode != 1, 0))
        goto end;
    c16_packet_client_status = java_jni_is_instance_of_c16_packet_client_status(hook->a, hook->screen);
    jvm = hook->a;
    if (c16_packet_client_status) {
        if (java_get_c16_packet_client_status_status(jvm, c16_packet_client_status) == 2) {
            hook->handled = 1;
            inv_walk_phase = 2;
        }
        goto end;
    }
    if (java_jni_is_instance_of_c0_epacket_click_window(jvm, hook->screen)) {
        if (inv_walk_phase == 2) {
            java_call_generic_future_listener(hook->a, hook->screen_obj, java_new_c16_packet_client_status(hook->a, java_get_c16_enum_state_open_inventory(hook->a)));
            inv_walk_phase = 3;
            inv_walk_replaying_queued_clicks = 1;
            n = inv_walk_queued_count;
            goto queue;
        }
        goto check;
    }
    if (!java_jni_is_instance_of_c(hook->a, hook->screen))
        goto end;
    if (inv_walk_phase == 2)
        goto mark;
    n = inv_walk_queued_count;
    if (n)
        goto queue;
    goto end;
check:
    n = inv_walk_queued_count;
    if (!(((n != 0) | inv_walk_replaying_queued_clicks) & 1)) {
        inv_walk_tick_last_screen_activity = inv_walk_tick_counter;
        goto end;
    }
queue:
    if (n != 0x20) {
        JNIEnv *obj = ((Jvm *)hook->a)->env;
        void *r = (*obj)->NewGlobalRef(obj, hook->screen);
        if (r) {
            unsigned long long k = inv_walk_queued_count;
            inv_walk_queued_clicks[k] = r;
            inv_walk_queued_count = k + 1;
        }
    }
mark:
    hook->handled = 1;
end:

}

extern "C" void inv_walk_on_inventory_tick(Hook *hook) {
    // Declared before the first goto: MSVC lets a jump step over an initialisation, the SysV C++ front end
    // makes it an error, and the same file has to build for both.
    Ctx *ctx = hook->ctx;
    if (__builtin_expect(inv_walk_replaying_queued_clicks, 0))
        goto end;
    if (!release_global_refs(ctx->jvm, ctx->minecraft))
        goto end;
    inv_walk_tick_last_screen_activity = inv_walk_tick_counter;
    if (ctx->in_game != 1)
        goto end;
    while (keyboard_has_events(ctx->jvm))
        ;
    while (keyboard_has_next(ctx->jvm))
        ;
end:

}

// tick: keep the movement keys pressed while a screen is open ("mode" 0 =
// vanilla screens only, 1 = silent / any screen), stopping on clicks
extern "C" void inv_walk_tick_keep_movement(Hook *hook) {
    Ctx *ctx = hook->ctx;
    void *scr = get_current_screen(ctx->jvm, ctx->minecraft);
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *back = java_get_game_settings_back_key(ctx->jvm, obj);
    void *left = java_get_game_settings_left_key(ctx->jvm, obj);
    void *right = java_get_game_settings_right_key(ctx->jvm, obj);
    void *fwd = java_get_game_settings_forward_key(ctx->jvm, obj);
    void *jump = get_held_item(ctx->jvm, obj);
    unsigned char key_binding_key_code = java_get_key_binding_key_code(ctx->jvm, back);
    unsigned char kl = java_get_key_binding_key_code(ctx->jvm, left);
    unsigned char kr = java_get_key_binding_key_code(ctx->jvm, right);
    unsigned char kf = java_get_key_binding_key_code(ctx->jvm, fwd);
    unsigned char kj = java_get_key_binding_key_code(ctx->jvm, jump);
    unsigned soc = inv_walk_stop_on_click;
    unsigned char queued = (inv_walk_mode == 1 && soc != 2) ? inv_walk_queued_count != 0 : 0;
    unsigned char release = (soc == 0) & queued;
    unsigned char act;
    inv_walk_tick_counter += 1;
    if (!scr) {
        if (inv_walk_phase != 0) {
            java_set_key_binding_pressed(ctx->jvm, back, key_binding_key_code);
            java_set_key_binding_pressed(ctx->jvm, left, kl);
            java_set_key_binding_pressed(ctx->jvm, right, kr);
            java_set_key_binding_pressed(ctx->jvm, fwd, kf);
            java_set_key_binding_pressed(ctx->jvm, jump, kj);
        }
        inv_walk_phase = 0;
        if (!ctx->in_game)
            return;
        act = 0;
        goto apply;
    }
    {
        unsigned char pressed = key_binding_key_code | kl | kr | kf | kj;
        unsigned char busy;
        if (!java_jni_is_instance_of_gui_inventory(ctx->jvm, scr)) {
            inv_walk_phase = 1;
            if (inv_walk_mode == 1) {
                queued |= pressed ^ 1;
                unsigned char recent = 0;
                if (soc != 2)
                    recent = (unsigned)(inv_walk_tick_counter - inv_walk_tick_last_screen_activity) < (unsigned)(long long)inv_walk_stop_duration_value;
                unsigned char stop = queued | recent;
                unsigned char other = (inv_walk_stop_non_inv_value != 2) | stop;
                queued = other;
                if (((soc == 0) & stop) || ((inv_walk_stop_non_inv_value == 0) & other))
                    goto release_all;
            } else if (release) {
                goto release_all;
            }
            goto physical;
        }
        if (inv_walk_mode != 1) {
            inv_walk_phase = 3;
            act = 1;
            busy = 0;
            goto recompute;
        }
        {
            // the world-update dispatch site does not check the player, and reading the inventory off
            // a NULL jobject faults; with no player there is nothing to inspect, so only queued
            // clicks can hold the keys
            void *inv = ctx->player ? java_get_entity_player_inventory(ctx->jvm, ctx->player) : 0;
            void *cont = java_get_net_handler_play_client_net_manager(ctx->jvm, java_call_minecraft_get_net_handler(ctx->jvm, ctx->minecraft));
            busy = 1;
            if (!inv || !java_get_inventory_player_item_stack(ctx->jvm, inv)) {
                if (inv_walk_phase == 1 || !ctx->player) {
                    busy = queued;
                } else {
                    void *grid = java_get_container_player_craft_matrix(ctx->jvm, java_get_entity_player_inventory_container(ctx->jvm, ctx->player));
                    if (!java_call_iinventory_get_stack_in_slot(ctx->jvm, grid, 0) && !java_call_iinventory_get_stack_in_slot(ctx->jvm, grid, 1) &&
                        !java_call_iinventory_get_stack_in_slot(ctx->jvm, grid, 2) && !java_call_iinventory_get_stack_in_slot(ctx->jvm, grid, 3))
                        busy = queued;
                }
            }
            pressed &= busy ^ 1;
            int st = inv_walk_phase;
            if (st == 2 && !pressed) {
                void *jvm = java_new_c16_packet_client_status(ctx->jvm, java_get_c16_enum_state_open_inventory(ctx->jvm));
                java_call_generic_future_listener(ctx->jvm, cont, jvm);
                inv_walk_phase = st = 3;
            } else if (pressed & (inv_walk_queued_count == 0) & (st == 3)) {
                if (soc == 2 || !((unsigned)(inv_walk_tick_counter - inv_walk_tick_last_screen_activity) < (unsigned)(long long)inv_walk_stop_duration_value)) {
                    void *x = java_new_c_init(ctx->jvm, 0);
                    java_call_generic_future_listener(ctx->jvm, cont, x);
                    inv_walk_phase = st = 2;
                }
            }
            busy |= st == 3;
            act = st != 0;
        }
recompute:
        queued |= (soc != 2) & busy;
        release = (soc == 0) & queued;
    }
apply:
    if (!((act | inv_walk_keys_released_last_tick | release) & 1))
        goto done;
    if (!release)
        goto physical;
release_all:
    java_set_key_binding_pressed(ctx->jvm, back, 0);
    java_set_key_binding_pressed(ctx->jvm, left, 0);
    java_set_key_binding_pressed(ctx->jvm, right, 0);
    java_set_key_binding_pressed(ctx->jvm, fwd, 0);
    java_set_key_binding_pressed(ctx->jvm, jump, 0);
    release = 1;
    goto done;
physical:
    java_set_key_binding_pressed(ctx->jvm, back, key_binding_key_code);
    java_set_key_binding_pressed(ctx->jvm, left, kl);
    java_set_key_binding_pressed(ctx->jvm, right, kr);
    java_set_key_binding_pressed(ctx->jvm, fwd, kf);
    java_set_key_binding_pressed(ctx->jvm, jump, kj);
    release = 0;
done:
    if (queued) {
        if (ctx->player)
            java_call_entity_set_sprinting(ctx->jvm, ctx->player, false);
        void *a = ctx->jvm;
        java_set_key_binding_pressed(a, java_get_game_settings_key_17_or_8(a, obj), 0);
    }
    inv_walk_keys_released_last_tick = release;
    inv_walk_replaying_queued_clicks = 0;
}
