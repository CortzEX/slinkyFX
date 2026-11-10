// Debug - chat/log diagnostics: hurt time, item-use state transitions, click counting.
#include "slinky/engine.h"

S(str_sect_7_03d_debug_hurt_time_sec);   // hurt-time log format
S(str_sect_7_03d_debug_item_use_sect);   // item-use log format
S(str_astart);   // "Start" label
S(str_cstop);   // "Stop" label
S(str_sect_7_03d_debug_click_sect_cl);   // click log format
extern "C" const int debug_hurt_time_code_table[];   // hurt-time code table

extern "C" unsigned char debug_hurt_time_value;   // "hurt_time"
extern "C" unsigned char debug_item_use_state_value;   // "item_use_state"
extern "C" unsigned char debug_clicks_value;   // "clicks"
extern "C" unsigned char debug_last_item_use_state;   // last item-use state
extern "C" unsigned char debug_current_item_use_state;   // current item-use state
extern "C" int debug_left_clicks_this_window;             // left clicks this window
extern "C" int debug_right_clicks_this_window;             // right clicks this window
extern "C" int debug_ticks_window;             // ticks in window

struct EvHook { char padding_00[8]; void *ctx; void *ev; char padding_18[12]; int type; };   // +0x24

FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void, jni_throw_format, (void *jvm, const char *fmt, ...));
FN(void *, java_jni_is_instance_of_c08_packet_player_block_placement, (void *ctx, void *event));
FN(void *, java_jni_is_instance_of_c07_packet_player_digging, (void *ctx, void *event));
FN(int, java_c07_packet_player_digging_status, (void *ctx, void *object));

extern "C" void debug_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game == 1 && ctx->player) {
        if (debug_hurt_time_value == 1) {
            int entity_living_base_hurt_time = java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player);
            if (entity_living_base_hurt_time >= 7) {
                void *jvm = ctx->jvm;
                unsigned key = java_get_entity_ticks_existed(jvm, ctx->player);
                unsigned ms = key - key / 1000 * 1000;
                int code = 0x66;
                unsigned idx = entity_living_base_hurt_time - 7;

                if (idx <= 3)
                    code = debug_hurt_time_code_table[idx];
                int sprint = 0x63;
                if (java_call_entity_is_sprinting(ctx->jvm, ctx->player)) sprint = 0x38;
                int item = 0x38;
                if (java_call_entity_player_is_using_item(ctx->jvm, ctx->player)) item = 0x63;
                jni_throw_format(jvm, str_sect_7_03d_debug_hurt_time_sec, ms, code, entity_living_base_hurt_time, sprint, item);
            }
        }
        if (debug_item_use_state_value == 1) {
            unsigned cur = debug_current_item_use_state;
            unsigned char st = cur & 1;
            if (st != debug_last_item_use_state) {
                debug_last_item_use_state = st;
                void *a = ctx->jvm;
                unsigned key = java_get_entity_ticks_existed(a, ctx->player);
                unsigned ms = key - key / 1000 * 1000;
                jni_throw_format(a, str_sect_7_03d_debug_item_use_sect, ms, (cur & 1) ? str_astart : str_cstop);
            }
        }
    }

}

extern "C" void debug_on_packet_sent(EvHook *hook) {
    int t = hook->type;
    unsigned char v;
    if (t == 3 || t == 0) {
        v = 1;
        if (!java_jni_is_instance_of_c08_packet_player_block_placement(hook->ctx, hook->ev)) {
            void *c07_packet_player_digging = java_jni_is_instance_of_c07_packet_player_digging(hook->ctx, hook->ev);
            if (!c07_packet_player_digging)
                return;
            if (java_c07_packet_player_digging_status(hook->ctx, c07_packet_player_digging) != 5)
                return;
            v = 0;
        }
        __atomic_exchange_n(&debug_current_item_use_state, v, __ATOMIC_SEQ_CST);
    }
}

extern "C" void debug_on_attack_post(unsigned char *h) {
    if (debug_clicks_value == 1) {
        int *p = &debug_left_clicks_this_window;
        int *q = &debug_right_clicks_this_window;
        if (!h[0x10])
            p = q;
        *p += 1;
        debug_ticks_window = 0;
    }
}

extern "C" void debug_on_late_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game == 1 && ctx->player) {
        unsigned n = debug_ticks_window;
        if (debug_clicks_value == 1) {
            if (n <= 9) {
                int l = debug_left_clicks_this_window;
                int s = 0x32;
                if (l == 1) s = 0x61;
                if (l == 0) s = 0x38;
                int result = debug_right_clicks_this_window;
                int t = 0x33;
                if (result == 1) t = 0x62;
                if (result == 0) t = 0x38;
                void *jvm = ctx->jvm;
                unsigned key = java_get_entity_ticks_existed(jvm, ctx->player);
                unsigned ms = key - key / 1000 * 1000;
                int lc = debug_left_clicks_this_window, rc = debug_right_clicks_this_window;
                jni_throw_format(jvm, str_sect_7_03d_debug_click_sect_cl, ms, s, lc, t, rc);
                n = debug_ticks_window;
            }
            debug_left_clicks_this_window = 0;
            debug_right_clicks_this_window = 0;
        }
        debug_ticks_window = n + 1;
    }
}
