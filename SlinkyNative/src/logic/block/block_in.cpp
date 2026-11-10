// Block In - surrounds the player with blocks (roof first, then the sides).
//   0x4abf0 (no-op hook)   0x4ad50 rotation override for the placement packet
//   0x4ae50 tick: pick the next free spot, aim at it and place
#include "slinky/engine.h"

typedef float v4f __attribute__((vector_size(16), aligned(16)));
struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
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
struct Pair { float x, y;
#ifdef __linux__
    ~Pair() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Offset3 { signed char dx, dy, dz; };
struct Offset2 { signed char dx, dz; };

struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct RotHook { char padding_00[8]; unsigned char override_yaw, override_pitch; char padding_0a[2]; Rot rot; };   // +0x08 / +0x0c

extern "C" unsigned char block_in_active;    // active
extern "C" int block_in_rotation_mode;              // rotation mode
extern "C" Rot block_in_placement_rotation;              // placement rotation

extern "C" char block_in_module_descriptor[];           // Block In module descriptor
extern "C" void *block_in_placed_count;
extern "C" int block_in_facing_quadrant;              // facing quadrant (0..3)
extern "C" const float divisor_90_in_block_in;     // 90.0
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(float, libc_roundf, (float v));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_back_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_left_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_right_key, (void *jvm, void *game_settings));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, vec3_add_eye_height_sneak, (Vec3 v));
FN(double, libc_floor, (double x));
FN(double, libc_fmod, (double x, double y));
FN(void *, java_block_pos_init, (void *jvm, void *object, int y, int z));
FN(void *, java_call_world_get_block_state, (void *jvm, void *world, void *pos, int x, int y, int z));
FN(int, get_block_id, (void *jvm, void *block));
FN(void *, java_get_entity_bounding_box, (void *jvm, void *entity));
FN(Box, java_get_axis_aligned_bb_min_x, (void *jvm, void *bounding_box));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(Box, aabb_make, (Vec3 lo, Vec3 hi));
FN(unsigned char, aabb_intersects, (Box a, Box b));
FN(Range, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Range r, double d));
FN(unsigned char, update_rotation_priority_peak, (int ms));
FN(void, switch_best_hotbar_slot, (Ctx *ctx, void *mod, unsigned char on, void (*cb)(void *)));
FN(void, key_category, (void *));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(unsigned char, block_entity_id, (void *jvm, void *entity));
FN(Rot, get_silent_rotation_offset, ());
FN(Rot, rot_add, (Rot a, Rot b));
FN(void *, java_call_entity_living_base_get_position, (void *jvm, void *entity_living_base));
FN(Vec3, java_get_vec3_x_coord, (void *jvm, void *bp));
FN(Rot, rot_look_at, (Vec3 from, Vec3 to));
FN(Pair, rot_sub, (Rot a, Rot b));
FN(float, wrap_angle, (float deg));
FN(Pair, rot_scale, (Pair p, float k));
FN(float, limit_rotation_delta, (float lo, float hi, float v));
FN(Pair, rot_lerp, (Pair a, Pair b, float k));
FN(void, apply_silent_rotation, (Pair r));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(void, queue_module_toggle, (void *module_manager, void *module, unsigned char enabled));

extern "C" unsigned char block_in_require_ground;    // require ground
extern "C" float block_in_aim_speed;            // aim speed
extern "C" double block_in_last_y;           // last y
extern "C" char block_in_rng_states[], block_in_rng_states_second[], block_in_rng_states_third[], block_in_rng_states_fourth[], block_in_rng_states_fifth[], block_in_rng_states_in_block_in_d[];   // RNG states
extern "C" const Offset3 block_in_roof_spots_relative_block_below[10];   // roof spots, relative to the block below the feet
extern "C" const Offset2 block_in_side_directions[4];    // side directions
extern "C" const v4f block_in_abs_mask_second;           // abs mask (float)
extern "C" const v4f block_in_abs_mask;           // abs mask (double)
extern "C" const float random_range_bound_neg_0_25, random_range_bound_neg_0_4, random_range_bound_0_25_in_block_in, random_range_bound_0_4_in_block_in, random_range_bound_0_6_in_block_in, random_range_bound_neg_0_1_in_block_in, random_range_bound_neg_0_2,
                       random_range_bound_3_6, random_range_bound_3_8, random_range_bound_1_1_in_block_in, random_range_bound_1_2_in_block_in, random_range_bound_0_8_in_block_in, half_turn_degrees_in_block_in, half_in_block_in, threshold_1_in_block_in;
extern "C" const double offset_neg_1_in_block_in, threshold_0_01_in_block_in, walk_period_0_5_in_block_in, modulus_1_in_block_in, threshold_0_69, threshold_0_31, walk_period_0_2_in_block_in;

