// No Slow - descriptor +0x198: rescale the movement slow-down while using an item.
#include "slinky/engine.h"

extern "C" float no_slow_mult_bow_value;  // "mult_bow"
extern "C" float no_slow_mult_sword_value;  // "mult_sword"
extern "C" float no_slow_mult_consumables_value;  // "mult_consumables"

struct SlowState { char padding_00[8]; Ctx *ctx; float move[2]; };  // +0x10: forward/strafe

FN(bool, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));

extern "C" void no_slow_on_movement_input(SlowState *s) {
    Ctx *ctx = s->ctx;
    if (!java_call_entity_player_is_using_item(ctx->jvm, ctx->player))
        return;
    void *item = item_held_hotbar_slot(ctx->jvm, ctx->player);
    if (!item)
        return;
    // item id - 261 (bow); swords are ids 267, 268, 272, 276, 283
    unsigned k = java_call_static_item_get_id_from_item(ctx->jvm, item) - 261;
    const float *mult;
    if (__builtin_expect(k > 22, 0))
        mult = &no_slow_mult_consumables_value;
    else if (__builtin_expect((0x4088c0u >> k) & 1, 1))
        mult = &no_slow_mult_sword_value;
    else if (__builtin_expect(k != 0, 0))
        mult = &no_slow_mult_consumables_value;
    else
        mult = &no_slow_mult_bow_value;
    double m = *mult / 0.2;
    s->move[0] = (float)(s->move[0] * m);
    s->move[1] = (float)(s->move[1] * m);
}
