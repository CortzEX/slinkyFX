// Strafe - descriptors +0x198 (remember ground state) / +0x1a8 (steer motion).
// +0x1a8 pulls the horizontal motion towards the direction the movement keys
// ask for, by "strength_air" / "strength_jump" / "strength_ground".
#include "slinky/engine.h"

typedef float __m128 __attribute__((vector_size(16), aligned(16)));
typedef int __m128i_ __attribute__((vector_size(16), aligned(16)));
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
struct MoveHook { char padding_00[8]; Ctx *ctx; float strafe; float forward; float speed; unsigned char sprint; };  // +0x10/+0x14/+0x1c

extern "C" float strafe_max_hurt_time_value;           // "max_hurt_time"
extern "C" float strafe_strength_air_value;           // "strength_air"
extern "C" float strafe_strength_jump_value;           // "strength_jump"
extern "C" float strafe_strength_ground_value;           // "strength_ground"
extern "C" unsigned char strafe_require_weapon_value;   // "require_weapon"
extern "C" unsigned char strafe_was_on_ground_last_tick;   // was on ground last tick
extern "C" const double fifth_in_strafe;
extern "C" const double minus_one_in_strafe;
extern "C" const double one_in_strafe;
extern "C" const double minus_right_angle_degrees_in_strafe;
extern "C" const double half_turn_degrees_in_strafe;
extern "C" const double quarter_right_angle_degrees;
extern "C" const double strafe_minus_quarter_right_angle;
extern "C" const double pi_in_strafe;
extern "C" const __m128 strafe_sign_mask;      // sign mask

FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(double, libc_hypot, (double x, double z));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(float, libc_sinf, (float v));
FN(float, libc_cosf, (float v));
FN(double, lerp_double, (double a, double b, double c));
FN(void, java_set_entity_motion_x, (void *jvm, void *entity, Vec3 value));

extern "C" void strafe_on_movement_input(Hook *hook) {
    Ctx *ctx = hook->ctx;
    strafe_was_on_ground_last_tick = java_call_entity_is_sprinting(ctx->jvm, ctx->player);
}

extern "C" void strafe_on_move_flying(MoveHook *hook) {
    if (hook->forward == 0.0f && hook->strafe == 0.0f)
        return;
    Ctx *ctx = hook->ctx;
    if (strafe_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        return;
    if ((float)java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) > strafe_max_hurt_time_value)
        return;
    const float *strength;
    unsigned char boost;
    if (java_get_entity_on_ground(ctx->jvm, ctx->player)) {
        if (hook->sprint == 1) {
            boost = strafe_was_on_ground_last_tick ^ 1;

            strength = &strafe_strength_jump_value;
        } else {
            boost = 0;
            strength = &strafe_strength_ground_value;
        }
    } else {
        boost = 0;
        strength = &strafe_strength_air_value;
    }
    float str = *(volatile const float *)strength;
    if (str == 0.0f)
        return;
    Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
    double d = libc_hypot(motion.x, motion.z);
    if (boost)
        d += fifth_in_strafe;
    float yawbits = get_entity_rotation(ctx->jvm, ctx->player).yaw;
    double a = 0.0;
    if (0.0f > hook->strafe)
        a = minus_one_in_strafe;
    if (hook->strafe > 0.0f)
        a = one_in_strafe;
    double y = yawbits;
    if (hook->forward > 0.0f) {
        a *= strafe_minus_quarter_right_angle;
    } else if (0.0f > hook->forward) {
        y += half_turn_degrees_in_strafe;
        a *= quarter_right_angle_degrees;
    } else {
        a *= minus_right_angle_degrees_in_strafe;
    }
    a += y;
    a *= pi_in_strafe;
    a /= half_turn_degrees_in_strafe;
    float ang = (float)a;
    double vx = motion.x;
    __m128 t = {libc_sinf(ang), 0, 0, 0};
    __m128 nm = (__m128)((__m128i_)t ^ (__m128i_)strafe_sign_mask);
    float nc = nm[0];
    motion.x = lerp_double(vx, (double)nc * d, str);
    double vz = motion.z;
    motion.z = lerp_double(vz, (double)libc_cosf(ang) * d, str);
    java_set_entity_motion_x(ctx->jvm, ctx->player, motion);
}
