// Reach - extended attack range with sprint and cooldown requirements.
#include "slinky/api.h"

// ---- settings (see src/modules/combat/reach.cpp) ----
extern "C" float reach_distance;          // "distance" current value
extern "C" float reach_activate_time;          // "activate_time" current value
extern "C" unsigned char reach_require_sprint;  // "require_sprint" current value

// ---- module state ----
extern "C" int reach_last_value_seen_from_java;            // last value seen from java_get_entity_living_base_last_attacker_time
extern "C" int reach_cooldown_ticks;            // cooldown, ticks (re-armed to 9)

// ---- engine helpers ----
extern "C" bool java_call_entity_is_sprinting(void *jvm, void *entity);   // sprint/ground check
extern "C" int java_get_entity_living_base_last_attacker_time(void *jvm, void *entity_living_base);    // target/state id

struct Ctx { void *jvm; char padding_08[0x20]; void *player; };  // +0x00, +0x28

struct ReachState {
    char padding_00[8];
    Ctx *ctx;       // +0x08
    bool active;    // +0x10
    char padding_11[7];
    double reach;   // +0x18
};

extern "C" void reach_on_reach_query(ReachState *s) {
    int c = reach_cooldown_ticks;
    if (c > 0) {
        c--;
        reach_cooldown_ticks = c;
    }
    Ctx *ctx = s->ctx;
    if (!ctx->player)
        return;
    if (reach_require_sprint == 1) {
        if (!java_call_entity_is_sprinting(ctx->jvm, ctx->player))
            return;
        c = reach_cooldown_ticks;
    }
    if (c == 0) {
        int v = java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
        if (v == reach_last_value_seen_from_java) {
            c = reach_cooldown_ticks;
        } else {
            reach_last_value_seen_from_java = v;
            reach_cooldown_ticks = 9;
            c = 9;
        }
    }
    if (reach_activate_time > (float)c) {
        s->active = true;
        s->reach = (double)reach_distance;
    }
}
