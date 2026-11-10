// Velocity - knockback modification: save the motion at hit time and apply the
// configured reduction.
#include "slinky/engine.h"
#include "slinky/jids.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
typedef float v4f __attribute__((vector_size(16), aligned(16)));
typedef int v4i __attribute__((vector_size(16), aligned(16)));
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Range { float lo, hi;
#ifdef __linux__
    ~Range() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct PhaseHook { char padding_00[8]; Ctx *ctx; unsigned char phase; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char velocity_module_descriptor[];           // Velocity module descriptor
extern "C" unsigned char velocity_tick_motion_saved;    // tick motion saved
extern "C" Vec3 velocity_motion_at_last_tick;             // motion at the last tick
extern "C" Vec3 velocity_motion_at_our_last_hit;             // motion at our last hit
extern "C" unsigned velocity_mode;         // mode
extern "C" float velocity_chance_setting;            // "chance"
extern "C" unsigned velocity_keep_vertical_1_air_2;         // keep vertical: 1 = in air, 2 = always
extern "C" unsigned char velocity_own_velocity_packet_seen;    // own velocity packet seen (atomic)
extern "C" char velocity_random_walk_1[];           // rng state
extern "C" int velocity_own_entity_id;              // own entity id
extern "C" unsigned char velocity_strong_knockback;    // strong knockback (atomic)
extern "C" const float threshold_99;
extern "C" unsigned char velocity_require_moving_setting;    // "require_moving"
extern "C" unsigned char velocity_require_weapon_value;    // "require_weapon"
extern "C" unsigned char velocity_every_hit_setting;    // "every_hit"
extern "C" unsigned char velocity_reduced_this_combo;    // reduced this combo
extern "C" const double velocity_reduce_factor;    // reduce factor
extern "C" double factor_0_01_in_velocity, offset_neg_0_05, offset_0_05_in_velocity, velocity_ignore_walk_period, threshold_0_05_in_velocity_a;
struct PktHook { char padding_00[8]; void *ctx; void *ev; char padding_18[12]; int verdict; };
FN(unsigned char, velocity_shared_conditions_entry, (Ctx *ctx));
extern "C" unsigned char velocity_require_mouse_value, velocity_require_target_value, velocity_require_sprinting_jump_value;
extern "C" float velocity_reduce_chance_value, velocity_reduce_delay_value, velocity_reduce_amount, velocity_fov_value, velocity_jump_chance_value;
extern "C" int velocity_delay_ticks;              // delay ticks
extern "C" unsigned char velocity_jump_key_pressed_by_us;    // jump key pressed by us
extern "C" const float threshold_99_in_velocity;
extern "C" const double factor_0_01_in_velocity_a, offset_neg_0_05_in_velocity, offset_0_05_in_velocity_b, velocity_chance_walk_period, neg_pi_in_velocity, half_turn_degrees_in_velocity, threshold_36;
extern "C" const v4i velocity_abs_mask;
extern "C" const float threshold_90_in_velocity;
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(void *, java_get_world_player_entities, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(Rot, rot_look_at, (Vec3 from, Vec3 to));
FN(float, wrap_angle, (float a));
FN(int, java_get_entity_entity_id, (void *jvm, void *entity));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, get_held_item, (void *jvm, void *item));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(unsigned char, is_key_down, (void *jvm, void *k));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(void *, java_potion_potion_types, (void *jvm, int id));
FN(void *, java_call_entity_living_base_get_active_potion_effect, (void *jvm, void *entity_living_base, void *effect));
FN(double, libc_hypot, (double x, double z));
FN(float, libc_sinf, (float v));
FN(float, libc_cosf, (float v));
FN(double, libc_acos, (double v));
FN(float, java_get_entity_living_base_move_forward, (void *jvm, void *entity_living_base));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(Range, rot_make_floats, (float lo, float hi));
FN(unsigned char, walk_should_change, (void *state, Range r, double p));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(void, java_set_entity_motion_x, (void *jvm, void *entity, Vec3 value));
FN(void *, java_jni_is_instance_of_s12_packet_entity_velocity, (void *ctx, void *event));
FN(int, java_get_s12_packet_entity_velocity_entity_id, (void *ctx, void *s12_packet_entity_velocity));
FN(void *, java_jni_is_instance_of_s27_packet_explosion, (void *ctx, void *event));
FN(float, java_get_s27_packet_explosion_f, (void *ctx, void *s27_packet_explosion));
FN(float, java_get_s27_packet_explosion_h, (void *ctx, void *s27_packet_explosion));
FN(void *, java_jni_is_instance_of_s08_packet_player_pos_look, (void *ctx, void *event));

FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));

