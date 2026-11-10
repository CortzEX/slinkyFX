// Hit Select - cancel attacks that would be "bad trades" (only hit when the
// target can be comboed / we are not about to take damage).
//   +0x48  tick:   count ground ticks without being hurt
//   +0x128 attack: decide (once per tick) whether to cancel the attack
//   +0x158 tick:   track the target's hurt time / server-side hit timing
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct AttackHook { char padding_00[8]; Ctx *ctx; unsigned char phase; unsigned char cancel; };
extern "C" float hit_select_cancel_rate_air_value;            // "cancel_rate_air"
extern "C" float hit_select_pause_dur_value;            // "pause_dur"
extern "C" float hit_select_wait_for_damage_dur_value;            // "wait_for_damage_dur"
extern "C" float hit_select_trades_delay_value;            // "trades_delay"
extern "C" float hit_select_cancel_rate_value;            // "cancel_rate"
extern "C" unsigned char hit_select_require_sprint_value;    // "require_sprint"
extern "C" unsigned char hit_select_require_weapon_value;    // "require_weapon"
extern "C" unsigned hit_select_mode;         // "mode"
extern "C" unsigned char hit_select_use_server_time_value;    // "use_server_time"
extern "C" unsigned char hit_select_crits_allow_reduce_value;    // "crits_allow_reduce"
extern "C" unsigned char hit_select_require_damage_value;    // "require_damage"
extern "C" unsigned char hit_select_fake_swing_value;    // "fake_swing"
extern "C" unsigned char hit_select_decided_this_tick;    // decided this tick
extern "C" int hit_select_calm_ticks;              // calm ticks
extern "C" unsigned char hit_select_cancel;    // cancel
extern "C" unsigned hit_select_ticks_since_target_was_hit;         // ticks since target was hit
extern "C" unsigned hit_select_wait_damage_counter;         // wait-for-damage counter
extern "C" unsigned char hit_select_target_just_got_hit;    // target just got hit
extern "C" int hit_select_last_target_id;              // last target id

FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void, java_jni_call_nonvirtual_void_method_entity_living_base_swing_item, (void *jvm, void *object));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(void *, java_get_moving_object_position_entity_hit, (void *jvm, void *moving_object_position));
FN(unsigned char, random_chance, (float chance));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(float, java_get_entity_fall_distance, (void *jvm, void *entity));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, vec3_add_eye_height_sneak, (Vec3 v));
FN(double, libc_floor, (double v));
FN(void *, java_call_variant, (void *jvm, void *world, int x, int y, int z));
FN(int, get_block_id, (void *jvm, void *blk));
FN(int, java_get_entity_entity_id, (void *jvm, void *entity));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));

// whole part of `v` plus a chance roll on the fractional part
static inline unsigned rand_round(float v) {
    unsigned whole = (unsigned)(long long)v;
    return random_chance(v - (float)(unsigned long long)whole) + whole;
}

extern "C" void hit_select_on_tick(Hook *hook) {
    hit_select_decided_this_tick = 0;
    Ctx *ctx = hook->ctx;
    int n = 0;
    if (ctx->in_game == 1 && ctx->player && java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) == 0)
        n = hit_select_calm_ticks + 1;
    hit_select_calm_ticks = n;
}

