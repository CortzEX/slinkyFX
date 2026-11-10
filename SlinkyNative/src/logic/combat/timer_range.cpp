// Timer Range - freezes the game timer, then catches up with a burst of ticks that
// carries us into hit range of the target.
//   0x43950 packet hook, 0x43970 disable, 0x43bf0 start a timer burst
//   0x42c60 tick: count the burst down, decide whether a burst is possible
//   0x42f00 render tick: release the freeze; simulate ticks ahead to find one that reaches the target
#include "slinky/engine.h"

typedef int v4i __attribute__((vector_size(16), aligned(16)));
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
struct Box { Vec3 lo, hi;
#ifdef __linux__
    ~Box() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Target { char padding_00[8]; void *ent; double dist2; };
struct FrameEvt { char padding_00[8]; Ctx *ctx; float partial; int padding_14; double dt; };
struct PktHook { char padding_00[0x24]; int verdict; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char timer_range_module_descriptor[];           // Timer Range module descriptor
extern "C" int timer_range_mode;              // mode
extern "C" unsigned char timer_range_block_while_charging;    // block while charging
extern "C" int timer_range_delay_ticks;
extern "C" unsigned char timer_range_timer_modified;    // timer modified
extern "C" unsigned char timer_range_holding_packets;    // holding packets
extern "C" float timer_speed_1_in_timer_range_a;           // normal timer speed
extern "C" unsigned char timer_range_burst_would_reach_target;    // a burst would reach the target
extern "C" float timer_range_last_forward_input, timer_range_last_strafe_input;   // last forward / strafe input
extern "C" double timer_range_cooldown;           // cooldown
extern "C" double timer_range_freeze_time_left;           // freeze time left
extern "C" unsigned char timer_range_require_mouse_pressed;    // require mouse pressed
extern "C" unsigned char timer_range_require_weapon;    // require weapon
extern "C" unsigned char timer_range_require_not_using_item;    // require not using item
extern "C" float timer_range_max_ticks, timer_range_max_ticks_second;   // max ticks (mode 0 / mode 1)
extern "C" float timer_range_min_range;            // min range
extern "C" float timer_range_cooldown_after_burst;            // cooldown after a burst
extern "C" const double threshold_3, factor_0_0174533, neg_pi_in_timer_range, offset_0_1_in_timer_range, offset_0_4, offset_1_9,
                        scale_3_in_timer_range, factor_0_05_in_timer_range, scale_0_546_in_timer_range, scale_1_in_timer_range;
extern "C" const float timer_speed_1_in_timer_range;
extern "C" const v4i timer_range_sign_mask;       // sign mask

FN(void *, get_timer, (void *jvm, void *timer));
FN(float, java_get_timer_timer_speed, (void *jvm, void *timer));
FN(void, java_set_timer_timer_speed, (void *jvm, void *timer, float speed));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(unsigned char, item_sword, (void *jvm, void *item));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *hit));
FN(void, java_set_minecraft_left_click_counter, (void *jvm, void *minecraft, int n));
FN(void *, java_get_game_settings_attack_key, (void *jvm, void *game_settings));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(void *, get_held_item, (void *jvm, void *game_settings));
FN(unsigned char, is_key_down, (void *jvm, void *k));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, vec3_add_eye_height, (Vec3 v));
FN(Vec3, java_get_entity_other_player_mp_other_player_mpx, (void *jvm, void *entity_other_player_mp));
FN(unsigned char, set_vertex_pointer, (void *jvm, void *world, Vec3 from, Vec3 to));
FN(void, timer_range_read_movement_axes, (void *jvm, void *game_settings));
FN(void, java_set_timer_elapsed_partial_ticks, (void *jvm, void *timer, float ticks));
FN(int, java_get_entity_other_player_mp_other_player_mppos_rotation_increments, (void *jvm, void *entity_other_player_mp));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(float, libc_cosf, (float x));     // cosf
FN(float, libc_sinf, (float x));     // sinf
FN(Vec3, vec3_make, (double x, double y, double z));
FN(Vec3, vec3_sub, (Vec3 a, Vec3 b));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(Vec3, vec3_scale, (Vec3 v, double k));
FN(Vec3, vec3_reciprocal, (Vec3 v));
FN(unsigned char, aabb_ray_slab_test, (Box b, Vec3 origin, Vec3 dir));
FN(Vec3, aabb_clamp_point, (Box b, Vec3 p));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(float, java_get_entity_living_base_land_movement_factor, (void *jvm, void *entity_living_base));
FN(Vec3, horizontal_motion, (Rot rotation, float fwd, float strafe, float speed));
FN(double, libc_hypot, (double x, double y));   // hypot

extern "C" void timer_range_hypot(Ctx *ctx);

static inline __attribute__((always_inline)) float neg(float x) {
    x = -x;
    return x;
}

// Owned by us only while "block while charging" holds the key down: the held slot can change during a
// timer freeze, so the release is keyed on this instead of on re-testing the item.
static unsigned char timer_range_use_key_forced;

