// Knockback Displace - when hitting a target, snap the rotation sideways for a
// tick so the knockback is pushed in "direction" by "angle" (with jitter),
// optionally hiding the rotation change from the server-side view.
//   +0x78  packet hook: hold while rotation is hidden
//   +0x118 input hook:  cancel the swing input while displacing
//   +0x128 attack hook: start a displacement
//   +0x138 / +0x168     tick bookkeeping, restore rotation
//   0x4ab90             direction: -1 / +1
#include "slinky/engine.h"

typedef float v4f __attribute__((vector_size(16), aligned(16)));
typedef int v4i __attribute__((vector_size(16), aligned(16)));
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct AttackHook { char padding_00[8]; Ctx *ctx; unsigned char phase; unsigned char handled; };   // +0x10 / +0x11
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct PktHook { char padding_00[0x24]; int verdict; };
extern "C" char knockback_displace_module_descriptor[];     // Knockback Displace module descriptor

extern "C" float knockback_displace_cooldown_value;            // "cooldown"
extern "C" float knockback_displace_angle;            // "angle"
extern "C" unsigned char knockback_displace_require_no_kb_value;    // "require_no_kb"
extern "C" unsigned char knockback_displace_require_weapon_value;    // "require_weapon"
extern "C" unsigned char knockback_displace_require_kb_enchant_value;    // "require_kb_enchant"
extern "C" unsigned char knockback_displace_require_facing_away_value;    // "require_facing_away"
extern "C" unsigned knockback_displace_direction_value;         // "direction"
extern "C" unsigned char knockback_displace_hide_rotation_value;    // "hide_rotation"
extern "C" unsigned char knockback_displace_use_last_strafe_value;    // "use_last_strafe"
extern "C" unsigned char knockback_displace_rotation_hidden;    // rotation hidden
extern "C" unsigned char knockback_displace_displacing_this_tick;    // displacing this tick
extern "C" unsigned char knockback_displace_displaced_last_tick;    // displaced last tick
extern "C" unsigned char knockback_displace_hit_seen_this_tick;    // hit seen this tick
extern "C" unsigned knockback_displace_ticks_since_reset;         // ticks since reset
extern "C" unsigned knockback_displace_ticks_since_displacement;         // ticks since displacement
extern "C" Rot knockback_displace_rotation_before_displacement;              // rotation before displacement
extern "C" int knockback_displace_last_strafe_sign;              // last strafe sign
extern "C" const float chance_0_5_in_knockback_displace;
extern "C" const float ten_in_knockback_displace;
extern "C" const float random_range_bound_20_in_knockback_displace;
extern "C" const v4i knockback_displace_sign_mask;       // sign mask
extern "C" const float chance_0_5_in_knockback_displace_a;

FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(void *, java_get_moving_object_position_entity_hit, (void *jvm, void *moving_object_position));
FN(void *, java_jni_is_instance_of_entity_other_player_mp, (void *jvm, void *candidate));
FN(unsigned char, entity_flags_has, (void *jvm, void *entity, int mode));
FN(unsigned char, timer_range_is_delay_pending, ());
FN(unsigned char, is_held_item_accepted_weapon, (void *jvm, void *item, unsigned *flags));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(unsigned char, yaw_diff_check, (Ctx *ctx, Rot rotation, void *target));
FN(unsigned char, update_rotation_priority_peak, (int ms));
FN(void, apply_player_rotation, (Ctx *ctx, unsigned char a, int b, int d, Rot rotation));
FN(unsigned char, random_chance, (float chance));
FN(Rot, rot_make_floats, (float lo, float hi));
FN(float, random_rotation, (Rot rng));
FN(void, apply_silent_rotation, (Rot rotation));
FN(void, set_rotation_active, (void *jvm, void *object, int x));
FN(float, java_get_entity_living_base_move_strafing, (void *jvm, void *entity_living_base));
FN(Rot, get_silent_rotation_offset, ());
FN(Rot, rot_add, (Rot a, Rot b));
FN(Rot, rot_sub, (Rot a, Rot b));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void, java_set_minecraft_left_click_counter, (void *jvm, void *minecraft, int y));
FN(void *, java_get_game_settings_attack_key, (void *jvm, void *game_settings));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
extern "C" int knockback_displace_direction();