extern "C" void block_in_on_tick(void *) {
}

extern "C" void block_in_rotation_override_placement(RotHook *hook) {
    if (block_in_active != 1)
        return;
    *(unsigned short *)&hook->override_yaw = 0x101;
    if (block_in_rotation_mode == 1)
        hook->rot = block_in_placement_rotation;
}

// tick while placing: hold forward only in mode 1, release back / left / right
extern "C" void block_in_tick_while_placing(Hook *hook) {
    if (block_in_rotation_mode != 0) {
        Ctx *ctx = hook->ctx;
        void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
        void *fwd = java_get_game_settings_forward_key(ctx->jvm, obj);
        void *back = java_get_game_settings_back_key(ctx->jvm, obj);
        void *left = java_get_game_settings_left_key(ctx->jvm, obj);
        void *right = java_get_game_settings_right_key(ctx->jvm, obj);
        void *jvm = ctx->jvm;
        asm("" : "+r"(jvm));
        java_set_key_binding_pressed(jvm, fwd, block_in_rotation_mode == 1);
        java_set_key_binding_pressed(ctx->jvm, back, 0);
        java_set_key_binding_pressed(ctx->jvm, left, 0);
        java_set_key_binding_pressed(ctx->jvm, right, 0);
        return;
    }
    EPILOG_NOP();
}

