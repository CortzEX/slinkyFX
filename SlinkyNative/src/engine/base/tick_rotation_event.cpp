// 0x6000: per-frame tick: rotation-delta tracking and the timed "tick" event (type 1)
#include "slinky/engine.h"
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
extern "C" double tick_rotation_last_time;
extern "C" Rot tick_rotation_previous;
extern "C" const double threshold_0_002;
extern "C" char java_natives_world_module_manager[];
FN(void, update_frame_time, ());
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(Rot, rot_sub, (Rot a, Rot b));
FN(void, add_rotation_delta, (Rot rotation));
FN(float, get_partial_ticks, (void *jvm, void *object));
FN(void, set_rotation_active, (void *jvm, void *player, bool on));
struct TickEvent { long long type; Ctx *ctx; float pt; int z; double dt; char rest[0x28]; };
FN(void, dispatch_event, (void *mgr, TickEvent *e));
extern "C" void tick_modules(Ctx *ctx, void *object, double now) {
    if (tick_rotation_last_time == 0.0) tick_rotation_last_time = now;
    update_frame_time();
    if (!ctx->world || !ctx->player) {
        tick_rotation_last_time = now;
        return;
    }
    Rot d = rot_sub(get_entity_rotation(ctx->jvm, ctx->player), tick_rotation_previous);
    unsigned long long d_bits;
    __builtin_memcpy(&d_bits, &d, sizeof d_bits);   // bit_cast needs a trivial type, and Rot is not one on Linux
    if (d_bits & 0x7fffffff7fffffffull) add_rotation_delta(d);
    double dt = now - tick_rotation_last_time;
    if (threshold_0_002 > dt) return;
    float partial_ticks = get_partial_ticks(ctx->jvm, object);
    TickEvent e = {1, ctx, partial_ticks, 0, dt, {}};
    dispatch_event(java_natives_world_module_manager, &e);
    set_rotation_active(ctx->jvm, ctx->player, false);
    tick_rotation_last_time = now;
    tick_rotation_previous = get_entity_rotation(ctx->jvm, ctx->player);
}