extern "C" void hit_select_on_attack(AttackHook *hook) {
    Ctx *ctx = hook->ctx;
    if (hook->phase != 1 || !ctx->player)
        return;
    if (hit_select_decided_this_tick == 1) {
        if (!hit_select_cancel)
            return;
        goto cancel;
    }
    {
        if (hit_select_require_sprint_value == 1 && !java_call_entity_is_sprinting(ctx->jvm, ctx->player))
            goto allow;
        if (hit_select_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
            goto allow;
        unsigned char rising = 1, no_ent = 1;
        void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (minecraft) {
            switch (java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft)) {
            case 2:
                no_ent = java_get_moving_object_position_entity_hit(ctx->jvm, minecraft) == nullptr;
                break;
            case 1:
                goto allow;
            case 0:
                if (random_chance(hit_select_cancel_rate_air_value))
                    goto do_cancel;
                break;
            }
        }
        unsigned limit = rand_round(hit_select_pause_dur_value);
        unsigned char ground = java_get_entity_on_ground(ctx->jvm, ctx->player);
        if (ground)
            rising = java_get_entity_motion_x(ctx->jvm, ctx->player).y > 0.0;
        rising &= ((CombatTarget *)ctx->target)->valid;
        if (hit_select_mode == 1) {
            unsigned char early = (hit_select_ticks_since_target_was_hit - 9 < (unsigned)-7) | hit_select_use_server_time_value;
            unsigned char falling;
            if (ground) {
                falling = 0;
            } else {
                falling = java_get_entity_fall_distance(ctx->jvm, ctx->player) > 0.0f;
                if (falling & early)
                    goto allow;
            }
            if (rising && (!early || hit_select_crits_allow_reduce_value))
                goto allow;
            Vec3 eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
            if (java_variant(ctx->jvm))
                eye = vec3_add_eye_height_sneak(eye);
            if (!ctx->world)
                goto allow;
            double ey = eye.y;
            int x = (int)libc_floor(eye.x);
            int y = (int)libc_floor(ey);
            int z = (int)libc_floor(eye.z);
            if (get_block_id(ctx->jvm, java_call_variant(ctx->jvm, ctx->world, x, y, z)) != 0)
                goto allow;
            if (ground | falling)
                goto check;
            if (hit_select_require_damage_value != 1 || java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) != 0)
                goto do_cancel;
            goto check;
        }
        if (hit_select_mode != 0 || rising)
            goto allow;
        if (hit_select_ticks_since_target_was_hit < 15) {
            hit_select_wait_damage_counter = 0;
        } else if (!no_ent) {
            unsigned s = hit_select_wait_damage_counter + 1;
            hit_select_wait_damage_counter = s;
            if (s <= rand_round(hit_select_wait_for_damage_dur_value))
                goto do_cancel;
        }
        if ((unsigned)hit_select_calm_ticks <= 9)
            limit += rand_round(hit_select_trades_delay_value);
        if (hit_select_target_just_got_hit == 1) {
            hit_select_target_just_got_hit = 0;
            goto allow;
        }
check:
        if (limit <= hit_select_ticks_since_target_was_hit + 1)
            goto allow;
        unsigned char r = random_chance(hit_select_cancel_rate_value);
        hit_select_cancel = r;
        hit_select_decided_this_tick = 1;
        if (r)
            goto cancel;
        return;
    }
do_cancel:
    hit_select_cancel = 1;
    hit_select_decided_this_tick = 1;
cancel:
    hook->cancel = 1;
    if (hit_select_fake_swing_value == 1 && ctx->player && !java_call_entity_player_is_using_item(ctx->jvm, ctx->player))
        java_jni_call_nonvirtual_void_method_entity_living_base_swing_item(ctx->jvm, ctx->player);
    return;
allow:
    hit_select_cancel = 0;
    hit_select_decided_this_tick = 1;
}

extern "C" void hit_select_on_player_update(Hook *hook) {
    Ctx *ctx = hook->ctx;
    void *tgt = ((CombatTarget *)ctx->target)->ent;
    if (hit_select_use_server_time_value == 1) {
        if (tgt && java_get_entity_living_base_hurt_time(ctx->jvm, tgt) == 10) {
            hit_select_ticks_since_target_was_hit = 0;
            hit_select_target_just_got_hit = 1;
            return;
        }
        hit_select_ticks_since_target_was_hit += 1;
        return;
    }
    int entity_entity_id = tgt ? java_get_entity_entity_id(ctx->jvm, tgt) : 0;
    if (entity_entity_id == hit_select_last_target_id) {
        unsigned v = hit_select_ticks_since_target_was_hit + 1;
        hit_select_ticks_since_target_was_hit = v;
        if (v < 10)
            return;
    } else {
        hit_select_last_target_id = entity_entity_id;
        hit_select_ticks_since_target_was_hit = 1001;
    }
    if (ctx->player && java_get_entity_ticks_existed(ctx->jvm, ctx->player) == java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player))
        hit_select_ticks_since_target_was_hit = 0;
}
