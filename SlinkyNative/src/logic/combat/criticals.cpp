// Criticals - descriptors +0x48 (decide), +0x58 (timer), +0x78 (packet hook), +0x98 (disable).
//   mode 0 ("packet"): hold outgoing packets for up to "max_delay" ms around a hit
//   mode 1 ("timer"):  slow the game timer while falling onto a target so the hit crits
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct PktHook { char padding_00[8]; void *ctx; void *ev; long long budget; int seq; int verdict; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct TimerHook { char padding_00[8]; Ctx *ctx; char padding_10[8]; double dt; };          // +0x18

extern "C" float criticals_max_delay_value;            // "max_delay"
extern "C" float criticals_chance_value;            // "chance"
extern "C" unsigned char criticals_require_mouse_pressed_value;    // "require_mouse_pressed"
extern "C" unsigned char criticals_require_weapon_value;    // "require_weapon"
extern "C" unsigned criticals_mode;         // "mode"
extern "C" float criticals_timer_speed_value;            // "timer_speed"
extern "C" float criticals_boost_value;            // "boost"
extern "C" char criticals_module_descriptor[];           // Criticals module descriptor
extern "C" unsigned char criticals_packet_mode_armed;    // packet mode armed
extern "C" unsigned short criticals_packet_delay;   // packet delay (ms)
extern "C" unsigned char criticals_timer_mode_armed;    // timer mode armed
extern "C" int criticals_roll_cooldown;              // roll cooldown
extern "C" unsigned char criticals_last_roll;    // last roll
extern "C" unsigned char criticals_timer_modified;    // timer modified
extern "C" double criticals_accumulated_boost;           // accumulated boost
extern "C" int criticals_current_hold_budget;              // current hold budget
extern "C" double criticals_last_y;           // last y
extern "C" int criticals_last_sequence;              // last sequence
extern "C" unsigned char criticals_falling;    // falling
extern "C" const double threshold_1_in_criticals;
extern "C" float timer_speed_1_in_criticals;
extern "C" double factor_20_in_criticals;
extern "C" float timer_speed_1_in_criticals_a;

FN(unsigned char, random_chance, (float chance));
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(void *, get_timer, (void *jvm, void *timer));
FN(float, java_get_timer_timer_speed, (void *jvm, void *timer));
FN(void, java_set_timer_timer_speed, (void *jvm, void *timer, float speed));
FN(float, java_get_timer_elapsed_partial_ticks, (void *jvm, void *timer));
FN(void, java_set_timer_elapsed_partial_ticks, (void *jvm, void *timer, float value));
FN(void *, java_jni_is_instance_of_c03_packet_player, (void *ctx, void *event));
FN(unsigned char, java_get_c03_packet_player_moving, (void *ctx, void *c03_packet_player));
FN(double, java_get_c03_packet_player_b, (void *ctx, void *c03_packet_player));
FN(unsigned char, java_get_c03_packet_player_on_ground, (void *ctx, void *c03_packet_player));

#define AXCHG(x, v) __atomic_exchange_n(&(x), (v), __ATOMIC_SEQ_CST)
#define ALOAD(x) __atomic_load_n(&(x), __ATOMIC_SEQ_CST)

extern "C" void criticals_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    bool ok = false;
    if (ctx->in_game == 1 && ctx->player) {
        int n = criticals_roll_cooldown;
        if (n != 0) {
            unsigned char last = criticals_last_roll;
            criticals_roll_cooldown = n - 1;
            if (last != 1)
                goto decided;
        } else {
            criticals_roll_cooldown = 10;
            unsigned char r = random_chance(criticals_chance_value);
            criticals_last_roll = r;
            criticals_roll_cooldown += -1;
            if (!r)
                goto decided;
        }
        if (criticals_require_mouse_pressed_value == 1 && !vertex_pointer_current(ctx))
            goto decided;
        if (criticals_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
            goto decided;
        int ht = java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
        ok = java_get_entity_ticks_existed(ctx->jvm, ctx->player) - ht < 11;
    }
decided:

    unsigned char v;
    if (criticals_mode == 0) {
        AXCHG(criticals_packet_mode_armed, (unsigned char)ok);
        AXCHG(criticals_packet_delay, (unsigned short)(int)criticals_max_delay_value);
        goto zero;
    }
    if (criticals_mode != 1)
        return;
    AXCHG(criticals_packet_mode_armed, (unsigned char)0);
    if (!ok)
        goto zero;
    if (java_get_entity_on_ground(ctx->jvm, ctx->player))
        goto zero;
    {
        Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
        if (!(motion.y < 0.0))
            goto zero;
    }
    if (java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) == 10)
        goto zero;
    {
        void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (!minecraft)
            goto zero;
        v = java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) == 2;
        goto store;
    }