extern "C" void timer_range_on_packet_sent(PktHook *hook) {
    if (__atomic_load_n(&timer_range_holding_packets, __ATOMIC_SEQ_CST) & 1)
        packet_hold(&hook->verdict);
}

extern "C" void timer_range_on_timer_phase(EvHook *hook) {
    if (hook->ev == timer_range_module_descriptor && !hook->on) {
        Ctx *ctx = hook->ctx;
        void *timer = get_timer(ctx->jvm, ctx->minecraft);
        void *jvm = ctx->jvm;
        // Only unfreeze if the frozen value is still ours: Criticals timer mode may have taken the
        // timer over after our last frame, and restoring 1.0 would cancel its slowdown.
        if (java_get_timer_timer_speed(jvm, timer) == 0.0f)
            java_set_timer_timer_speed(jvm, timer, timer_speed_1_in_timer_range_a);
        // The freeze-expiry release in the frame handler is only reached while the timer is still modified,
        // so a disable mid-freeze has to undo our own use-key press here.
        if (timer_range_use_key_forced == 1) {
            java_set_key_binding_pressed(jvm, java_get_game_settings_use_item_key(jvm, get_game_settings(jvm, ctx->minecraft)), 0);
            timer_range_use_key_forced = 0;
        }
        timer_range_timer_modified = 0;
        timer_range_delay_ticks = 0;
        __atomic_exchange_n(&timer_range_holding_packets, (unsigned char)0, __ATOMIC_SEQ_CST);
    }


    SHARED_EPILOG();
}

extern "C" void timer_range_hypot(Ctx *c) {
    // The tick handler can re-freeze the timer while the delay counter is still running, and that branch has
    // no in-game test -- after a disconnect item_held_hotbar_slot() would read a field off a null entity.
    if (!c->player)
        return;
    void *timer = get_timer(c->jvm, c->minecraft);
    void *jvm = c->jvm;
    java_set_timer_timer_speed(jvm, timer, 0.0f);
    timer_range_timer_modified = 1;
    if (timer_range_mode == 1) {
        __atomic_exchange_n(&timer_range_holding_packets, (unsigned char)1, __ATOMIC_SEQ_CST);
    } else if (timer_range_block_while_charging == 1) {
        jvm = c->jvm;
        if (item_sword(jvm, item_held_hotbar_slot(jvm, c->player))) {
            void *k = java_get_game_settings_use_item_key(c->jvm, get_game_settings(c->jvm, c->minecraft));
            java_set_key_binding_pressed(c->jvm, k, 1);
            java_increment_key_binding_press_time(c->jvm, k);
            timer_range_use_key_forced = 1;
            return;
        }
    }
    EPILOG_NOP();
}

extern "C" void timer_range_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (timer_range_delay_ticks > 0) {
        void *game_settings = get_game_settings(ctx->jvm, ctx->minecraft);
        int n = --timer_range_delay_ticks;
        if (!(n | timer_range_mode))
            timer_range_hypot(ctx);
        if (ctx->in_game != 1 || !((Target *)ctx->target)->ent)
            return;
        void *hit = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (!hit || java_get_moving_object_position_type_ordinal(ctx->jvm, hit) != 2)
            return;
        java_set_minecraft_left_click_counter(ctx->jvm, ctx->minecraft, 0);
        java_increment_key_binding_press_time(ctx->jvm, java_get_game_settings_attack_key(ctx->jvm, game_settings));       // attack
        return;
    }
    timer_range_burst_would_reach_target = 0;
    if (ctx->in_game != 1 || !ctx->player)
        return;
    Target *target = (Target *)ctx->target;
    if (!target->ent || threshold_3 > target->dist2)
        return;
    if (!java_get_entity_on_ground(ctx->jvm, ctx->player) || java_get_entity_living_base_hurt_time(ctx->jvm, target->ent) > 1)
        return;
    if (timer_range_require_mouse_pressed == 1 && !vertex_pointer_current(ctx))
        return;
    if (timer_range_require_weapon == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        return;
    if (timer_range_require_not_using_item == 1 && java_call_entity_player_is_using_item(ctx->jvm, ctx->player))
        return;
    if (java_get_entity_motion_x(ctx->jvm, ctx->player).y > 0.0)
        return;
    void *net_handler = get_game_settings(ctx->jvm, ctx->minecraft);
    if (is_key_down(ctx->jvm, get_held_item(ctx->jvm, net_handler)))
        return;
    Vec3 position = java_get_entity_pos_x(ctx->jvm, ctx->player);
    if (!java_variant(ctx->jvm))
        position = vec3_add_eye_height(position);
    Vec3 tp = java_get_entity_other_player_mp_other_player_mpx(ctx->jvm, target->ent);
    if (!java_variant(ctx->jvm))
        tp = vec3_add_eye_height(tp);
    if (!set_vertex_pointer(ctx->jvm, ctx->world, position, tp))
        return;
    timer_range_read_movement_axes(ctx->jvm, net_handler);
    timer_range_burst_would_reach_target = timer_range_last_forward_input > 0.0f;
}

