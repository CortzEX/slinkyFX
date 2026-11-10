// Fast Accel - descriptor +0x1a8: while moving on the ground, rescale the
// horizontal motion towards the speed the current input could reach after 20
// ticks of acceleration (capped by "acceleration").
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct MoveHook { char padding_00[8]; Ctx *ctx; float strafe; float forward; float speed; unsigned char sprint; };  // +0x10/+0x14/+0x18/+0x1c

extern "C" float fast_accel_acceleration_value;           // "acceleration"
extern "C" unsigned char fast_accel_require_weapon_value;   // "require_weapon"
extern "C" unsigned char fast_accel_require_not_damaged_value;   // "require_not_damaged"
extern "C" const float threshold_1_in_fast_accel;    // minimum input length
extern "C" const double ground_friction_times_air_drag;   // per-tick friction factor
extern "C" const double fifth_in_fast_accel;   // sprint bonus

FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(double, libc_hypot, (double x, double z));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(float, libc_hypotf_alt, (float x, float y));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(double, lerp_double, (double a, double b, double c));
FN(void, java_set_entity_motion_x, (void *jvm, void *entity, Vec3 value));

extern "C" void fast_accel_on_move_flying(MoveHook *hook) {
    if (hook->forward == 0.0f && hook->strafe == 0.0f)
        return;
    Ctx *ctx = hook->ctx;
    if (fast_accel_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        return;
    if (fast_accel_require_not_damaged_value == 1 && java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) > 0)
        return;
    Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
    double d = libc_hypot(motion.x, motion.z);
    if (d == 0.0)
        return;
    if (java_get_entity_on_ground(ctx->jvm, ctx->player)) {
        float strafe = hook->strafe, forward = hook->forward, speed = hook->speed;
        float len = libc_hypotf_alt(strafe, forward);
        float scale = speed / (len > threshold_1_in_fast_accel ? len : threshold_1_in_fast_accel);
        strafe *= scale;
        scale *= forward;
        double a = libc_hypotf_alt(strafe, scale);
        double t = 0.0;
        t += a;
        for (int i = 0; i < 19; i++) {
            t *= ground_friction_times_air_drag;
            t += a;
        }
        if (hook->sprint == 1 && java_call_entity_is_sprinting(ctx->jvm, ctx->player))
            t += fifth_in_fast_accel;
        if (d > t)
            return;
        double k = lerp_double(d, t, fast_accel_acceleration_value);
        motion.x = motion.x / d * k;
        asm volatile("");   // keep the two components scalar (no SLP vectorisation), as in the original
        motion.z = motion.z / d * k;
        java_set_entity_motion_x(ctx->jvm, ctx->player, motion);
    }
}