// "reduce" factor for the current attack, shared with Keep Sprint: both handlers rescale the same
// pre-hit motion from event 0xf and the lower-priority one used to simply overwrite the other
extern "C" float velocity_attack_motion_scale = 1.0f;

extern "C" void velocity_disable(EvHook *hook) {
    if (hook->ev != velocity_module_descriptor || hook->on)
        return;
    velocity_tick_motion_saved = 0;
    velocity_delay_ticks = 0;
    velocity_attack_motion_scale = 1.0f;
    __atomic_exchange_n(&velocity_strong_knockback, (unsigned char)0, __ATOMIC_SEQ_CST);
    // Mode 1 presses the jump key and undoes that press on the next update. Disabling unregisters that
    // update, so the press has to be released here or KeyBinding.pressed stays set (endless hopping).
    if (velocity_jump_key_pressed_by_us == 1) {
        Ctx *ctx = hook->ctx;
        void *key = get_held_item(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft));
        java_set_key_binding_pressed(ctx->jvm, key, java_get_key_binding_key_code(ctx->jvm, key));
        velocity_jump_key_pressed_by_us = 0;
    }
}

extern "C" void velocity_attack(PhaseHook *hook) {
    if (hook->phase != 1)
        return;
    Ctx *ctx = hook->ctx;
    if (ctx->player)
        velocity_motion_at_our_last_hit = java_get_entity_motion_x(ctx->jvm, ctx->player);
}

extern "C" void velocity_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->player) {
        velocity_motion_at_last_tick = java_get_entity_motion_x(ctx->jvm, ctx->player);
        velocity_tick_motion_saved = 1;
    }
}

// mode 3 ("delayed"): after our own velocity packet, restore the motion of
// the previous tick (optionally keeping the new vertical motion)
extern "C" void velocity_mode_3_delayed(Hook *hook) {
    if (velocity_mode != 3)
        return;
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1 || !ctx->player)
        return;
    if (__builtin_expect(__atomic_load_n(&velocity_own_velocity_packet_seen, __ATOMIC_SEQ_CST) & 1, 1)) {
        __atomic_exchange_n(&velocity_own_velocity_packet_seen, (unsigned char)0, __ATOMIC_SEQ_CST);
        return;
    }
    if (velocity_tick_motion_saved != 1 || !velocity_shared_conditions_entry(ctx))
        return;
    float ch = velocity_chance_setting;
    if (!(ch > threshold_99)) {
        double p = (float)((double)ch * factor_0_01_in_velocity);
        if (!walk_should_change(velocity_random_walk_1, rot_make_floats((float)(offset_neg_0_05 + p), (float)(p + offset_0_05_in_velocity)), velocity_ignore_walk_period))
            return;
    }
    Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
    motion.x = velocity_motion_at_last_tick.x;
    unsigned keep = velocity_keep_vertical_1_air_2;
    if (keep == 2 || (keep == 1 && !java_get_entity_on_ground(ctx->jvm, ctx->player)))
        motion.y = velocity_motion_at_last_tick.y;
    motion.z = velocity_motion_at_last_tick.z;
    java_set_entity_motion_x(ctx->jvm, ctx->player, motion);
    velocity_tick_motion_saved = 0;
}