extern "C" void timer_range_render_tick(FrameEvt *h) {
    Ctx *ctx = h->ctx;
    void *timer = get_timer(ctx->jvm, ctx->minecraft);
    if (timer_range_timer_modified == 1) {
        timer_range_freeze_time_left -= h->dt;
        if (timer_range_freeze_time_left > 0.0) {
            java_set_timer_timer_speed(ctx->jvm, timer, 0.0f);
            return;
        }
        // Release only the freeze we still own (see the disable path).
        if (java_get_timer_timer_speed(ctx->jvm, timer) == 0.0f)
            java_set_timer_timer_speed(ctx->jvm, timer, timer_speed_1_in_timer_range);
        timer_range_timer_modified = 0;
        __atomic_exchange_n(&timer_range_holding_packets, (unsigned char)0, __ATOMIC_SEQ_CST);
        // Released on ownership rather than on what is held now: switching hotbar slots during the freeze used
        // to leave the use key pressed for good, and item_held_hotbar_slot() read a null player on logout.
        if (timer_range_use_key_forced == 1) {
            java_set_key_binding_pressed(ctx->jvm, java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)), 0);
            timer_range_use_key_forced = 0;
        }
        if (timer_range_mode == 1)
            java_set_timer_elapsed_partial_ticks(ctx->jvm, timer, (float)timer_range_delay_ticks);
    }
    if (timer_range_cooldown > 0.0) {
        timer_range_cooldown -= h->dt;
        return;
    }
    if (timer_range_burst_would_reach_target != 1 || ctx->in_game != 1 || !ctx->player)
        return;
    Target *target = (Target *)ctx->target;
    if (!target->ent)
        return;
    Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
    Vec3 position = java_get_entity_pos_x(ctx->jvm, ctx->player);
    if (!java_variant(ctx->jvm))
        position = vec3_add_eye_height(position);
    Vec3 tpos = java_get_entity_pos_x(ctx->jvm, target->ent);
    Vec3 tserver = java_get_entity_other_player_mp_other_player_mpx(ctx->jvm, target->ent);
    int steps = java_get_entity_other_player_mp_other_player_mppos_rotation_increments(ctx->jvm, target->ent);         // interpolation steps left
    Rot rot = get_entity_rotation(ctx->jvm, ctx->player);
    float ry = (float)(neg_pi_in_timer_range - (double)rot.yaw * factor_0_0174533);
    float cy = libc_cosf(ry), sy = libc_sinf(ry);
    float rp = (float)((double)neg(rot.pitch) * factor_0_0174533);
    float cp = neg(libc_cosf(rp)), sp = libc_sinf(rp);
    Vec3 look = {(double)(sy * cp), (double)sp, (double)(cy * cp)};
    int max = (int)(timer_range_mode == 0 ? timer_range_max_ticks : timer_range_max_ticks_second);
    for (int i = 1; i <= max; i++) {
        Box box;
        box.lo = vec3_sub(tpos, vec3_make(offset_0_4, offset_0_1_in_timer_range, offset_0_4));
        box.hi = vec3_add(tpos, vec3_make(offset_0_4, offset_1_9, offset_0_4));
        Vec3 dir = vec3_reciprocal(vec3_scale(look, scale_3_in_timer_range));
        if (aabb_ray_slab_test(box, position, dir)) {
            double d2 = vec3_distance_sq(position, aabb_clamp_point(box, position));
            if (d2 > 0.0) {
                if ((double)(timer_range_min_range * timer_range_min_range) > d2)
                    return;                     // already too close
                if (timer_range_mode == 1)
                    timer_range_hypot(ctx);
                else
                    java_set_timer_elapsed_partial_ticks(ctx->jvm, timer, (float)i);
                timer_range_delay_ticks = i;
                timer_range_freeze_time_left = (double)((float)i - h->partial) * factor_0_05_in_timer_range;
                timer_range_cooldown = timer_range_cooldown_after_burst;
                return;
            }
        }
        // advance one tick: our movement ...
        Vec3 dv = horizontal_motion(rot, timer_range_last_forward_input, timer_range_last_strafe_input, java_get_entity_living_base_land_movement_factor(ctx->jvm, ctx->player));
        motion = vec3_add(motion, dv);
        position = vec3_add(position, motion);
        motion = vec3_scale(motion, scale_0_546_in_timer_range);
        if (timer_range_mode == 1) {
            double speed = libc_hypot(motion.x, motion.z);
            Vec3 rel = vec3_sub(position, tserver);
            Vec3 step = vec3_scale(rel, speed / libc_hypot(rel.x, rel.z));
            step.y = 0.0;
            tserver = vec3_add(tserver, step);
        }
        // ... and the target's interpolation towards its server position
        if (steps > 0) {
            Vec3 d = vec3_sub(tserver, tpos);
            tpos = vec3_add(tpos, vec3_scale(d, scale_1_in_timer_range / (double)steps));
            steps -= 1;
        }
    }
}
