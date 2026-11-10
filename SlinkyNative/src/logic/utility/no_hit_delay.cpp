// No Hit Delay - descriptor +0x128.
#include "slinky/engine.h"

extern "C" unsigned no_hit_delay_mode;  // "mode" current value

struct World { int *state; };
struct HitState { char padding_00[8]; Ctx *ctx; unsigned char on; };
FN(int, java_get_minecraft_left_click_counter, (void *jvm, void *minecraft));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(void, java_set_minecraft_left_click_counter, (void *jvm, void *minecraft, int value));
FN(bool, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void, java_jni_call_nonvirtual_void_method_entity_living_base_swing_item, (void *jvm, void *object));

extern "C" void no_hit_delay_on_attack(HitState *s) {
    if (s->on != 1)
        return;
    Ctx *ctx = s->ctx;
    if (*((World *)ctx->jvm)->state == 0)
        return;
    if (!ctx->player)
        return;
    if (java_get_minecraft_left_click_counter(ctx->jvm, ctx->minecraft) <= 0)
        return;
    if (no_hit_delay_mode == 1) {
        void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (!minecraft || java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) == 0)
            goto next;
    }
    java_set_minecraft_left_click_counter(ctx->jvm, ctx->minecraft, 0);
    return;
next:
    if (java_call_entity_player_is_using_item(ctx->jvm, ctx->player))
        return;
    java_jni_call_nonvirtual_void_method_entity_living_base_swing_item(ctx->jvm, ctx->player);
}
