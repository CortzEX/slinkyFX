// Knockback Delay - descriptors +0x68 (packet hook) / +0x168 (tick).
// +0x168 decides each tick whether delaying is allowed (air / weapon / mouse /
// facing a target in range / chance roll) and publishes it atomically.
// +0x68 holds incoming packets while our own velocity packet is pending, up to
// "max_delay" ms, then releases them.
#include "slinky/engine.h"
#include "slinky/jids.h"

typedef float v4f __attribute__((vector_size(16), aligned(16)));
typedef int v4i __attribute__((vector_size(16), aligned(16)));
struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct PktHook { char padding_00[8]; void *ctx; void *ev; long long budget; int id; int verdict; };  // +0x18/+0x20/+0x24

extern "C" float knockback_delay_max_delay;            // "max_delay"
extern "C" unsigned char knockback_delay_require_air_value;    // "require_air"
extern "C" float knockback_delay_range_value;            // "range"
extern "C" unsigned char knockback_delay_require_weapon_value;    // "require_weapon"
extern "C" unsigned char knockback_delay_require_mouse_value;    // "require_mouse"
extern "C" unsigned char knockback_delay_require_target_value;    // "require_target"
extern "C" float knockback_delay_chance_value;            // "chance"
extern "C" unsigned char knockback_delay_holding;    // holding
extern "C" int knockback_delay_held_transaction_id;              // held transaction id
extern "C" unsigned char knockback_delay_air;    // in air
extern "C" unsigned char knockback_delay_allowed;    // allowed
extern "C" unsigned short knockback_delay_max_delay_second;   // max delay (ms)
extern "C" int knockback_delay_own_entity_id;              // own entity id
extern "C" char knockback_delay_random_walk_1[];           // rng state
extern "C" const double threshold_36_in_knockback_delay;    // max target distance^2
extern "C" const v4f knockback_delay_abs_mask;       // abs mask
extern "C" const float threshold_90_in_knockback_delay;     // max yaw difference
extern "C" const double threshold_0_999;
extern "C" const double offset_neg_0_025;
extern "C" const double offset_0_025;
extern "C" const double knockback_delay_walk_period;


