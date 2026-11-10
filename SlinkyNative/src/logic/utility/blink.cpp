// Blink - descriptors +0x48 / +0x68 / +0x78 / +0x98 / +0x128.
// +0x48:  per tick: auto-disable when "max_duration" runs out, or on attack
//         (own hurt-time rising) if "disable_on_attack".
// +0x68: inbound hook: protect status/health/sound (3 = send now), hold everything
//        else (1), except S2_packet_particles, which is also sent now (3).
// +0x78: force the packet verdict to 1 while blinking.
// +0x98:  on enable: load the duration, reset the counters, publish "direction".
// +0x128: disable when the player digs / interacts with a block.
#include "slinky/engine.h"

extern "C" char blink_module_descriptor[];                 // Blink module descriptor
extern "C" unsigned char blink_disable_on_attack_value;          // "disable_on_attack"
extern "C" float blink_max_duration_value;                  // "max_duration"
extern "C" unsigned char blink_direction;          // "direction"
extern "C" unsigned char blink_disable_on_block_dig_value;          // "disable_on_block_dig"
extern "C" unsigned char blink_disable_on_block_interact_value;          // "disable_on_block_interact"
extern "C" int blink_last_hurt_time;                    // last hurt time
extern "C" int blink_ticks_left;                    // ticks left
extern "C" volatile unsigned char blink_active_direction;          // blinking / direction state

struct PktHook { char padding_00[8]; Ctx *ctx; void *ev; char padding_18[12]; int verdict; };   // +0x24
struct PhaseHook { char padding_00[8]; Ctx *ctx; unsigned char phase; };                 // +0x10
struct StateHook { char padding_00[16]; void *ev; unsigned char on; };                   // +0x10 / +0x18

FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(void, queue_module_toggle, (void *module_manager, void *module, int enabled));
FN(void *, java_jni_is_instance_of_s19_packet_entity_status, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s06_packet_update_health, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s29_packet_sound_effect, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s2_apacket_particles, (void *jvm, void *event));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));

extern "C" void blink_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1 || !ctx->player)
        return;
    int hurt = java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
    unsigned char rose = hurt > blink_last_hurt_time;
            // keep the compare materialised (setg)
    unsigned char attack = blink_disable_on_attack_value;

    blink_last_hurt_time = hurt;
    if (--blink_ticks_left <= 0 || (rose & attack))
        queue_module_toggle(ctx->module_manager, blink_module_descriptor, 0);
}

extern "C" void blink_on_packet_received(PktHook *hook) {
    if (blink_active_direction != 1) {
        if (java_jni_is_instance_of_s19_packet_entity_status(hook->ctx, hook->ev) || java_jni_is_instance_of_s06_packet_update_health(hook->ctx, hook->ev) || java_jni_is_instance_of_s29_packet_sound_effect(hook->ctx, hook->ev))
            packet_flush(&hook->verdict);
        else if (java_jni_is_instance_of_s2_apacket_particles(hook->ctx, hook->ev))
            packet_flush(&hook->verdict);
        else
            packet_hold(&hook->verdict);
    }
}

extern "C" void blink_on_packet_sent(PktHook *hook) {
    if (blink_active_direction)
        packet_hold(&hook->verdict);
}

extern "C" void blink_on_toggle(StateHook *hook) {
    if (hook->ev != blink_module_descriptor || hook->on != 1)
        return;
    blink_ticks_left = (int)(long long)blink_max_duration_value;
    blink_last_hurt_time = 0x7fffffff;
    __atomic_exchange_n(&blink_active_direction, blink_direction, __ATOMIC_SEQ_CST);
}

extern "C" void blink_on_attack(PhaseHook *hook) {
    Ctx *ctx = hook->ctx;
    if (hook->phase == 1 ? blink_disable_on_block_dig_value == 1 : blink_disable_on_block_interact_value == 1) {
        void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (minecraft && java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) == 1) {
            queue_module_toggle(ctx->module_manager, blink_module_descriptor, 0);
            return;
        }
    }
    EPILOG_NOP();
}
