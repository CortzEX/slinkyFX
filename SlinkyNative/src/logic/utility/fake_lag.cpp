// Fake Lag - descriptors +0x48 (tick) / +0x68 (inbound) / +0x78 (outbound) / +0x98 (toggle).
// +0x48: manage the lag windows: per-direction counters ("inbound"/"outbound"
//        ms budgets), and decide whether lagging is currently allowed.
// +0x68/+0x78 (one handler, event type 2 = inbound, 3 = outbound): keep the packet
//        unopinionated while the window is open, otherwise spend the budget and hold
//        (verdict 1) once it runs out; protected inbound packets pass straight
//        through (verdict 3) when "realtime_damage" is on.
// +0x98: drop the budgets/counters on disable - they belong to a lag cycle that no longer exists.
#include "slinky/engine.h"
#define LIKELY(c) __builtin_expect(!!(c), 1)
#define H4(c) __builtin_expect(!!(c), 0)

extern "C" char fake_lag_module_descriptor[];           // Fake Lag module descriptor
extern "C" float fake_lag_inbound_value;            // "inbound"
extern "C" float fake_lag_outbound_value;            // "outbound"
extern "C" bool fake_lag_realtime_damage_value;             // "realtime_damage"
extern "C" unsigned fake_lag_mode;         // "mode"
extern "C" unsigned char fake_lag_require_ingame_value;    // "require_ingame"
extern "C" unsigned char fake_lag_require_weapon_value;    // "require_weapon"
extern "C" unsigned char fake_lag_require_attack_value;    // "require_attack"
extern "C" int fake_lag_last_hurt_time;              // last hurt time
extern "C" int fake_lag_cooldown;              // cooldown
extern "C" int fake_lag_inbound_counter;              // inbound counter
extern "C" int fake_lag_outbound_counter;              // outbound counter
extern "C" unsigned char fake_lag_realtime_damage_flag;
extern "C" unsigned char fake_lag_inbound_window_open;             // inbound window open
extern "C" unsigned char fake_lag_outbound_window_open;             // outbound window open
extern "C" volatile unsigned short fake_lag_inbound_budget;   // inbound budget
extern "C" volatile unsigned short fake_lag_outbound_budget;   // outbound budget

struct PktHook { int type; char padding_04[4]; Ctx *ctx; void *ev; long long budget; char padding_20[4]; int verdict; };  // +0x00/+0x08/+0x10/+0x18/+0x24
struct ToggleHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, java_jni_is_instance_of_s19_packet_entity_status, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s06_packet_update_health, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s29_packet_sound_effect, (void *jvm, void *event));

extern "C" void fake_lag_on_tick(Hook *hook) {
    if (fake_lag_cooldown > 0)
        fake_lag_cooldown--;
    __atomic_exchange_n(&fake_lag_realtime_damage_flag, fake_lag_realtime_damage_value, __ATOMIC_SEQ_CST);
    if (fake_lag_mode == 1) {
        int n = fake_lag_inbound_counter;
        bool open;
        if (n > 0) {
            fake_lag_inbound_counter = n - 50;

            open = (unsigned)n < 51;
        } else {
            fake_lag_inbound_counter = (int)((float)n + fake_lag_inbound_value);

            open = false;
        }

        __atomic_exchange_n(&fake_lag_inbound_window_open, open, __ATOMIC_SEQ_CST);
        n = fake_lag_outbound_counter;
        if (n > 0) {
            fake_lag_outbound_counter = n - 50;

            open = (unsigned)n < 51;
        } else {
            fake_lag_outbound_counter = (int)((float)n + fake_lag_outbound_value);

            open = false;
        }

        __atomic_exchange_n(&fake_lag_outbound_window_open, open, __ATOMIC_SEQ_CST);
    }
    Ctx *ctx = hook->ctx;
    unsigned char ok;
    int hurt;
    if (!ctx->world || !ctx->player) goto off;
    if (fake_lag_require_ingame_value == 1 && !ctx->in_game) goto off;
    hurt = java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
    if (fake_lag_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player)) {
        fake_lag_cooldown = 0;
        ok = false;
    } else {
        bool busy = fake_lag_cooldown != 0;
        unsigned char ra = fake_lag_require_attack_value;
        ok = (ra ^ 1) | busy;

    }
    if (hurt != fake_lag_last_hurt_time)
        fake_lag_cooldown = 20;
    fake_lag_last_hurt_time = hurt;
    if (ok) {
        __atomic_exchange_n(&fake_lag_inbound_budget, (unsigned short)(int)fake_lag_inbound_value, __ATOMIC_SEQ_CST);
        __atomic_exchange_n(&fake_lag_outbound_budget, (unsigned short)(int)fake_lag_outbound_value, __ATOMIC_SEQ_CST);
        return;
    }
    goto clear;
off:
    fake_lag_cooldown = 0;
clear:
    __atomic_exchange_n(&fake_lag_outbound_budget, (unsigned short)0, __ATOMIC_SEQ_CST);
    __atomic_exchange_n(&fake_lag_inbound_budget, (unsigned short)0, __ATOMIC_SEQ_CST);
}

extern "C" void fake_lag_on_packet_sent(PktHook *hook) {
    unsigned short v;
    if (hook->type == 2) {
        if (LIKELY(__atomic_load_n(&fake_lag_realtime_damage_flag, __ATOMIC_SEQ_CST) & 1)) {
            if (java_jni_is_instance_of_s19_packet_entity_status(hook->ctx, hook->ev) || java_jni_is_instance_of_s06_packet_update_health(hook->ctx, hook->ev) || java_jni_is_instance_of_s29_packet_sound_effect(hook->ctx, hook->ev)) {
                packet_flush(&hook->verdict);
                return;
            }
        }
        if (__atomic_load_n(&fake_lag_inbound_window_open, __ATOMIC_SEQ_CST) & 1)
            return;
        v = fake_lag_inbound_budget;
        asm volatile("");

    } else {
        if (__atomic_load_n(&fake_lag_outbound_window_open, __ATOMIC_SEQ_CST) & 1)
            return;
        v = fake_lag_outbound_budget;
        asm volatile("");

    }
    // Age is read, never rewritten: this field is shared by every later packet hook (criticals,
    // lag_range, backtrack, knockback_delay all compare it against their own max_delay), so subtracting
    // fake_lag's window from it silently extended their holds by that amount.
    if (hook->budget - (long long)v < 0)
        packet_hold(&hook->verdict);
}

extern "C" void fake_lag_on_toggle(ToggleHook *hook) {
    if (hook->ev != fake_lag_module_descriptor || hook->on != 0)
        return;
    __atomic_exchange_n(&fake_lag_inbound_budget, (unsigned short)0, __ATOMIC_SEQ_CST);
    __atomic_exchange_n(&fake_lag_outbound_budget, (unsigned short)0, __ATOMIC_SEQ_CST);
    __atomic_exchange_n(&fake_lag_inbound_window_open, (unsigned char)0, __ATOMIC_SEQ_CST);
    __atomic_exchange_n(&fake_lag_outbound_window_open, (unsigned char)0, __ATOMIC_SEQ_CST);
    fake_lag_inbound_counter = 0;
    fake_lag_outbound_counter = 0;
    fake_lag_cooldown = 0;
    // 0 = "not hurt": matches the resting value, so re-enabling does not look like a fresh hit
    fake_lag_last_hurt_time = 0;
}