// enable: remember the facing quadrant; disable: restore the movement keys
extern "C" void block_in_enable_remember_facing(EvHook *hook) {
    if (hook->ev != block_in_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    if (hook->on == 1) {
        block_in_placed_count = nullptr;
        int q = (int)libc_roundf(get_entity_rotation(ctx->jvm, ctx->player).yaw / divisor_90_in_block_in);
        block_in_facing_quadrant = q % 4;
        return;
    }
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *fwd = java_get_game_settings_forward_key(ctx->jvm, obj);
    void *back = java_get_game_settings_back_key(ctx->jvm, obj);
    void *left = java_get_game_settings_left_key(ctx->jvm, obj);
    void *right = java_get_game_settings_right_key(ctx->jvm, obj);
    void *jvm = ctx->jvm;
    java_set_key_binding_pressed(jvm, fwd, java_get_key_binding_key_code(jvm, fwd));
    jvm = ctx->jvm;
    java_set_key_binding_pressed(jvm, back, java_get_key_binding_key_code(jvm, back));
    jvm = ctx->jvm;
    java_set_key_binding_pressed(jvm, left, java_get_key_binding_key_code(jvm, left));
    jvm = ctx->jvm;
    java_set_key_binding_pressed(jvm, right, java_get_key_binding_key_code(jvm, right));
}

// air, water and tall grass can be built into
static inline __attribute__((always_inline)) bool open_at(Ctx *ctx, void *pos, int x, int y, int z) {
    unsigned block_id = get_block_id(ctx->jvm, java_call_world_get_block_state(ctx->jvm, ctx->world, pos, x, y, z));
    return block_id <= 0x1f && (0x80000301u >> block_id & 1);
}

static inline __attribute__((always_inline)) double fabs_(double x) {
    x = __builtin_fabs(x);
    return x;
}

extern "C" void block_in_tick(Hook *hook) {
    unsigned long long &placed = *(unsigned long long *)&block_in_placed_count;
    // the 0x10 handler holds forward while a placement plan is active; this tick's movement has already
    // used that, so put the keys back in step with the real keyboard before deciding whether to plan
    // again - a tick that ends without a new plan would otherwise leave forward held down
    unsigned char was_rotating = block_in_rotation_mode != 0;
    block_in_active = 0;
    block_in_rotation_mode = 0;
    Ctx *ctx = hook->ctx;
    if (was_rotating) {
        void *settings = get_game_settings(ctx->jvm, ctx->minecraft);
        void *fwd = java_get_game_settings_forward_key(ctx->jvm, settings);
        void *back = java_get_game_settings_back_key(ctx->jvm, settings);
        void *left = java_get_game_settings_left_key(ctx->jvm, settings);
        void *right = java_get_game_settings_right_key(ctx->jvm, settings);
        java_set_key_binding_pressed(ctx->jvm, fwd, java_get_key_binding_key_code(ctx->jvm, fwd));
        java_set_key_binding_pressed(ctx->jvm, back, java_get_key_binding_key_code(ctx->jvm, back));
        java_set_key_binding_pressed(ctx->jvm, left, java_get_key_binding_key_code(ctx->jvm, left));
        java_set_key_binding_pressed(ctx->jvm, right, java_get_key_binding_key_code(ctx->jvm, right));
    }
    Vec3 me, target;
    void *pos;
    int bx, by, bz;
    unsigned char bit;
    unsigned char ground;
    // event 0x12 dispatches with the world it was called on and the local player is not checked by the
    // dispatch site; across a world change both can still be missing while the window has focus, and the
    // whole body below reads them through JNI. Return without switching the module off: the frame says
    // nothing about the build being finished.
    if (!ctx->world || !ctx->player)
        return;
    if (ctx->in_game != 1)
        goto off;
    ground = java_get_entity_on_ground(ctx->jvm, ctx->player);
    if (block_in_require_ground && !ground)
        goto off;
    me = java_get_entity_pos_x(ctx->jvm, ctx->player);
    if (java_variant(ctx->jvm))
        me = vec3_add_eye_height_sneak(me);
    bx = (int)libc_floor(me.x);
    by = (int)(libc_floor(me.y) + offset_neg_1_in_block_in);
    bz = (int)libc_floor(me.z);
    if (fabs_(me.y - block_in_last_y) > threshold_0_01_in_block_in)
        placed = 0;
    block_in_last_y = me.y;
    pos = **(int **)ctx->jvm ? java_block_pos_init(ctx->jvm, nullptr, 0, 0) : nullptr;

    // roof: the first occupied spot decides where the next block goes
    for (int i = 0; i < 10; i++) {
        const Offset3 &o = block_in_roof_spots_relative_block_below[i];
        if (!(placed >> 32 >> i & 1) && open_at(ctx, pos, bx + o.dx, by + o.dy, bz + o.dz))
            continue;
        if (i == 0)
            break;                              // standing under a roof already: do the sides
        int slot = i >= 5 && i <= 8 ? i - 4 : 0;
        bit = (unsigned char)(slot + 0x20);
        float fx = (float)bx, fy = (float)by, fz = (float)bz;
        Range up = rot_make_floats(random_range_bound_3_6, random_range_bound_3_8);
        switch (i == 9 ? 4 : (i - 1) & 3) {
        case 0:
            target.x = bx;
            target.y = fy + walk_update(block_in_rng_states, up, walk_period_0_5_in_block_in);
            target.z = fz + walk_update(block_in_rng_states_second, rot_make_floats(random_range_bound_neg_0_1_in_block_in, random_range_bound_neg_0_2), walk_period_0_5_in_block_in);
            break;
        case 1:
            target.x = fx + walk_update(block_in_rng_states, rot_make_floats(random_range_bound_1_1_in_block_in, random_range_bound_1_2_in_block_in), walk_period_0_5_in_block_in);
            target.y = fy + walk_update(block_in_rng_states_second, up, walk_period_0_5_in_block_in);
            target.z = bz;
            break;
        case 2:
            target.x = bx + 1;
            target.y = fy + walk_update(block_in_rng_states, up, walk_period_0_5_in_block_in);
            target.z = fz + walk_update(block_in_rng_states_second, rot_make_floats(random_range_bound_1_1_in_block_in, random_range_bound_1_2_in_block_in), walk_period_0_5_in_block_in);
            break;
        case 3:
            target.x = fx + walk_update(block_in_rng_states, rot_make_floats(random_range_bound_neg_0_1_in_block_in, random_range_bound_neg_0_2), walk_period_0_5_in_block_in);
            target.y = fy + walk_update(block_in_rng_states_second, up, walk_period_0_5_in_block_in);
            target.z = bz + 1;
            break;
        default:
            target.x = fx + walk_update(block_in_rng_states, rot_make_floats(random_range_bound_0_4_in_block_in, random_range_bound_0_6_in_block_in), walk_period_0_5_in_block_in);
            target.y = by + 4;
            target.z = fz + walk_update(block_in_rng_states_second, rot_make_floats(random_range_bound_0_4_in_block_in, random_range_bound_0_6_in_block_in), walk_period_0_5_in_block_in);
            break;
        }
        goto place;
    }

    // sides: one block in each direction, at feet, head or above
    for (int d = 0; d < 4; d++) {
        int dir = (block_in_facing_quadrant + d) & 3;
        int dx = block_in_side_directions[dir].dx, dz = block_in_side_directions[dir].dz;
        int nx = bx + dx, nz = bz + dz;
        Box mine = java_get_axis_aligned_bb_min_x(ctx->jvm, java_get_entity_bounding_box(ctx->jvm, ctx->player));
        Vec3 lo = {(double)nx, (double)by, (double)nz};
        Box cell = aabb_make(lo, vec3_add(lo, Vec3{1.0, 3.0, 1.0}));
        if (aabb_intersects(mine, cell))
            continue;
        int base = dir * 3;
        int k;
        Range r;
        if (!(placed >> base & 1) && open_at(ctx, pos, nx, by, nz) && !open_at(ctx, pos, nx, by - 1, nz)) {
            k = 0;
            r = rot_make_floats(random_range_bound_neg_0_25, random_range_bound_neg_0_4);
        } else if (!(placed >> (base + 1) & 1) && open_at(ctx, pos, nx, by + 1, nz) && !open_at(ctx, pos, nx, by, nz)) {
            k = 1;
            r = rot_make_floats(random_range_bound_0_25_in_block_in, random_range_bound_0_4_in_block_in);
        } else if (!(placed >> (base + 2) & 1) && open_at(ctx, pos, nx, by + 2, nz) && !open_at(ctx, pos, nx, by + 1, nz)) {
            k = 2;
            r = rot_make_floats(random_range_bound_0_25_in_block_in, random_range_bound_0_4_in_block_in);
        } else {
            continue;
        }
        target.x = (walk_period_0_5_in_block_in - (double)(walk_update(block_in_rng_states_third, r, walk_period_0_5_in_block_in) * (float)dx)) + nx;
        target.z = (walk_period_0_5_in_block_in - (double)(walk_update(block_in_rng_states_fourth, r, walk_period_0_5_in_block_in) * (float)dz)) + nz;
        target.y = by + k;
        bit = (unsigned char)(base + k);
        goto place;
    }

    // nothing left to place: stop once we stand in the middle of the block
    if (!ground)
        return;
    {
        double fx = fabs_(libc_fmod(me.x, modulus_1_in_block_in));
        double fz = fabs_(libc_fmod(me.z, modulus_1_in_block_in));
        if (threshold_0_69 >= fx && fx >= threshold_0_31 && fz >= threshold_0_31 && threshold_0_69 >= fz)
            goto off;
    }
    return;

place:
    // another module owns the rotation this tick (Clutch asks for 1000): yield the tick instead of switching
    // the module off - the peak resets every tick, so the next one can be ours again
    if (!update_rotation_priority_peak(500))
        return;
    switch_best_hotbar_slot(ctx, block_in_module_descriptor, 1, key_category);
    if (!block_entity_id(ctx->jvm, item_held_hotbar_slot(ctx->jvm, ctx->player)))
        goto off;
    {
        Rot cur = rot_add(get_entity_rotation(ctx->jvm, ctx->player), get_silent_rotation_offset());
        Vec3 eye = java_get_vec3_x_coord(ctx->jvm, java_call_entity_living_base_get_position(ctx->jvm, ctx->player));
        Pair d = rot_sub(rot_look_at(eye, target), cur);
        float dyaw = wrap_angle(d.x);
        float dpitch = d.y;
        Pair step = rot_scale(Pair{dyaw, dpitch}, (float)((double)block_in_aim_speed * walk_period_0_5_in_block_in + walk_period_0_5_in_block_in));
        float j1 = walk_update(block_in_rng_states_fifth, rot_make_floats(random_range_bound_0_8_in_block_in, random_range_bound_1_2_in_block_in), walk_period_0_2_in_block_in);
        float j2 = walk_update(block_in_rng_states_in_block_in_d, rot_make_floats(random_range_bound_0_8_in_block_in, random_range_bound_1_2_in_block_in), walk_period_0_2_in_block_in);
        float my = j1 * half_turn_degrees_in_block_in * block_in_aim_speed;
        float mp = half_turn_degrees_in_block_in * j2 * block_in_aim_speed;
        Pair lim = {limit_rotation_delta(-my, my, dyaw), limit_rotation_delta(-mp, mp, dpitch)};
        Pair rot = rot_lerp(step, lim, half_in_block_in);
        apply_silent_rotation(rot);
        v4f err = {dyaw - rot.x, dpitch - rot.y, 0, 0};
        for (int e = 0; e < 4; e++) err[e] = __builtin_fabsf(err[e]);
        if (threshold_1_in_block_in > err[1] + err[0]) {   // on target: place
            java_increment_key_binding_press_time(ctx->jvm, java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)));
            placed |= 1ull << (bit & 63);
        }
        block_in_active = 1;
        double fx = fabs_(libc_fmod(me.x, modulus_1_in_block_in));
        double fz = libc_fmod(me.z, modulus_1_in_block_in);
        if (threshold_0_69 >= fx && fx >= threshold_0_31 && fabs_(fz) >= threshold_0_31 && threshold_0_69 >= fabs_(fz)) {
            block_in_rotation_mode = 2;
            return;
        }
        block_in_rotation_mode = 1;                        // walk to the middle of the block first
        Vec3 mid = {libc_floor(me.x) + walk_period_0_5_in_block_in, me.y, libc_floor(me.z) + walk_period_0_5_in_block_in};
        block_in_placement_rotation = rot_look_at(me, mid);
    }
    return;

off:
    queue_module_toggle(ctx->module_manager, block_in_module_descriptor, 0);
}
