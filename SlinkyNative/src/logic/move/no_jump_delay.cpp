// No Jump Delay - descriptor +0x158: clear the jump cooldown while the flag is set.
#include "slinky/engine.h"

FN(void, java_set_entity_living_base_jump_ticks, (void *jvm, void *entity_living_base, int value));

extern "C" void no_jump_delay_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    // the world-update dispatch site leaves ctx->player NULL between logout and the next join,
    // and a JNI SetIntField on a NULL object faults
    if (ctx->in_game == 1 && ctx->player) {
        java_set_entity_living_base_jump_ticks(ctx->jvm, ctx->player, 0);
        return;
    }

}
