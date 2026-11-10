// Auto Weapon - descriptor +0x1d8: switch to the best weapon slot against the
// current target once "activation_time" ticks have passed.
#include "slinky/engine.h"
#define UNLIKELY(c) __builtin_expect(!!(c), 0)

extern "C" float auto_weapon_activation_time_value;            // "activation_time"
extern "C" unsigned char auto_weapon_require_mouse_pressed_value;    // "require_mouse_pressed"
extern "C" int auto_weapon_ticks_since_target_acquired;              // ticks since target acquired
extern "C" char auto_weapon_module_descriptor[];           // switch cooldown timer

struct WState { char padding_00[8]; Ctx *ctx; };
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(void *, java_get_moving_object_position_entity_hit, (void *jvm, void *moving_object_position));
FN(void *, java_jni_is_instance_of_entity_player, (void *jvm, void *candidate));
FN(unsigned char, entity_flags_has, (void *jvm, void *entity, int mode));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(int, weapons_find_first_weapon_slot_entry, (Ctx *ctx, void *player));
FN(unsigned char, claim_release_hotbar_module, (void *timer));
FN(void, java_set_inventory_player_current_item, (void *jvm, void *player, int slot));
FN(void, release_owner, (void *timer));

extern "C" void auto_weapon_on_inventory_tick(WState *s) {
    Ctx *ctx = s->ctx;
    void *moving_object_position, *y, *inventory_player;
    if (UNLIKELY(ctx->in_game != 1 || !ctx->player)) goto reset;
    if (UNLIKELY(java_call_entity_player_is_using_item(ctx->jvm, ctx->player))) goto reset;
    if (UNLIKELY(auto_weapon_require_mouse_pressed_value == 1 && !vertex_pointer_current(ctx))) goto reset;
    moving_object_position = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
    if (UNLIKELY(!moving_object_position)) goto reset;
    if (UNLIKELY(java_get_moving_object_position_type_ordinal(ctx->jvm, moving_object_position) != 2)) goto reset;
    y = java_jni_is_instance_of_entity_player(ctx->jvm, java_get_moving_object_position_entity_hit(ctx->jvm, moving_object_position));
    if (UNLIKELY(!y)) goto reset;
    if (UNLIKELY(entity_flags_has(ctx->jvm, y, 7))) goto reset;
    if (is_holding_accepted_weapon(ctx->jvm, ctx->player)) goto reset;
    {
        inventory_player = java_get_entity_player_inventory(ctx->jvm, ctx->player);
        int cur = java_get_inventory_player_current_item(ctx->jvm, inventory_player);
        int best = weapons_find_first_weapon_slot_entry(ctx, inventory_player);
        if (best == -1 || cur == best) goto reset;
        int cnt = auto_weapon_ticks_since_target_acquired;
        if (auto_weapon_activation_time_value > (float)cnt) {
            auto_weapon_ticks_since_target_acquired = cnt + 1;
            return;
        }
        if (!claim_release_hotbar_module(auto_weapon_module_descriptor)) return;
        java_set_inventory_player_current_item(ctx->jvm, inventory_player, best);
        release_owner(auto_weapon_module_descriptor);
        return;
    }
reset:
    auto_weapon_ticks_since_target_acquired = 0;
}
