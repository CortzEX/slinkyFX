// Timer - descriptor +0x58 / +0x98: set the game timer speed.
// The first hook applies "speed" when every enabled condition holds, else 1.0.
#include "slinky/engine.h"

extern "C" float timer_speed_value;             // "speed" current value
extern "C" unsigned char timer_require_mouse_value;     // "require_mouse"
extern "C" unsigned char timer_require_falling_value;     // "require_falling"
extern "C" unsigned char timer_require_damage_value;     // "require_damage"
extern "C" char timer_module_descriptor[];            // owner tag checked by hook +0x98

struct V3 { double x, y, z;
#ifdef __linux__
    ~V3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct TimerState { char padding_00[8]; Ctx *ctx; void *tag; bool flag; };
FN(void *, get_timer, (void *jvm, void *timer));
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(V3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(void, java_set_timer_timer_speed, (void *jvm, void *timer, float speed));

extern "C" void timer_on_frame(TimerState *s) {
    Ctx *ctx = s->ctx;
    void *timer = get_timer(ctx->jvm, ctx->minecraft);
    float speed;
    void *jvm;
    if (timer_require_mouse_value == 1 && !vertex_pointer_current(ctx))
        goto normal;
    if (timer_require_falling_value == 1) {
        if (java_get_entity_on_ground(ctx->jvm, ctx->player))
            goto normal;
        V3 v = java_get_entity_motion_x(ctx->jvm, ctx->player);
        if (v.y >= 0.0)
            goto normal;
    }
    if (timer_require_damage_value == 1) {
        if (ctx->in_game != 1)
            goto normal;
        if (java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) == 0)
            goto normal;
    }
    jvm = ctx->jvm;

    speed = timer_speed_value;
    goto apply;
normal:
    jvm = ctx->jvm;

    speed = 1.0f;
apply:
    java_set_timer_timer_speed(jvm, timer, speed);

}

extern "C" void timer_on_toggle(TimerState *s) {
    if (s->tag != (void *)timer_module_descriptor || s->flag != 0)
        return;
    Ctx *ctx = s->ctx;
    java_set_timer_timer_speed(ctx->jvm, get_timer(ctx->jvm, ctx->minecraft), 1.0f);
}