// packet hook: watch our own velocity packet / explosions / strong knockback
extern "C" void velocity_packet_hook_watch(PktHook *hook) {
    // Purely an observer: it never writes a verdict, but the old early-out on verdict != 0 meant
    // fake_lag / blink / backtrack / lag_range holding a packet also hid this module's own
    // S12/S27/S08 packets from it, so reduce mode silently missed the knockback it was waiting for.
    {
        void *s12_packet_entity_velocity = java_jni_is_instance_of_s12_packet_entity_velocity(hook->ctx, hook->ev);
        if (s12_packet_entity_velocity) {
            if (java_get_s12_packet_entity_velocity_entity_id(hook->ctx, s12_packet_entity_velocity) == velocity_own_entity_id)
                __atomic_exchange_n(&velocity_strong_knockback, (unsigned char)1, __ATOMIC_SEQ_CST);
            goto end;
        }
        void *s27_packet_explosion = java_jni_is_instance_of_s27_packet_explosion(hook->ctx, hook->ev);
        if (s27_packet_explosion) {
            float s27_packet_explosion_f = java_get_s27_packet_explosion_f(hook->ctx, s27_packet_explosion);
            float s27_packet_explosion_h = java_get_s27_packet_explosion_h(hook->ctx, s27_packet_explosion);
            __atomic_exchange_n(&velocity_strong_knockback, (unsigned char)((double)(s27_packet_explosion_f * s27_packet_explosion_f + s27_packet_explosion_h * s27_packet_explosion_h) > threshold_0_05_in_velocity_a), __ATOMIC_SEQ_CST);
            goto end;
        }
        if (java_jni_is_instance_of_s08_packet_player_pos_look(hook->ctx, hook->ev))
            __atomic_exchange_n(&velocity_own_velocity_packet_seen, (unsigned char)1, __ATOMIC_SEQ_CST);
    }
end:

}

// mode 2 ("reduce"): on our hit while the target is hurt, scale the saved motion
extern "C" void velocity_mode_2_reduce(PhaseHook *hook) {
    velocity_attack_motion_scale = 1.0f;
    if (velocity_mode != 2)
        return;
    if (velocity_reduced_this_combo == 1 && !velocity_every_hit_setting)
        return;
    if (hook->phase != 1)
        return;
    Ctx *ctx = hook->ctx;
    if (!ctx->player || ((CombatTarget *)ctx->target)->valid != 1)
        return;
    if (velocity_require_moving_setting == 1 && 0.0f >= java_get_entity_living_base_move_forward(ctx->jvm, ctx->player))
        return;
    if (velocity_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        return;
    if (java_get_entity_ticks_existed(ctx->jvm, ctx->player) != java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player))
        return;
    double k = velocity_reduce_factor;
    velocity_attack_motion_scale = (float)k;
    velocity_motion_at_our_last_hit.x *= k;
    velocity_motion_at_our_last_hit.z *= k;
    void *jvm = ctx->jvm, *player = ctx->player;
    asm volatile("" ::: "memory");   // original reloads the motion from memory here
    java_set_entity_motion_x(jvm, player, velocity_motion_at_our_last_hit);
    velocity_reduced_this_combo = 1;
}

