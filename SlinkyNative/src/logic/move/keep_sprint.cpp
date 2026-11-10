// Keep Sprint - descriptor +0x128 / +0x138 / +0x158.
// +0x128: on the attack tick, roll "chance" and remember the player's motion.
// +0x138: if the roll succeeded and the player is still moving the same way,
//         re-apply the saved motion scaled by "motion_mult".
// +0x158: afterwards re-assert sprint on the player.
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct PhaseHook { char padding_00[8]; Ctx *ctx; unsigned char phase; };   // +0x10: phase flag
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char keep_sprint_module_descriptor[];
extern "C" float keep_sprint_chance_value;           // "chance"
extern "C" float keep_sprint_motion_mult;           // "motion_mult"
extern "C" unsigned char keep_sprint_require_on_ground_value;   // "require_on_ground"
extern "C" unsigned char keep_sprint_allow_reduce_value;   // "allow_reduce"
extern "C" Vec3 keep_sprint_saved_motion;            // saved motion
extern "C" unsigned char keep_sprint_roll_succeeded;   // roll succeeded
extern "C" unsigned char keep_sprint_motion_pending_re_apply;   // motion pending re-apply

// Velocity "Reduce" scales the same pre-hit motion from the same post-attack event (it runs first).
extern "C" float velocity_attack_motion_scale;

FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(unsigned char, random_chance, (float chance));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(void, java_set_entity_motion_x, (void *jvm, void *entity, Vec3 value));
FN(void, java_call_entity_set_sprinting, (void *jvm, void *entity, bool on));

extern "C" void keep_sprint_on_attack(PhaseHook *hook) {
    if (hook->phase == 1) {
        Ctx *ctx = hook->ctx;
        unsigned char ok = 0;
        // the click hook dispatches attack events without checking the player, and is_sprinting /
        // reading motion off a NULL jobject faults
        if (ctx->in_game == 1 && ctx->player && java_call_entity_is_sprinting(ctx->jvm, ctx->player) && random_chance(keep_sprint_chance_value)) {
            keep_sprint_saved_motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
            ok = 1;
        }
        keep_sprint_roll_succeeded = ok;
    }
}

extern "C" void keep_sprint_on_attack_post(PhaseHook *hook) {
    float velocity_scale = velocity_attack_motion_scale;
    velocity_attack_motion_scale = 1.0f;
    if (keep_sprint_roll_succeeded != 1 || hook->phase != 1)
        return;
    Ctx *ctx = hook->ctx;
    if (!ctx->player)
        return;
    if (keep_sprint_require_on_ground_value == 1 && !java_get_entity_on_ground(ctx->jvm, ctx->player))
        return;
    if (keep_sprint_allow_reduce_value == 1 && ((CombatTarget *)ctx->target)->valid)
        return;
    // lastAttackerTime is only bumped when the player is the one taking damage, so it cannot be used
    // to detect a landed attack - the click hook itself is the trigger
    double m = keep_sprint_motion_mult * velocity_scale;
    keep_sprint_saved_motion.x *= m;
    keep_sprint_saved_motion.z *= m;
    void *jvm = ctx->jvm, *player = ctx->player;
    asm volatile("" ::: "memory");   // original reloads the saved motion from memory here
    java_set_entity_motion_x(jvm, player, keep_sprint_saved_motion);
    keep_sprint_motion_pending_re_apply = 1;
}

extern "C" void keep_sprint_on_player_update(Hook *hook) {
    if (keep_sprint_motion_pending_re_apply == 1) {
        keep_sprint_motion_pending_re_apply = 0;
        Ctx *ctx = hook->ctx;
        if (ctx->player) {
            java_call_entity_set_sprinting(ctx->jvm, ctx->player, true);
            return;
        }
    }
    EPILOG_NOP();
}

extern "C" void keep_sprint_on_toggle(EvHook *hook) {
    if (hook->on || hook->ev != keep_sprint_module_descriptor)
        return;
    // Toggling off between the attack pair and the following world update used to leave the pending
    // re-apply set: the next time the module was switched on, its first tick forced sprint on for no
    // reason, and the stale roll could carry a saved motion across the gap.
    keep_sprint_motion_pending_re_apply = 0;
    keep_sprint_roll_succeeded = 0;
    keep_sprint_saved_motion = { 0.0, 0.0, 0.0 };
}