extern "C" void knockback_displace_on_packet_sent(PktHook *hook) {
    if (__atomic_load_n(&knockback_displace_rotation_hidden, __ATOMIC_SEQ_CST) & 1)
        packet_hold(&hook->verdict);
}

extern "C" void knockback_displace_on_rotation_update(char *h) {
    if (__builtin_expect(knockback_displace_displacing_this_tick != 0, 0))
        goto clear;
    JOIN();   // keeps the two flag tests as separate branches, as in the original
    if (__builtin_expect(knockback_displace_displaced_last_tick == 0, 1))
        return;
clear:
    h[9] = 0;
}

extern "C" void knockback_displace_on_attack(AttackHook *hook) {
    // Event 0xe is both mouse buttons; only the left one is a swing. The displacement check used to run before
    // this test, so a right click in the tick a displacement started was cancelled too, and a chest, NPC panel or
    // door opened in that same tick never reached the game.
    if (hook->phase != 1)
        return;
    if (knockback_displace_displacing_this_tick == 1) {
        hook->handled = 1;
        return;
    }
    Ctx *ctx = hook->ctx;
    // is_held_item_accepted_weapon (= weapons_check_held_item_cached) reads ticksExisted straight off the
    // player, so every accessor below needs a live entity.
    if (!ctx->player)
        return;
    void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
    if (!minecraft || java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) != 2)
        return;
    void *ent = java_jni_is_instance_of_entity_other_player_mp(ctx->jvm, java_get_moving_object_position_entity_hit(ctx->jvm, minecraft));
    if (!ent || entity_flags_has(ctx->jvm, ent, 7))
        return;
    knockback_displace_hit_seen_this_tick = 1;
    if (knockback_displace_displaced_last_tick || knockback_displace_ticks_since_reset < 9)
        return;
    if (knockback_displace_cooldown_value > (float)knockback_displace_ticks_since_displacement)
        return;
    if (timer_range_is_delay_pending())
        return;
    if (knockback_displace_require_no_kb_value == 1 && ((CombatTarget *)ctx->target)->valid)
        return;
    unsigned flags;
    unsigned char weapon = is_held_item_accepted_weapon(ctx->jvm, ctx->player, &flags);
    if (knockback_displace_require_weapon_value == 1 && !weapon)
        return;
    if (!(flags & 2) && (knockback_displace_require_kb_enchant_value || !java_call_entity_is_sprinting(ctx->jvm, ctx->player)))
        return;
    Rot rotation = get_entity_rotation(ctx->jvm, ctx->player);
    knockback_displace_rotation_before_displacement = rotation;
    if (knockback_displace_require_facing_away_value == 1 && !yaw_diff_check(ctx, rotation, ent))
        return;
    int dir = knockback_displace_direction();
    if (!dir || !update_rotation_priority_peak(200))
        return;
    apply_player_rotation(ctx, 1, 0, 0, knockback_displace_rotation_before_displacement);
    knockback_displace_displacing_this_tick = 1;
    unsigned char keep = random_chance(chance_0_5_in_knockback_displace);
    float ang = knockback_displace_angle;
    float fdir = (float)dir;
    float jit = ten_in_knockback_displace;
    ang = (ang - random_rotation(rot_make_floats(0.0f, jit))) * fdir;
    float pitch = random_rotation(rot_make_floats(jit, random_range_bound_20_in_knockback_displace));
    if (!keep) {
        v4f p = {pitch, 0, 0, 0};
        pitch = ((v4f)((v4i)p ^ knockback_displace_sign_mask))[0];
    }
    apply_silent_rotation(Rot{ang, pitch});
    set_rotation_active(ctx->jvm, ctx->player, 0);
    if (knockback_displace_hide_rotation_value == 1)
        __atomic_exchange_n(&knockback_displace_rotation_hidden, (unsigned char)1, __ATOMIC_SEQ_CST);
    hook->handled = 1;
    knockback_displace_ticks_since_displacement = 0;
}