zero:
    v = 0;
store:
    criticals_timer_mode_armed = v;
}

extern "C" void criticals_on_frame(TimerHook *hook) {
    Ctx *ctx = hook->ctx;
    void *timer = get_timer(ctx->jvm, ctx->minecraft);
    if (criticals_timer_mode_armed == 1) {
        // Timer Range freezes the timer at 0.0 while a burst is pending: yield instead of overwriting
        // the freeze, and never accumulate boost for time we did not actually slow down (that boost is
        // paid out later as extra elapsedPartialTicks, i.e. free ticks).
        if (java_get_timer_timer_speed(ctx->jvm, timer) == 0.0f) {
            criticals_timer_modified = 0;
            return;
        }
        void *jvm = ctx->jvm;
        java_set_timer_timer_speed(jvm, timer, criticals_timer_speed_value);
        criticals_timer_modified = 1;
        double dt = hook->dt * factor_20_in_criticals;
        double acc = (double)(timer_speed_1_in_criticals - criticals_timer_speed_value) * dt + criticals_accumulated_boost;
        criticals_accumulated_boost = acc;
        if (acc > (double)criticals_boost_value)
            criticals_accumulated_boost = criticals_boost_value;
        return;
    }
    double acc = criticals_accumulated_boost;
    if (acc > threshold_1_in_criticals) {
        float cur = java_get_timer_elapsed_partial_ticks(ctx->jvm, timer);
        void *a = ctx->jvm;
        int whole = (int)acc;
        java_set_timer_elapsed_partial_ticks(a, timer, (float)whole + cur);
        criticals_accumulated_boost -= (double)whole;
    }
    if (criticals_timer_modified == 1) {
        // Only hand the timer back if it still carries our speed; a freeze that took over meanwhile
        // owns the value now and restores it itself.
        if (java_get_timer_timer_speed(ctx->jvm, timer) == criticals_timer_speed_value)
            java_set_timer_timer_speed(ctx->jvm, timer, timer_speed_1_in_criticals);
        criticals_timer_modified = 0;
    }
}

extern "C" void criticals_on_packet_sent(PktHook *hook) {
    void *packet = java_jni_is_instance_of_c03_packet_player(hook->ctx, hook->ev);
    if (packet && java_get_c03_packet_player_moving(hook->ctx, packet) && hook->seq > criticals_last_sequence) {
        double c03_packet_player_b = java_get_c03_packet_player_b(hook->ctx, packet);
        unsigned char c03_packet_player_on_ground = java_get_c03_packet_player_on_ground(hook->ctx, packet);
        if (c03_packet_player_on_ground || criticals_last_y > c03_packet_player_b)
            criticals_falling = c03_packet_player_on_ground ^ 1;
        SHARED_EPILOG();   // keep the join (original does not tail-duplicate it)
        int hold = 0;
        if ((ALOAD(criticals_packet_mode_armed) & 1) && (criticals_falling & 1)) {
            hold = ALOAD(criticals_packet_delay) / 5 + criticals_current_hold_budget;
            criticals_current_hold_budget = hold;
            if (hold <= (int)ALOAD(criticals_packet_delay))
                goto keep;
            hold = ALOAD(criticals_packet_delay);
        }
        criticals_current_hold_budget = hold;
keep:
        criticals_last_y = c03_packet_player_b;
        criticals_last_sequence = hook->seq;
    }
    if (hook->budget < (long long)criticals_current_hold_budget)
        packet_hold(&hook->verdict);
}

extern "C" void criticals_on_toggle(EvHook *hook) {
    if (hook->ev == criticals_module_descriptor && !hook->on) {
        // Leave no hold behind: a stale budget/arm would otherwise swallow outgoing packets from the first
        // packet on after a re-enable, before the next tick has had a chance to decide again.
        criticals_current_hold_budget = 0;
        AXCHG(criticals_packet_mode_armed, (unsigned char)0);
        AXCHG(criticals_timer_mode_armed, (unsigned char)0);
        if (criticals_timer_modified & 1) {
            Ctx *ctx = hook->ctx;
            void *timer = get_timer(ctx->jvm, ctx->minecraft);
            void *jvm = ctx->jvm;
            if (java_get_timer_timer_speed(jvm, timer) == criticals_timer_speed_value)
                java_set_timer_timer_speed(jvm, timer, timer_speed_1_in_criticals_a);
            criticals_timer_modified = 0;
        }
    }


    SHARED_EPILOG();
}