// shared conditions (moving / mouse / weapon / facing a target in range)
extern "C" unsigned char velocity_shared_conditions(Ctx *ctx) {
    if (!ctx->player || !ctx->world)
        return 0;
    if (velocity_require_moving_setting == 1 && 0.0f >= java_get_entity_living_base_move_forward(ctx->jvm, ctx->player))
        return 0;
    if (velocity_require_mouse_value == 1 && !vertex_pointer_current(ctx))
        return 0;
    if (velocity_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        return 0;
    if (velocity_require_target_value != 1)
        return 1;
    Vec3 eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
    Rot rot = get_entity_rotation(ctx->jvm, ctx->player);
    void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
    int count = java_call_list_size(ctx->jvm, list);
    for (int i = 0; i < count; i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        JNIEnv *obj = ((Jvm *)ctx->jvm)->env;
        if ((*obj)->IsSameObject(obj, ctx->player, element))
            continue;
        Vec3 position = java_get_entity_pos_x(ctx->jvm, element);
        if (vec3_distance_sq(eye, position) > threshold_36)
            continue;
        v4f d = {wrap_angle(rot_look_at(eye, position).yaw - rot.yaw), 0, 0, 0};
        d = (v4f)((v4i)d & velocity_abs_mask);
        if (d[0] > threshold_90_in_velocity)
            continue;
        return 1;
    }
    return 0;
}

static inline __attribute__((always_inline)) bool roll(float ch) {
    if (ch > threshold_99_in_velocity)
        return true;
    double p = (float)((double)ch * factor_0_01_in_velocity_a);
    return walk_should_change(velocity_random_walk_1, rot_make_floats((float)(offset_neg_0_05_in_velocity + p), (float)(p + offset_0_05_in_velocity_b)), velocity_chance_walk_period);
}

// tick: mode 0 = cancel horizontal knockback after a delay when hit from the
// front, mode 1 = jump reset
extern "C" void velocity_tick_mode_0(Hook *hook) {
    velocity_reduced_this_combo = 0;
    Ctx *ctx = hook->ctx;
    // The pending jump-key release is restored before the mode checks: changing the mode away from "jump"
    // used to skip the branch below and leave the key pressed forever.
    if (velocity_jump_key_pressed_by_us == 1) {
        void *held_item = get_held_item(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft));
        void *jvm = ctx->jvm;
        java_set_key_binding_pressed(jvm, held_item, java_get_key_binding_key_code(jvm, held_item));
        velocity_jump_key_pressed_by_us = 0;
        return;
    }
    if (velocity_mode == 2)
        return;
    if (ctx->in_game != 1 || !ctx->player)
        return;
    __atomic_exchange_n(&velocity_own_entity_id, java_get_entity_entity_id(ctx->jvm, ctx->player), __ATOMIC_SEQ_CST);
    unsigned mode = velocity_mode;
    if (mode == 1) {
        if (!velocity_shared_conditions(ctx) || !roll(velocity_jump_chance_value))
            return;
        if (velocity_require_sprinting_jump_value == 1 && !java_call_entity_is_sprinting(ctx->jvm, ctx->player))
            return;
        if (!java_get_entity_on_ground(ctx->jvm, ctx->player))
            return;
        if (0.0 >= java_get_entity_motion_x(ctx->jvm, ctx->player).y)
            return;
        if (java_call_entity_living_base_get_active_potion_effect(ctx->jvm, ctx->player, java_potion_potion_types(ctx->jvm, 8)))
            return;
        void *key_binding = get_held_item(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft));
        if (is_key_down(ctx->jvm, key_binding))
            return;
        java_set_key_binding_pressed(ctx->jvm, key_binding, 1);
        velocity_jump_key_pressed_by_us = 1;
        return;
    }
    if (mode != 0)
        return;
    int n = velocity_delay_ticks;
    if (n <= 0) {
        unsigned char expected = 1;
        if (!__atomic_compare_exchange_n(&velocity_strong_knockback, &expected, 0, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return;
        if (!velocity_shared_conditions(ctx) || !roll(velocity_reduce_chance_value))
            return;
        Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
        double d = libc_hypot(motion.x, motion.z);
        if (d == 0.0)
            return;
        float yaw = get_entity_rotation(ctx->jvm, ctx->player).yaw;
        float ang = (float)((double)yaw * neg_pi_in_velocity / half_turn_degrees_in_velocity + neg_pi_in_velocity);
        float sn = libc_sinf(ang);
        float cs = libc_cosf(ang);
        double q = libc_acos(((double)sn * motion.x + (double)cs * motion.z) / d);
        if (!((double)velocity_fov_value >= q))
            return;
        n = (int)velocity_reduce_delay_value;
        velocity_delay_ticks = n;
        if (n <= 0)
            goto apply;
    }
    velocity_delay_ticks = n - 1;
    if (n - 1 != 0)
        return;
apply:
    {
        Vec3 entity_motion_x = java_get_entity_motion_x(ctx->jvm, ctx->player);
        double f = velocity_reduce_amount;
        entity_motion_x.x *= f;
        entity_motion_x.z *= f;
        java_set_entity_motion_x(ctx->jvm, ctx->player, entity_motion_x);
    }
}