extern "C" void knockback_displace_on_attack_post(AttackHook *hook) {
    if (hook->phase == 1 && knockback_displace_ticks_since_reset >= 10 && (knockback_displace_hit_seen_this_tick & 1))
        knockback_displace_ticks_since_reset = 0;
}

extern "C" void knockback_displace_on_late_tick(Hook *hook) {
    knockback_displace_ticks_since_reset += 1;
    knockback_displace_ticks_since_displacement += 1;
    knockback_displace_displaced_last_tick = knockback_displace_displacing_this_tick;
    knockback_displace_hit_seen_this_tick = 0;
    Ctx *ctx = hook->ctx;
    if (ctx->in_game == 1 && ctx->player) {
        float s = java_get_entity_living_base_move_strafing(ctx->jvm, ctx->player);
        int sgn = (s > 0.0f) - (0.0f > s);
    asm("" : "+r"(sgn));
        if (knockback_displace_use_last_strafe_value != 1 || sgn != 0)
            knockback_displace_last_strafe_sign = sgn;
        if (knockback_displace_displacing_this_tick != 1)
            goto publish;
        if (update_rotation_priority_peak(200)) {
            Rot now = get_entity_rotation(ctx->jvm, ctx->player);
            Rot d = rot_add(now, get_silent_rotation_offset());
            apply_silent_rotation(rot_sub(knockback_displace_rotation_before_displacement, d));
            set_rotation_active(ctx->jvm, ctx->player, 0);
            void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
            java_set_minecraft_left_click_counter(ctx->jvm, ctx->minecraft, 0);
            java_increment_key_binding_press_time(ctx->jvm, java_get_game_settings_attack_key(ctx->jvm, obj));
        }
    }
    knockback_displace_displacing_this_tick = 0;
publish:
    JOIN();
    {
        unsigned v = 0;
        asm("" : "+r"(v));
        if (knockback_displace_displaced_last_tick)
            v = *(volatile unsigned *)&knockback_displace_hide_rotation_value;   // original reads the setting as a dword here
        __atomic_exchange_n(&knockback_displace_rotation_hidden, (unsigned char)v, __ATOMIC_SEQ_CST);
    }
}

FN(void, java_increment_key_binding_press_time, (void *a, void *k));
extern "C" void knockback_displace_on_toggle(EvHook *hook) {
    if (hook->ev != knockback_displace_module_descriptor || hook->on)
        return;
    // rotation_hidden makes the packet hook hold every outgoing packet and the late tick is what clears it
    // again, so both flags have to be dropped here: re-enabling the module would otherwise start by
    // swallowing the player's movement packets until the next late tick happens to publish a zero.
    Ctx *ctx = hook->ctx;
    if (knockback_displace_displacing_this_tick == 1 && ctx->in_game == 1 && ctx->player) {
        Rot now = get_entity_rotation(ctx->jvm, ctx->player);
        Rot d = rot_add(now, get_silent_rotation_offset());
        apply_silent_rotation(rot_sub(knockback_displace_rotation_before_displacement, d));
        set_rotation_active(ctx->jvm, ctx->player, 0);
    }
    knockback_displace_displacing_this_tick = 0;
    knockback_displace_displaced_last_tick = 0;
    knockback_displace_hit_seen_this_tick = 0;
    __atomic_exchange_n(&knockback_displace_rotation_hidden, (unsigned char)0, __ATOMIC_SEQ_CST);
}

extern "C" int knockback_displace_direction() {
    int result;
    switch (knockback_displace_direction_value) {
    case 0: result = -1; break;
    case 4: result = -knockback_displace_last_strafe_sign; break;
    case 2: result = (random_chance(chance_0_5_in_knockback_displace_a) - 1) | 1; break;
    case 3: result = knockback_displace_last_strafe_sign; break;
    case 1: result = 1; break;
    default: __builtin_unreachable();
    }

    return result;
}
