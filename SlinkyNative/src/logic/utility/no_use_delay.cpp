// No Use Delay - descriptor +0x98 (toggle) / +0x168 (late tick): shorten the item-use delay.
#include "slinky/engine.h"

extern "C" char no_use_delay_module_descriptor[];           // No Use Delay module descriptor
extern "C" unsigned char no_use_delay_allow_desync_value;  // "allow_desync"
extern "C" int no_use_delay_remaining_ticks;            // remaining use-delay ticks
extern "C" int no_use_delay_last_seen_slot_state;            // last seen slot/state

struct DelayState { char padding_00[8]; Ctx *ctx; };
struct ToggleHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(unsigned char, is_key_down, (void *jvm, void *object));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(int, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, int value));
FN(int, java_get_entity_player_item_in_use_count, (void *jvm, void *entity_player));

extern "C" void no_use_delay_on_late_tick(DelayState *s) {
    Ctx *ctx = s->ctx;
    if (ctx->in_game == 0 || !ctx->player) {
        no_use_delay_remaining_ticks = 0;
        return;
    }
    void *game_settings_key_bindings = java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft));
    unsigned char busy = is_key_down(ctx->jvm, game_settings_key_bindings);
    int slot = java_get_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player));
    if (slot != no_use_delay_last_seen_slot_state) {
        no_use_delay_remaining_ticks = 0;
        no_use_delay_last_seen_slot_state = slot;
        void *jvm = ctx->jvm;
        java_set_key_binding_pressed(jvm, game_settings_key_bindings, java_get_key_binding_key_code(jvm, game_settings_key_bindings));
    }
    if (!busy)
        return;
    int w = java_get_entity_player_item_in_use_count(ctx->jvm, ctx->player);
    int cnt = no_use_delay_remaining_ticks;
    if (__builtin_expect(cnt <= 0, 0)) {
        no_use_delay_remaining_ticks = w;
        return;
    }
    cnt--;
    no_use_delay_remaining_ticks = cnt;
    if (cnt != 0) {
        unsigned char ok = (w == 0) & no_use_delay_allow_desync_value;

        if (ok != 1)
            return;
    }
    java_set_key_binding_pressed(ctx->jvm, game_settings_key_bindings, 0);
}

// the module forces the use-item binding down; turned off mid-use nothing else releases it,
// so the client would keep "holding" right click after the module is gone
extern "C" void no_use_delay_on_toggle(ToggleHook *hook) {
    if (hook->ev != no_use_delay_module_descriptor || hook->on != 0)
        return;
    no_use_delay_remaining_ticks = 0;
    no_use_delay_last_seen_slot_state = -1;
    Ctx *ctx = hook->ctx;
    if (!ctx->player)
        return;
    void *jvm = ctx->jvm;
    void *use_key = java_get_game_settings_use_item_key(jvm, get_game_settings(jvm, ctx->minecraft));
    if (use_key)
        java_set_key_binding_pressed(jvm, use_key, 0);
}
