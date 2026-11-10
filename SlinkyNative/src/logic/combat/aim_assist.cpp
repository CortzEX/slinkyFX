// Aim Assist - picks a target and turns the view towards a randomised point on it.
//   0x2b5b0 disable: drop the cached global ref   0x2b610 input flags
//   0x2b630 tick: choose / keep the target and its aim point
//   0x2b200 frame: turn towards the interpolated aim point
//   0x2bf40 apply one frame of rotation (deadzone, speed, randomisation, mouse gating)
//   0x2c3b0 test one entity, updating the best-so-far scores
#include "slinky/engine.h"
#include "slinky/jids.h"

typedef float v4f __attribute__((vector_size(16), aligned(16)));
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
struct Range { float lo, hi;
#ifdef __linux__
    ~Range() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct FrameEvt { char padding_00[8]; Ctx *ctx; float partial; int padding_14; double dt; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char aim_assist_module_descriptor[];           // Aim Assist module descriptor
extern "C" unsigned char aim_assist_silent_move_correct_value;
extern "C" unsigned char aim_assist_has_target;
extern "C" void *aim_assist_cached_target_ref;            // cached target ref
extern "C" unsigned char aim_assist_aiming;    // aiming
extern "C" Vec3 aim_assist_previous_current_aim_point, aim_assist_previous_current_aim_point_second;    // previous / current aim point
extern "C" float aim_assist_mouse_movement_since_last_tick, aim_assist_mouse_movement_since_last_tick_second;   // mouse movement since the last tick
extern "C" int aim_assist_clicks_wait_before_aiming;              // clicks to wait before aiming
extern "C" unsigned long long aim_assist_tie_break_counter;   // tie-break counter
extern "C" char aim_assist_rng_states[], aim_assist_rng_states_second[], aim_assist_rng_states_third[], aim_assist_rng_states_fourth[], aim_assist_rng_states_fifth[];   // RNG states
extern "C" int aim_assist_mode;              // mode (1 clamp, 2 instant, 3 silent)
extern "C" unsigned char aim_assist_lock_on_vertical;    // lock on vertical
extern "C" unsigned char aim_assist_require_mouse_moved;    // require mouse moved
extern "C" unsigned char aim_assist_silent_ignore_manual_aim;    // silent: ignore manual aim
extern "C" unsigned char aim_assist_require_sprint;    // require sprint
extern "C" unsigned char aim_assist_require_mouse_pressed;    // require mouse pressed
extern "C" unsigned char aim_assist_disable_on_block_break;    // disable on block break
extern "C" unsigned char aim_assist_require_weapon;    // require weapon
extern "C" int aim_assist_mode_target;              // re-pick the target every tick
extern "C" unsigned char aim_assist_ignore_invisible;    // ignore invisible
extern "C" unsigned char aim_assist_allow_exceeding_fov;    // allow exceeding the fov
extern "C" unsigned char aim_assist_require_line_sight;    // require line of sight
extern "C" int aim_assist_sort_by_distance_health_hurt;              // sort by: distance, health, hurt time, fov
extern "C" float aim_assist_deadzone;            // deadzone
extern "C" float aim_assist_fov;            // fov
extern "C" float aim_assist_horizontal_vertical_speed, aim_assist_horizontal_vertical_speed_second;   // horizontal / vertical speed
extern "C" float aim_assist_near_distance;            // near distance
extern "C" float aim_assist_range;            // range
extern "C" float aim_assist_predict;            // predict
extern "C" float aim_assist_horizontal_vertical_hitbox_correction, aim_assist_horizontal_vertical_hitbox_correction_second;   // horizontal / vertical hitbox correction
extern "C" float aim_assist_aim_randomisation;            // aim randomisation
extern "C" const float threshold_10_in_aim_assist, chance_0_5_in_aim_assist, random_range_bound_40_in_aim_assist, random_range_bound_120, factor_5_in_aim_assist, half_in_aim_assist_a,
                       random_range_bound_0_9, random_range_bound_1_1, offset_10000, offset_20000;
extern "C" const double walk_period_5, minus_one, walk_period_1_in_aim_assist, walk_period_60, factor_0_1_in_aim_assist_b, tenth_in_aim_assist,
                        aim_assist_speed_divisor, tenth_in_aim_assist_a, factor_0_5_in_aim_assist, factor_0_9;
extern "C" const double aim_assist_jitter_scale[2];
extern "C" const v4f aim_assist_abs_mask;       // abs mask
extern "C" const v4f aim_assist_sign_mask;       // sign mask

static int aim_assist_hold_left;   // see aim_assist_hold_through_gap

FN(unsigned char, update_rotation_priority_peak, (int ms));
FN(unsigned char, is_silent_rotation_active, ());
FN(Rot, get_render_rotation, ());
FN(Rot, get_silent_rotation_offset, ());
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(Rot, rot_add, (Rot a, Rot b));
FN(Rot, rot_sub, (Rot a, Rot b));
FN(Rot, rot_scale, (Rot rotation, float k));
FN(float, rot_length, (Rot rotation));
FN(float, wrap_angle, (float deg));
FN(void, apply_silent_rotation, (Rot rotation));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(Vec3, java_get_entity_prev_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, vec3_add_eye_height, (Vec3 v));
FN(Vec3, vec3_sub, (Vec3 a, Vec3 b));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(Vec3, vec3_scale, (Vec3 v, double k));
FN(Vec3, vec3_lerp, (Vec3 a, Vec3 b, double t));
FN(Rot, rot_look_at, (Vec3 from, Vec3 to));
FN(Range, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Range r, double d));
FN(float, limit_rotation_delta, (float lo, float hi, float v));
FN(unsigned char, random_chance, (float chance));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *hit));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, java_get_world_player_entities, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(unsigned char, java_get_entity_is_dead, (void *jvm, void *entity));
FN(unsigned char, java_get_entity_player_sleeping, (void *jvm, void *entity_player));
FN(unsigned char, java_call_entity_player_is_invisible_to_player, (void *jvm, void *entity_player, void *viewer));
FN(unsigned char, entity_flags_has, (void *jvm, void *entity, int checks));
FN(Vec3, java_get_entity_other_player_mp_other_player_mpx, (void *jvm, void *entity_other_player_mp));
FN(void *, java_get_entity_bounding_box, (void *jvm, void *entity));
FN(Box, java_get_axis_aligned_bb_min_x, (void *jvm, void *bounding_box));
FN(Box, aabb_expand, (Box b, double grow));
FN(Vec3, aabb_clamp_point, (Box b, Vec3 p));
FN(double, vec3_distance, (Vec3 a, Vec3 b));
FN(double, lerp_double, (double a, double b, double t));
FN(float, noise_1d, (float x));             // noise
FN(float, java_call_entity_living_base_get_health, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(unsigned char, set_vertex_pointer, (void *jvm, void *world, Vec3 from, Vec3 to));
FN(int, java_get_entity_entity_id, (void *jvm, void *entity));

typedef unsigned char (*IsSameObject)(void *env, void *a, void *b);
typedef void *(*NewGlobalRef)(void *env, void *o);

static inline __attribute__((always_inline)) float fabs_(float x) {
    x = __builtin_fabsf(x);
    return x;
}
static inline __attribute__((always_inline)) float neg(float x) {
    x = -x;
    return x;
}
static inline __attribute__((always_inline)) Rot wrap(Rot rotation) {
    rotation.yaw = wrap_angle(rotation.yaw);
    return rotation;
}

extern "C" unsigned char aim_assist_test_entity_updating(Ctx *ctx, void *entity, Vec3 *eye, Rot rot, unsigned char sticky, double *range,
                                     float *best_hp, int *best_hurt, unsigned *best_tie, float *fov,
                                     Vec3 *aim_out, unsigned char *far_out);
extern "C" void aim_assist_apply_frame_rotation(Rot d, float dt);
extern "C" void aim_assist_disable(EvHook *hook) {
    if (hook->ev == aim_assist_module_descriptor && !hook->on) {
        void *ref = aim_assist_cached_target_ref;
        if (ref) {
            JNIEnv *env = ((Jvm *)hook->ctx->jvm)->env;
            (*env)->DeleteGlobalRef(env, ref);
        }
        aim_assist_cached_target_ref = nullptr;
        aim_assist_has_target = 0;
        // The frame handler is unregistered with the module: a stale "aiming" would keep handing the silent
        // rotation back on the next enable, and a stale hold would carry an old click through the first gap.
        aim_assist_aiming = 0;
        aim_assist_hold_left = 0;
    }


    SHARED_EPILOG();
}

extern "C" void aim_assist_on_rotation_update(char *h) {
    if (aim_assist_aiming == 1) {
        h[8] = 1;
        if (aim_assist_silent_move_correct_value == 1)
            h[9] = 1;
    }
}

extern "C" unsigned char aim_assist_test_entity_updating(Ctx *c, void *e, Vec3 *eye, Rot rot, unsigned char sticky, double *range,
                                     float *best_hp, int *best_hurt, unsigned *best_tie, float *fov,
                                     Vec3 *aim_out, unsigned char *far_out) {
    if (java_get_entity_is_dead(c->jvm, e) || java_get_entity_player_sleeping(c->jvm, e))
        return 0;
    if (aim_assist_ignore_invisible == 1 && java_call_entity_player_is_invisible_to_player(c->jvm, e, c->player))
        return 0;
    if (entity_flags_has(c->jvm, e, 7))
        return 0;
    Vec3 pos = java_get_entity_pos_x(c->jvm, e);
    if (aim_assist_predict > 0.0f)
        pos = vec3_lerp(pos, java_get_entity_other_player_mp_other_player_mpx(c->jvm, e), aim_assist_predict);
    Box bb = aabb_expand(java_get_axis_aligned_bb_min_x(c->jvm, java_get_entity_bounding_box(c->jvm, e)), tenth_in_aim_assist);
    Vec3 near = aabb_clamp_point(bb, *eye);
    double dist = vec3_distance(*eye, near);
    unsigned char far = !((double)aim_assist_near_distance > dist);
    if (dist > *range)
        return 0;
    Vec3 base = vec3_add_eye_height(pos);
    double ax, az;
    float hc = aim_assist_horizontal_vertical_hitbox_correction;
    if (hc > 0.0f) {
        if (aim_assist_speed_divisor > dist)
            hc = (float)(hc * (dist / aim_assist_speed_divisor));
        Vec3 p = vec3_lerp(base, near, hc);
        ax = p.x;
        az = p.z;
    } else {
        ax = base.x;
        az = base.z;
    }
    double ay = lerp_double(base.y, near.y, aim_assist_horizontal_vertical_hitbox_correction_second);
    float seed = (float)((eye->x + eye->y + eye->z + ax + ay + az) * tenth_in_aim_assist_a);
    float s1 = offset_10000 + seed, s2 = offset_20000 + seed;
    float n1 = noise_1d(seed), n2 = noise_1d(s1);
    double n3 = (double)noise_1d(s2) * factor_0_5_in_aim_assist;
    float rnd = aim_assist_aim_randomisation;
    if (aim_assist_speed_divisor > dist)
        rnd = (float)(rnd * (dist / aim_assist_speed_divisor * factor_0_9 + tenth_in_aim_assist_a));
    Vec3 jitter = vec3_scale(Vec3{(double)n1 * aim_assist_jitter_scale[0], (double)n2 * aim_assist_jitter_scale[1], n3}, rnd);
    Vec3 aim = vec3_add(Vec3{ax, ay, az}, jitter);
    float off = fabs_(wrap_angle(rot_sub(rot_look_at(*eye, aim), rot).yaw));
    if (off > *fov) {
        if (!sticky)
            return 0;
        far = aim_assist_allow_exceeding_fov;
    }
    float health = java_call_entity_living_base_get_health(c->jvm, e);
    if (health > *best_hp)
        return 0;
    int hurt = java_get_entity_living_base_hurt_time(c->jvm, e);
    if (hurt > *best_hurt)
        return 0;
    if (aim_assist_require_line_sight == 1 && !set_vertex_pointer(c->jvm, c->world, *eye, aim))
        return 0;
    if (!aim_assist_mode_target) {
        *fov = off;
    } else {
        switch (aim_assist_sort_by_distance_health_hurt) {
        case 0:
            *range = dist;
            break;
        case 1:
            *best_hp = health;
            break;
        case 2: {
            unsigned entity_entity_id = java_get_entity_entity_id(c->jvm, e) + (unsigned)aim_assist_tie_break_counter;
            entity_entity_id = (entity_entity_id >> 16 ^ entity_entity_id) * 0x45d9f3b;
            entity_entity_id = (entity_entity_id >> 16 ^ entity_entity_id) * 0x45d9f3b;
            entity_entity_id = entity_entity_id >> 16 ^ entity_entity_id;
            aim_assist_tie_break_counter += 1;
            if (hurt == *best_hurt && entity_entity_id < *best_tie)
                return 0;
            *best_hurt = hurt;
            *best_tie = entity_entity_id;
            break;
        }
        case 3:
            *fov = off;
            break;
        }
    }
    *aim_out = aim;
    *far_out = far;
    return 1;
}

extern "C" void aim_assist_apply_frame_rotation(Rot d, float dt) {
    float y = d.yaw, pitch = d.pitch;
    const float dead = aim_assist_deadzone;
    if (y > 0.0f) {
        y -= dead;
        y = y > 0.0f ? y : 0.0f;
    } else {
        y += dead;
        y = y < 0.0f ? y : 0.0f;
    }
    if (pitch > 0.0f) {
        pitch -= dead;
        pitch = pitch > 0.0f ? pitch : 0.0f;
    } else {
        pitch += dead;
        pitch = pitch < 0.0f ? pitch : 0.0f;
    }
    if (aim_assist_mode == 2) {
        if (!aim_assist_lock_on_vertical)
            pitch = 0.0f;
    } else {
        // pitch follows less while the yaw error is large
        float r = walk_update(aim_assist_rng_states, rot_make_floats(random_range_bound_40_in_aim_assist, random_range_bound_120), walk_period_5);
        float k = (float)(minus_one / (double)r * (double)fabs_(y) + walk_period_1_in_aim_assist);
        pitch *= k > 0.0f ? k : 0.0f;
        float hs = aim_assist_horizontal_vertical_speed, vs = aim_assist_horizontal_vertical_speed_second;
        if (aim_assist_mode == 3) {
            const float m = factor_5_in_aim_assist;
            hs = m > hs ? m : hs;
            vs = m > vs ? m : vs;
        }
        hs *= half_in_aim_assist_a;
        vs *= half_in_aim_assist_a;
        float ry = walk_update(aim_assist_rng_states_third, rot_make_floats(random_range_bound_0_9, random_range_bound_1_1), walk_period_1_in_aim_assist) * (hs * hs);
        float rp = walk_update(aim_assist_rng_states_fourth, rot_make_floats(random_range_bound_0_9, random_range_bound_1_1), walk_period_1_in_aim_assist) * (vs * vs);
        float rs = walk_update(aim_assist_rng_states_fifth, Range{0.95f, 1.05f}, walk_period_60);
        if (aim_assist_mode == 1) {
            float ly = ry * factor_5_in_aim_assist * rs * dt;
            y = limit_rotation_delta(neg(ly), ly, y);
            float lp = rp * factor_5_in_aim_assist * rs * dt;
            pitch = limit_rotation_delta(neg(lp), lp, pitch);
        } else {
            Rot step = rot_scale(rot_scale(Rot{ry * y, rp * pitch}, rs), dt);
            if (!(fabs_(step.yaw) > fabs_(y)))
                y = step.yaw;
            if (!(fabs_(step.pitch) > fabs_(pitch)))
                pitch = step.pitch;
        }
        if (aim_assist_mode != 2) {
            float j = (float)((double)fabs_(y) * factor_0_1_in_aim_assist_b);
            pitch += walk_update(aim_assist_rng_states_second, rot_make_floats(neg(j), j), walk_period_1_in_aim_assist);
        }
    }
    if (aim_assist_require_mouse_moved == 1) {                     // never turn faster than the mouse moved
        float ay = fabs_(y), ap = fabs_(pitch);
        float mx = fabs_(aim_assist_mouse_movement_since_last_tick), my = fabs_(aim_assist_mouse_movement_since_last_tick_second);
        bool cut_y = mx < ay && y != 0.0f;
        float ny = mx / ay * y;
        if (my < ap && pitch != 0.0f)
            pitch *= my / ap;
        aim_assist_mouse_movement_since_last_tick = 0.0f;
        aim_assist_mouse_movement_since_last_tick_second = 0.0f;
        if (cut_y)
            y = ny;
    }
    apply_silent_rotation(Rot{y, pitch});
}

extern "C" void aim_assist_frame(FrameEvt *h) {
    Ctx *ctx = h->ctx;
    if (ctx->in_game != 1 || !ctx->player)
        return;
    if (!aim_assist_has_target) {
        if (aim_assist_aiming != 1)
            return;
        if (!is_silent_rotation_active() || !update_rotation_priority_peak(100)) {
            aim_assist_aiming = 0;
            return;
        }
        Rot cur = rot_add(get_entity_rotation(ctx->jvm, ctx->player), get_silent_rotation_offset());
        Rot d = wrap(rot_sub(get_render_rotation(), cur));  // hand the silent rotation back
        if (threshold_10_in_aim_assist > rot_length(d))
            aim_assist_aiming = 0;
        aim_assist_apply_frame_rotation(d, (float)h->dt);
        return;
    }
    if (!update_rotation_priority_peak(100))
        return;
    if (!aim_assist_aiming && aim_assist_mode == 3)
        aim_assist_aiming = 1;
    if (aim_assist_aiming && !is_silent_rotation_active())
        return;
    float partial = h->partial;
    Vec3 prev = java_get_entity_prev_pos_x(ctx->jvm, ctx->player);
    Vec3 pos = java_get_entity_pos_x(ctx->jvm, ctx->player);
    Vec3 eye = vec3_add(prev, vec3_scale(vec3_sub(pos, prev), partial));
    if (!java_variant(ctx->jvm))
        eye = vec3_add_eye_height(eye);
    Vec3 aim = vec3_lerp(aim_assist_previous_current_aim_point, aim_assist_previous_current_aim_point_second, h->partial);
    Rot want = rot_look_at(eye, aim);
    Rot cur = rot_add(get_entity_rotation(ctx->jvm, ctx->player), get_silent_rotation_offset());
    Rot d = wrap(rot_sub(want, cur));
    if (aim_assist_mode == 3 && !aim_assist_silent_ignore_manual_aim) {
        Rot back = wrap(rot_sub(get_render_rotation(), cur));
        Rot rest = wrap(rot_sub(want, get_render_rotation()));
        if (threshold_10_in_aim_assist > rot_length(back) && rot_length(d) > rot_length(rest)) {
            apply_silent_rotation(back);
            return;
        }
    }
    aim_assist_apply_frame_rotation(d, (float)h->dt);
}

// Ticks of missing click tolerated before the target is dropped. A click from AHK or mouse software is down and
// up inside one frame, and its edge can land in the gap between two ticks; dropping the target on every such gap
// - which also re-rolls the wait below - is what made external clickers track in bursts, while the built-in one
// holds the button and never produces a gap.
static const int aim_assist_hold_gap_ticks = 2;

// 1 when the current target is worth carrying through a tick that saw no click: the hold has not run out, a
// target is cached, and this rotation still has it inside the module's own cone - so a flick away or a target
// that has left the cone ends the hold at once instead of being carried for its full length.
static int aim_assist_hold_through_gap(Ctx *ctx) {
    if (aim_assist_hold_left <= 0 || !aim_assist_cached_target_ref)
        return 0;
    aim_assist_hold_left -= 1;
    Vec3 eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
    if (!java_variant(ctx->jvm))
        eye = vec3_add_eye_height(eye);
    Rot want = rot_look_at(eye, aim_assist_previous_current_aim_point_second);
    Rot cur = is_silent_rotation_active() ? get_render_rotation() : get_entity_rotation(ctx->jvm, ctx->player);
    return rot_length(wrap(rot_sub(want, cur))) <= aim_assist_fov;
}

extern "C" void aim_assist_tick(Hook *hook) {
    *(unsigned long long *)&aim_assist_mouse_movement_since_last_tick = 0;
    Ctx *ctx = hook->ctx;
    JNIEnv *env;
    void *best = nullptr;
    Vec3 eye, aim;
    Rot rot;
    double range;
    unsigned char far = 0;   // written by the test below on every entity it accepts; see the read after the loop
    struct { float fov, hp; int hurt; unsigned tie; } s;
    if (ctx->in_game != 1 || !ctx->player || !ctx->world)
        goto clear;
    if (aim_assist_require_sprint == 1 && aim_assist_mode != 3 && !java_call_entity_is_sprinting(ctx->jvm, ctx->player))
        goto lost;
    {
        unsigned char pressed = vertex_pointer_current(ctx);
        if (pressed)
            aim_assist_hold_left = aim_assist_hold_gap_ticks;
        if (aim_assist_require_mouse_pressed == 1) {
            if (!pressed) {
                if (!aim_assist_hold_through_gap(ctx)) {
                    aim_assist_clicks_wait_before_aiming = random_chance(chance_0_5_in_aim_assist) + 1;
                    goto clear;
                }
                pressed = 1;   // carried through the gap, so the tests below treat it as a press
            }
            if ((unsigned)aim_assist_mode < 2 && aim_assist_clicks_wait_before_aiming) {
                aim_assist_clicks_wait_before_aiming -= 1;
                goto clear;
            }
        }
        if ((pressed & aim_assist_disable_on_block_break) == 1) {
            void *hit = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
            if (hit && java_get_moving_object_position_type_ordinal(ctx->jvm, hit) == 1)
                goto lost;                      // breaking a block
        }
    }
    if (aim_assist_require_weapon == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        goto clear;
    eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
    if (!java_variant(ctx->jvm))
        eye = vec3_add_eye_height(eye);
    rot = is_silent_rotation_active() ? get_render_rotation() : get_entity_rotation(ctx->jvm, ctx->player);
    range = aim_assist_range;
    s.fov = aim_assist_fov;
    *(unsigned *)&s.hp = 0x7f7fffff;            // FLT_MAX
    s.hurt = 0x7fffffff;
    s.tie = 0;
    if (!aim_assist_mode_target && aim_assist_cached_target_ref) {          // keep the current target while it stays valid
        double r = range;
        struct { float fov, hp; int hurt; unsigned tie; } k;
        k.fov = aim_assist_fov;
        *(unsigned *)&k.hp = 0x7f7fffff;
        k.hurt = 0x7fffffff;
        k.tie = 0;
        if (aim_assist_test_entity_updating(ctx, aim_assist_cached_target_ref, &eye, rot, 1, &r, &k.hp, &k.hurt, &k.tie, &k.fov, &aim, &aim_assist_has_target)) {
            aim_assist_previous_current_aim_point = aim_assist_previous_current_aim_point_second;
            aim_assist_previous_current_aim_point_second = aim;
            return;
        }
        env = ((Jvm *)ctx->jvm)->env;
        (*env)->DeleteGlobalRef(env, aim_assist_cached_target_ref);
        aim_assist_cached_target_ref = nullptr;
        aim_assist_has_target = 0;
    }
    {
        void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
        int count = java_call_list_size(ctx->jvm, list);
        for (int i = 0; i < count; i++) {
            void *element = java_call_list_get(ctx->jvm, list, i);
            env = ((Jvm *)ctx->jvm)->env;
            if ((*env)->IsSameObject(env, ctx->player, element))
                continue;
            if (aim_assist_test_entity_updating(ctx, element, &eye, rot, 0, &range, &s.hp, &s.hurt, &s.tie, &s.fov, &aim, &far))
                best = element;
        }
    }
    if (!best)
        goto clear;
    env = ((Jvm *)ctx->jvm)->env;
    if ((*env)->IsSameObject(env, aim_assist_cached_target_ref, best)) {
        aim_assist_previous_current_aim_point = aim_assist_previous_current_aim_point_second;
        aim_assist_previous_current_aim_point_second = aim;
    } else {
        aim_assist_previous_current_aim_point_second = aim;
        aim_assist_previous_current_aim_point = aim;
    }
    aim_assist_has_target = far;
    if (aim_assist_cached_target_ref)
        (*env)->DeleteGlobalRef(env, aim_assist_cached_target_ref);
    // Store the result unconditionally: the old ref was just freed, and keeping it when NewGlobalRef fails
    // leaves a dangling reference the next tick hands straight to JNI.
    aim_assist_cached_target_ref = (*env)->NewGlobalRef(env, best);
    if (!aim_assist_cached_target_ref)
        aim_assist_has_target = 0;
    return;
clear:
    if (aim_assist_cached_target_ref) {
        env = ((Jvm *)ctx->jvm)->env;
        (*env)->DeleteGlobalRef(env, aim_assist_cached_target_ref);
    }
    aim_assist_cached_target_ref = nullptr;
lost:
    aim_assist_hold_left = 0;
    aim_assist_has_target = 0;
}
