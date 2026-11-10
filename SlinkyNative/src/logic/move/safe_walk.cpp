// Safe Walk - descriptor +0x48: scale horizontal motion while sneaking at an edge.
#include "slinky/engine.h"

extern "C" float safe_walk_motion_multiplier_value;           // "motion_multiplier"
extern "C" unsigned char safe_walk_require_looking_down_value;   // "require_looking_down"
extern "C" unsigned char safe_walk_require_not_forward_value;   // "require_not_forward"
extern "C" unsigned char safe_walk_require_holding_blocks_value;   // "require_holding_blocks"
extern "C" unsigned char safe_walk_safe_walk_active;   // safe-walk active

struct V3 { double x, y, z;
#ifdef __linux__
    ~V3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct WalkState { char padding_00[8]; Ctx *ctx; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char safe_walk_module_descriptor[];

FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(float, java_get_entity_living_base_move_forward, (void *jvm, void *entity_living_base));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(unsigned char, block_entity_id, (void *jvm, void *item));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(V3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(void, java_set_entity_motion_x, (void *jvm, void *entity, V3 value));

extern "C" void safe_walk_on_tick(WalkState *s) {
    Ctx *ctx = s->ctx;
    // the mouse-over dispatch site leaves ctx->player NULL once the world is unloaded, even while the
    // window still has focus, and every accessor below reads the entity
    if (!ctx->player)
        goto clear;
    // in_game is 0 with a screen open, but vanilla still moves the player then - keep the last verdict
    // rather than dropping edge protection mid-inventory
    if (ctx->in_game != 1)
        goto out;
    if (safe_walk_require_looking_down_value == 1) {
        float pitch = get_entity_rotation(ctx->jvm, ctx->player).pitch;
        if (pitch < 65.0f)
            goto clear;
    }
    if (safe_walk_require_not_forward_value == 1) {
        if (java_get_entity_living_base_move_forward(ctx->jvm, ctx->player) > 0.0f)
            goto clear;
    }
    if (safe_walk_require_holding_blocks_value == 1) {
        void *item = item_held_hotbar_slot(ctx->jvm, ctx->player);
        if (!block_entity_id(ctx->jvm, item))
            goto clear;
    }
    safe_walk_safe_walk_active = 1;
    if (!java_get_entity_on_ground(ctx->jvm, ctx->player))
        goto out;
    if (safe_walk_motion_multiplier_value == 1.0f)
        goto out;
    {
        V3 v = java_get_entity_motion_x(ctx->jvm, ctx->player);
        double m = safe_walk_motion_multiplier_value;
        v.x *= m;
        v.z *= m;
        void *jvm = ctx->jvm, *entity = ctx->player;

        java_set_entity_motion_x(jvm, entity, v);
    }
    goto out;
clear:
    safe_walk_safe_walk_active = 0;
out:


    SHARED_EPILOG();
}

extern "C" void safe_walk_on_toggle(EvHook *hook) {
    if (hook->on || hook->ev != safe_walk_module_descriptor)
        return;
    // safe_walk_active is what hook_should_clip_ledge (the isSneaking hook) trusts, and the only code
    // that clears it is this module's own tick - unregistered the moment it is switched off. Left at
    // 1, re-enabling reports the player as sneaking (sneak hitbox + slow walk) for the ticks before
    // the first tick recomputes it, with no sneak key held.
    safe_walk_safe_walk_active = 0;
}