FN(void *, java_jni_is_instance_of_s12_packet_entity_velocity, (void *ctx, void *event));
FN(int, java_get_s12_packet_entity_velocity_entity_id, (void *ctx, void *pkt));
FN(void *, java_jni_is_instance_of_s19_packet_entity_status, (void *ctx, void *event));
FN(void *, java_jni_is_instance_of_s06_packet_update_health, (void *ctx, void *event));
FN(void *, java_jni_is_instance_of_s29_packet_sound_effect, (void *ctx, void *event));
FN(int, java_get_entity_entity_id, (void *jvm, void *entity));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(void *, java_get_world_player_entities, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(Rot, rot_look_at, (Vec3 from, Vec3 to));
FN(float, wrap_angle, (float a));
FN(Rot, rot_make_floats, (float lo, float hi));
FN(unsigned char, walk_should_change, (void *state, Rot rng, double p));

#define ALOAD(x) __atomic_load_n(&(x), __ATOMIC_SEQ_CST)
#define AXCHG(x, v) __atomic_exchange_n(&(x), (v), __ATOMIC_SEQ_CST)

extern "C" void knockback_delay_on_packet_received(PktHook *hook) {
    // No verdict gate here: this hook is both an observer and a holder, and returning early whenever
    // fake_lag / blink / backtrack had already held the packet froze this module's latch, transaction
    // id and timeout, so its own delay window never advanced again.
    bool mine = false;
    void *vel = java_jni_is_instance_of_s12_packet_entity_velocity(hook->ctx, hook->ev);
    if (vel)
        mine = java_get_s12_packet_entity_velocity_entity_id(hook->ctx, vel) == ALOAD(knockback_delay_own_entity_id);
    unsigned char act;
    if (ALOAD(knockback_delay_air) & 1) {
        act = hook->budget == 0 ? (ALOAD(knockback_delay_allowed) & 1) : 0;
        knockback_delay_holding = act;
    } else if ((ALOAD(knockback_delay_allowed) & mine) & 1) {
        knockback_delay_holding = 1;
        act = 1;
    } else {
        act = knockback_delay_holding;
    }
    if ((mine & act) == 1) {
        int st = knockback_delay_held_transaction_id;
        int id = hook->id;
        if (st == 0)
            knockback_delay_held_transaction_id = id;
        else if (st != id)
            goto reset;
    } else if (!act) {
        return;
    }
    if (hook->budget > (long long)ALOAD(knockback_delay_max_delay_second)) {
reset:
        knockback_delay_holding = 0;
        AXCHG(knockback_delay_allowed, (unsigned char)0);
        knockback_delay_held_transaction_id = 0;
        return;
    }
    if (java_jni_is_instance_of_s19_packet_entity_status(hook->ctx, hook->ev) || java_jni_is_instance_of_s06_packet_update_health(hook->ctx, hook->ev) || java_jni_is_instance_of_s29_packet_sound_effect(hook->ctx, hook->ev))
        packet_flush(&hook->verdict);
    else
        packet_hold(&hook->verdict);
}

extern "C" void knockback_delay_on_late_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (!ctx->player || !ctx->world) {
        // Publish "not allowed" instead of returning silently: the packet hook runs on the network thread
        // and would otherwise keep a stale latch + own_entity_id from the last session, which can hold the
        // incoming packets of the next login until the budget times out.
        knockback_delay_holding = 0;
        AXCHG(knockback_delay_air, (unsigned char)0);
        AXCHG(knockback_delay_allowed, (unsigned char)0);
        AXCHG(knockback_delay_max_delay_second, (unsigned short)(int)knockback_delay_max_delay);
        return;
    }
    AXCHG(knockback_delay_own_entity_id, java_get_entity_entity_id(ctx->jvm, ctx->player));
    unsigned char air;
    if (knockback_delay_require_air_value == 1)
        air = !java_get_entity_on_ground(ctx->jvm, ctx->player);
    else
        air = 0;
    AXCHG(knockback_delay_air, air);
    unsigned char ok = 0;
    // Declared before the first goto: MSVC lets a jump step over an initialisation, the SysV C++ front end
    // makes it an error, and the same file has to build for both.
    struct Target { void *padding_00; void *ent; double dist2; } *target = (Target *)ctx->target;
    if (knockback_delay_require_air_value == 1 && !air) {
        knockback_delay_holding = 0;
        goto out;
    }
    if (!target->ent || target->dist2 > (double)(knockback_delay_range_value * knockback_delay_range_value))
        goto out;
    if (knockback_delay_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        goto out;
    if (knockback_delay_require_mouse_value == 1 && !vertex_pointer_current(ctx))
        goto out;
    if (knockback_delay_require_target_value == 1) {
        Vec3 eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
        Rot rot = get_entity_rotation(ctx->jvm, ctx->player);
        void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
        int count = java_call_list_size(ctx->jvm, list);
        int i = 0;
        for (;; i++) {
            if (i >= count)
                goto out;
            void *element = java_call_list_get(ctx->jvm, list, i);
            JNIEnv *obj = ((Jvm *)ctx->jvm)->env;
            auto pred = (*obj)->IsSameObject;
            if (pred(obj, ctx->player, element))
                continue;
            Vec3 position = java_get_entity_pos_x(ctx->jvm, element);
            if (vec3_distance_sq(eye, position) > threshold_36_in_knockback_delay)
                continue;
            v4f d = {wrap_angle(rot_look_at(eye, position).yaw - rot.yaw), 0, 0, 0};
            d = (v4f)((v4i)d & (v4i)knockback_delay_abs_mask);
            if (d[0] > threshold_90_in_knockback_delay)
                continue;
            break;
        }
    }
    {
        double ch = knockback_delay_chance_value;
        ok = 1;
        if (!(ch > threshold_0_999))
            ok = walk_should_change(knockback_delay_random_walk_1, rot_make_floats((float)(offset_neg_0_025 + ch), (float)(ch + offset_0_025)), knockback_delay_walk_period);
    }
out:
    AXCHG(knockback_delay_allowed, ok);
    AXCHG(knockback_delay_max_delay_second, (unsigned short)(int)knockback_delay_max_delay);
}
