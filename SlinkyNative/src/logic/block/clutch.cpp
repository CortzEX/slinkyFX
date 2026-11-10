// Clutch - while falling, aims at a block next to / under the player and places on it,
// then snaps the view back.
//   0x488f0 tick: re-click the planned block when the crosshair is on it
//   0x48a30 rotation override flag
//   0x48a50 tick: plan the block, aim at it, snap back afterwards
//   0x49f40 helper: aim point and rotation cost for one candidate block
#include "slinky/engine.h"

typedef double v2d __attribute__((vector_size(16), aligned(16)));
struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct BlockPos { int x, y, z;
#ifdef __linux__
    ~BlockPos() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
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
extern "C" float clutch_clicks_per_second;            // clicks per second
extern "C" float clutch_click_cooldown;            // click cooldown
extern "C" unsigned char clutch_planned;    // planned
extern "C" BlockPos clutch_planned_block;         // planned block
extern "C" unsigned char clutch_planned_face_up;    // planned face is "up"
extern "C" unsigned char clutch_rotating;    // rotating
extern "C" const float minus_one_in_clutch;
extern "C" const float twenty_in_clutch;

FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(BlockPos, java_get_moving_object_position_block_pos, (void *jvm, void *hit));
FN(int, java_get_variant, (void *jvm, void *hit));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(Vec3, java_get_adjacent_block_position, (void *jvm, void *hit));

extern "C" int clutch_ticks_since_last_hurt;              // ticks since last hurt
extern "C" int clutch_ticks_left_jump_snapback;              // ticks left for the jump snapback
extern "C" int clutch_snapback_delay_second;              // snapback delay
extern "C" unsigned char clutch_disable_module_after_snapback;    // disable the module after the snapback
extern "C" int clutch_snapback_ticks_left;              // snapback ticks left
extern "C" Vec3 clutch_aim_point;             // aim point
extern "C" double clutch_jump_wiggle_direction;           // jump wiggle direction
extern "C" char clutch_random_walk_6[], clutch_random_walk_7[], clutch_random_walk_8[], clutch_random_walk_4[], clutch_random_walk_5[], clutch_random_walk_1[],
                clutch_random_walk_2[], clutch_random_walk_3[];   // RNG states
extern "C" char clutch_module_descriptor[];           // Clutch module descriptor
extern "C" float clutch_snapback_duration;            // snapback duration
extern "C" float clutch_snapback_delay;            // snapback delay
extern "C" float clutch_randomisation;            // randomisation
extern "C" float clutch_aim_speed;            // aim speed
extern "C" float clutch_aim_multiplier;            // aim multiplier
extern "C" float clutch_aim_multiplier_strength;            // aim multiplier strength
extern "C" unsigned char clutch_snapback_instantly_on_jump;    // snapback instantly on jump
extern "C" unsigned char clutch_require_air;    // require air
extern "C" unsigned char clutch_require_damage;    // require damage
extern "C" unsigned char clutch_require_moving_backwards;    // require moving backwards
extern "C" float clutch_min_height;            // min height
extern "C" float clutch_range;            // range
extern "C" float clutch_max_aim_distance;            // max aim distance
extern "C" unsigned char clutch_sideways_only;    // sideways only
extern "C" unsigned char clutch_multipoint_aim;    // multipoint aim
extern "C" int clutch_select_blocks_1_switch_2;              // select blocks: 1 switch, 2 switch back
extern "C" unsigned char clutch_disable_after;    // disable after
extern "C" const double clutch_jump_wiggle_sign[2];
extern "C" const double neg_pi_in_clutch, half_turn_degrees_in_clutch, threshold_1_5708, walk_period_0_5_in_clutch, offset_neg_0_99, threshold_20_1601, walk_period_1_in_clutch,
                        offset_0_1_in_clutch, offset_0_98, scale_4_4, threshold_0_99_in_clutch, threshold_89_999, factor_neg_0_25, walk_period_10_in_clutch,
                        modulus_1_in_clutch_a, walk_period_0_5_in_clutch_a, offset_0_1_in_clutch_a, factor_0_25_in_clutch;
extern "C" const float largest_float_for_best_search, random_range_bound_0_05_in_clutch, random_range_bound_neg_0_1_in_clutch, random_range_bound_0_1_in_clutch, one_in_clutch, random_range_bound_neg_20_in_clutch, random_range_bound_20_in_clutch_a,
                       random_range_bound_neg_0_05_in_clutch, random_range_bound_5_in_clutch, random_range_bound_15_in_clutch, random_range_bound_0_05_in_clutch_a, random_range_bound_neg_0_01, random_range_bound_0_01_in_clutch, clamp_bound_0_65;
extern "C" const v2d clutch_top_face_inset_x, clutch_top_face_inset_z;
extern "C" const double threshold_0_5_in_clutch_b, offset_neg_1_in_clutch, one_in_clutch_b;   // 0.5, -1.0, 1.0
extern "C" const v2d clutch_abs_mask;       // abs mask (double)
extern "C" const v2d clutch_abs_mask_second;       // abs mask (float)
extern "C" const v2d clutch_sign_mask;       // sign mask (float)
extern "C" const unsigned short clutch_candidates_within_distance_n[8];   // candidates within distance n
extern "C" const unsigned short clutch_candidate_offsets_4_bits_each[];    // candidate offsets, 4 bits each (+8)

FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(unsigned char, is_silent_rotation_active, ());
FN(void *, get_held_item, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(Rot, get_silent_rotation_offset, ());
FN(Rot, get_render_rotation, ());
FN(Rot, rot_add, (Rot a, Rot b));
FN(Rot, rot_sub, (Rot a, Rot b));
FN(Rot, rot_scale, (Rot rotation, float k));
FN(float, wrap_angle, (float deg));
FN(void, apply_silent_rotation, (Rot rotation));
FN(Range, rot_make_floats, (float lo, float hi));
FN(float, random_rotation, (Range r));
FN(float, walk_update, (void *state, Range r, double d));
FN(float, walk_update_timed, (void *state, Range r, double a, double b));
FN(float, limit_rotation_delta, (float lo, float hi, float v));
FN(float, lerp_unclamped_float, (float a, float b, float t));
FN(void *, java_get_entity_bounding_box, (void *jvm, void *entity));
FN(Box, java_get_axis_aligned_bb_min_x, (void *jvm, void *bounding_box));
FN(void *, java_call_entity_living_base_get_position, (void *jvm, void *entity_living_base));
FN(Vec3, java_get_vec3_x_coord, (void *jvm, void *vec3));
FN(void *, java_get_entity_riding_entity, (void *jvm, void *entity));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(double, libc_hypot, (double x, double y));   // hypot
FN(double, libc_acos, (double x));             // acos
FN(float, libc_sinf, (float x));                // sinf
FN(float, libc_cosf, (float x));                // cosf
FN(double, libc_floor, (double x));              // floor
FN(double, libc_fmod, (double x, double y));    // fmod
FN(void *, java_block_pos_init, (void *jvm, void *object, int y, int z));
FN(void *, java_call_world_get_block_state, (void *jvm, void *world, void *pos, int x, int y, int z));
FN(int, get_block_id, (void *jvm, void *block));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(void *, java_new_vec3_init, (void *jvm, Vec3 v));
FN(void *, java_world_ray_trace_blocks, (void *jvm, void *world, void *from, void *to, unsigned char liquids, unsigned char ignore_no_box, unsigned char last));
FN(Vec3, vec3_make, (double x, double y, double z));
FN(Rot, rot_look_at, (Vec3 from, Vec3 to));
FN(void, track_owner, (Ctx *ctx, void *mod, unsigned char back));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(unsigned char, block_entity_id, (void *jvm, void *entity));
FN(void, queue_module_toggle, (void *module_manager, void *module, unsigned char enabled));
FN(unsigned char, update_rotation_priority_peak, (int ms));
FN(Vec3, java_get_entity_rotation_pitch, (void *jvm, void *entity));
FN(Vec3, vec3_scale, (Vec3 v, double k));
FN(unsigned char, aabb_ray_hit, (Box b, Vec3 origin, Vec3 dir, Vec3 *hit));

static inline __attribute__((always_inline)) int iabs(int v) {
    return v < 0 ? -v : v;
}
static inline __attribute__((always_inline)) double dabs(double x) {
    x = __builtin_fabs(x);
    return x;
}
static inline __attribute__((always_inline)) float fabs_(float x) {
    x = __builtin_fabsf(x);
    return x;
}
static inline __attribute__((always_inline)) float neg(float x) {
    x = -x;
    return x;
}

extern "C" void clutch_tick(Hook *hook) {
    float t = clutch_click_cooldown;
    if (t > 0.0f) {
        t += minus_one_in_clutch;
        clutch_click_cooldown = t;
        if (t > 0.0f)
            return;
    }
    clutch_click_cooldown = twenty_in_clutch / clutch_clicks_per_second + t;
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1 || !clutch_planned)
        return;
    void *hit = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
    if (!hit || java_get_moving_object_position_type_ordinal(ctx->jvm, hit) != 1)
        return;
    BlockPos bp = java_get_moving_object_position_block_pos(ctx->jvm, hit);
    if (bp.x != clutch_planned_block.x || bp.y != clutch_planned_block.y || bp.z != clutch_planned_block.z)
        return;
    if ((java_get_variant(ctx->jvm, hit) != 1) == clutch_planned_face_up)
        return;
    java_increment_key_binding_press_time(ctx->jvm, java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)));
    Vec3 variant = java_get_adjacent_block_position(ctx->jvm, hit);
    int x = (int)variant.x, y = (int)variant.y, z = (int)variant.z;
    asm("" : "+r"(x), "+r"(y), "+r"(z));   // scalar conversions, no SLP (as in the original)
    clutch_planned_block.x = x;
    clutch_planned_block.y = y;
    clutch_planned_block.z = z;
}

extern "C" void clutch_rotation_override_flag(char *h) {
    if (clutch_rotating == 1)
        *(unsigned short *)(h + 8) = 0x101;
}

extern "C" float clutch_pick_aim_target(Vec3 *eye, Rot cur, Vec3 *center, BlockPos *base, BlockPos *rel, Vec3 *out) {
    int rx = rel->x, ry = rel->y, rz = rel->z;
    float j0 = walk_update(clutch_random_walk_1, rot_make_floats(0.0f, random_range_bound_0_05_in_clutch_a), modulus_1_in_clutch_a);
    float rnd = clutch_randomisation;
    float j1 = walk_update(clutch_random_walk_2, rot_make_floats(random_range_bound_neg_0_01, random_range_bound_0_01_in_clutch), walk_period_0_5_in_clutch_a);
    float j2 = walk_update(clutch_random_walk_3, rot_make_floats(random_range_bound_neg_0_01, random_range_bound_0_01_in_clutch), walk_period_0_5_in_clutch_a);
    double lane_x = (double)(j1 * rnd), lane_z = (double)(j2 * rnd);
    if (!clutch_multipoint_aim) {
        // lean towards the side of the block the previous aim point was on
        double fx = dabs(libc_fmod(center->x, modulus_1_in_clutch_a));
        double fz = dabs(libc_fmod(center->z, modulus_1_in_clutch_a));
        fx = threshold_0_5_in_clutch_b < fx ? dabs(fx + offset_neg_1_in_clutch) : fx;
        fz = threshold_0_5_in_clutch_b < fz ? dabs(fz + offset_neg_1_in_clutch) : fz;
        lane_z += fx * (clutch_aim_point.z < center->z ? offset_neg_1_in_clutch : one_in_clutch_b);
        lane_x += fz * (clutch_aim_point.x < center->x ? offset_neg_1_in_clutch : one_in_clutch_b);
    }
    double k = (double)(rnd * j0) + offset_0_1_in_clutch_a;
    double far = modulus_1_in_clutch_a - k;
    double xlo, xhi, zlo, zhi;
    if ((unsigned)iabs(rx) > (unsigned)iabs(rz)) {
        xlo = xhi = (double)((unsigned)rx >> 31);  // the face pointing at us
        zlo = k;
        zhi = far;
    } else {
        xlo = k;
        xhi = far;
        zlo = zhi = (double)((unsigned)rz >> 31);
    }
    int nx = rx + base->x, ny = ry + base->y, nz = rz + base->z;
    double dist = (double)(iabs(rel->x) + iabs(rel->z));
    double ax = limit_rotation_delta((float)(xlo + nx), (float)(xhi + nx), (float)(lane_x + center->x));
    double ay = (double)limit_rotation_delta(0.0f, clamp_bound_0_65, (float)(dist * factor_0_25_in_clutch)) + ((double)ny + factor_0_25_in_clutch);
    double az = limit_rotation_delta((float)(zlo + nz), (float)(zhi + nz), (float)(lane_z + center->z));
    Vec3 aim = vec3_make(ax, ay, az);
    Rot d = rot_sub(rot_look_at(*eye, aim), cur);
    *out = aim;
    return fabs_(wrap_angle(d.yaw)) + fabs_(d.pitch);
}

extern "C" void clutch_on_late_tick(Hook *hook) {
    clutch_planned = 0;
    clutch_rotating = 0;
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1)
        return;
    // event 0x12 dispatches with the world it was called on and the dispatch site does not check the
    // local player; across a world change both can still be missing while the window has focus, and
    // everything below reads them through JNI
    if (!ctx->world || !ctx->player)
        return;
    unsigned char ground = java_get_entity_on_ground(ctx->jvm, ctx->player);
    clutch_ticks_since_last_hurt = java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player) == 0 ? clutch_ticks_since_last_hurt + 1 : 0;
    if (clutch_ticks_left_jump_snapback > 0) {
        clutch_ticks_left_jump_snapback -= 1;
        if (ground && is_silent_rotation_active() && clutch_snapback_instantly_on_jump &&
            java_get_key_binding_key_code(ctx->jvm, get_held_item(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)))) {
            // jumped: wiggle back to the real rotation at once
            Rot cur = rot_add(get_entity_rotation(ctx->jvm, ctx->player), get_silent_rotation_offset());
            Rot srv = get_render_rotation();
            double sgn = clutch_jump_wiggle_sign[0.0 > clutch_jump_wiggle_direction];
            float r1 = (float)((double)random_rotation(rot_make_floats(random_range_bound_5_in_clutch, random_range_bound_15_in_clutch)) * sgn);
            float r2 = (float)((double)random_rotation(rot_make_floats(random_range_bound_5_in_clutch, random_range_bound_15_in_clutch)) * sgn);
            clutch_jump_wiggle_direction += r1;
            Rot d = rot_sub(rot_add(srv, rot_scale(Rot{r1, r2}, clutch_randomisation)), cur);
            d.yaw = wrap_angle(d.yaw);
            apply_silent_rotation(d);
            clutch_rotating = 1;
            clutch_snapback_delay_second = (int)clutch_snapback_duration;
            return;
        }
    }
    Box axis_aligned_bb_min_x = java_get_axis_aligned_bb_min_x(ctx->jvm, java_get_entity_bounding_box(ctx->jvm, ctx->player));
    Vec3 eye = java_get_vec3_x_coord(ctx->jvm, java_call_entity_living_base_get_position(ctx->jvm, ctx->player));
    Rot cur = rot_add(get_entity_rotation(ctx->jvm, ctx->player), get_silent_rotation_offset());
    if (clutch_require_air & ground)
        goto idle;
    if (clutch_ticks_since_last_hurt >= 11 && clutch_require_damage)
        goto idle;
    if (java_get_entity_riding_entity(ctx->jvm, ctx->player))
        goto idle;
    if (clutch_require_moving_backwards == 1) {
        Vec3 motion = java_get_entity_motion_x(ctx->jvm, ctx->player);
        double speed = libc_hypot(motion.x, motion.z);
        if (speed == 0.0)
            goto idle;
        float rotation = (float)((double)get_entity_rotation(ctx->jvm, ctx->player).yaw * neg_pi_in_clutch / half_turn_degrees_in_clutch + neg_pi_in_clutch);
        float sn = libc_sinf(rotation), cs = libc_cosf(rotation);
        // (sn, cs) points backwards, so the dot is positive only while moving backwards
        if (threshold_1_5708 <= libc_acos(((double)sn * motion.x + (double)cs * motion.z) / speed))
            goto idle;                          // not moving backwards
    }
    {
        Vec3 center = {(axis_aligned_bb_min_x.lo.x + axis_aligned_bb_min_x.hi.x) * walk_period_0_5_in_clutch, axis_aligned_bb_min_x.lo.y + offset_neg_0_99, (axis_aligned_bb_min_x.lo.z + axis_aligned_bb_min_x.hi.z) * walk_period_0_5_in_clutch};
        clutch_planned_face_up = 0;
        void *pos = **(int **)ctx->jvm ? java_block_pos_init(ctx->jvm, nullptr, 0, 0) : nullptr;
        BlockPos base = {(int)libc_floor(center.x), (int)libc_floor(center.y), (int)libc_floor(center.z)};
        if (clutch_min_height > 0.0f) {
            int y = base.y;
            for (int k = 1;; k++, y--) {
                if (get_block_id(ctx->jvm, java_call_world_get_block_state(ctx->jvm, ctx->world, pos, base.x, y, base.z)))
                    goto idle;                  // ground too close
                if (!(clutch_min_height > (float)k))
                    break;
            }
        }
        unsigned long long limit = clutch_candidates_within_distance_n[(int)clutch_range];
        long long best_rank = -3;
        float best = largest_float_for_best_search;
        Vec3 best_aim;
        BlockPos best_block;
        Vec3 aim;
        // keep the previous block while it still works
        if (!((double)clutch_planned_block.y > center.y) &&
            get_block_id(ctx->jvm, java_call_world_get_block_state(ctx->jvm, ctx->world, pos, clutch_planned_block.x, clutch_planned_block.y, clutch_planned_block.z))) {
            BlockPos rel = {clutch_planned_block.x - base.x, clutch_planned_block.y - base.y, clutch_planned_block.z - base.z};
            float cost = clutch_pick_aim_target(&eye, cur, &center, &base, &rel, &aim);
            if (!(cost > clutch_max_aim_distance) && threshold_20_1601 > vec3_distance_sq(eye, aim)) {
                void *hit = java_world_ray_trace_blocks(ctx->jvm, ctx->world, java_new_vec3_init(ctx->jvm, eye), java_new_vec3_init(ctx->jvm, aim), 0, 1, 0);
                BlockPos hb = clutch_planned_block;
                if (hit)
                    hb = java_get_moving_object_position_block_pos(ctx->jvm, hit);
                if (hb.x == clutch_planned_block.x && hb.y == clutch_planned_block.y && hb.z == clutch_planned_block.z) {
                    best_aim = aim;
                    best_block = hb;
                    best = largest_float_for_best_search;
                    if (cost != largest_float_for_best_search) {
                        best_rank = iabs(hb.x - base.x) + iabs(hb.y - base.y) + iabs(hb.z - base.z) - 2;
                        best = cost;
                    }
                }
            }
        }
        for (unsigned long long i = 0; i < limit; i++) {
            unsigned off = clutch_candidate_offsets_4_bits_each[i];
            int ox = (off & 0xf) - 8, oy = (off >> 4 & 0xf) - 8, oz = (off >> 8 & 0xf) - 8;
            int x = base.x + ox, y = base.y + oy, z = base.z + oz;
            if (!get_block_id(ctx->jvm, java_call_world_get_block_state(ctx->jvm, ctx->world, pos, x, y, z)))
                continue;
            if (!ox && !oz) {                   // straight below: aim at its top face
                if (clutch_sideways_only)
                    goto idle;
                double inset = (double)(walk_update(clutch_random_walk_1, rot_make_floats(0.0f, random_range_bound_0_05_in_clutch), walk_period_1_in_clutch) * clutch_randomisation) + offset_0_1_in_clutch;
                double jx = walk_update(clutch_random_walk_4, rot_make_floats(random_range_bound_neg_0_1_in_clutch, random_range_bound_0_1_in_clutch), walk_period_1_in_clutch) * clutch_randomisation;
                double jz = walk_update(clutch_random_walk_5, rot_make_floats(random_range_bound_neg_0_1_in_clutch, random_range_bound_0_1_in_clutch), walk_period_1_in_clutch) * clutch_randomisation;
                double ax = limit_rotation_delta((float)((double)x + inset), (float)((double)(x + 1) - inset), (float)(center.x + jx));
                double az = limit_rotation_delta((float)((double)z + inset), (float)((double)(z + 1) - inset), (float)(center.z + jz));
                clutch_aim_point = vec3_make(ax, (double)y + walk_period_1_in_clutch, az);
                clutch_planned_block = BlockPos{x, y, z};
                clutch_planned_face_up = 1;
                goto planned;
            }
            long long rank = iabs(ox) + iabs(oy) + iabs(oz);
            limit = clutch_candidates_within_distance_n[rank];
            BlockPos rel = {ox, oy, oz};
            float cost = clutch_pick_aim_target(&eye, cur, &center, &base, &rel, &aim);
            if (cost > best && best_rank <= rank)
                continue;
            if (!(threshold_20_1601 > vec3_distance_sq(eye, aim)))
                continue;
            best_aim = aim;
            best_block = BlockPos{x, y, z};
            best = cost;
        }
        if (best > clutch_max_aim_distance)
            goto idle;
        clutch_aim_point = best_aim;
        clutch_planned_block = best_block;
    planned:
        if (clutch_select_blocks_1_switch_2 == 1)
            track_owner(ctx, clutch_module_descriptor, 0);
        else if (clutch_select_blocks_1_switch_2 == 2)
            track_owner(ctx, clutch_module_descriptor, 1);
        clutch_planned = block_entity_id(ctx->jvm, item_held_hotbar_slot(ctx->jvm, ctx->player));
        if (!clutch_planned)
            goto countdown;
        float r = random_rotation(rot_make_floats(one_in_clutch, (float)((double)clutch_randomisation * walk_period_0_5_in_clutch + walk_period_1_in_clutch)));
        clutch_snapback_delay_second = (int)(r * clutch_snapback_delay);
        clutch_ticks_left_jump_snapback = 10;
        clutch_disable_module_after_snapback = clutch_disable_after;
        goto act;
    }
idle:
    clutch_planned = 0;
countdown:
    if (clutch_snapback_delay_second > 0) {
        clutch_snapback_delay_second -= 1;
        clutch_snapback_ticks_left = (int)clutch_snapback_duration;
    } else if (clutch_snapback_ticks_left > 0) {
        clutch_snapback_ticks_left -= 1;
        clutch_planned = 0;
    } else {
        if (clutch_disable_module_after_snapback == 1) {
            queue_module_toggle(ctx->module_manager, clutch_module_descriptor, 0);
            clutch_disable_module_after_snapback = 0;
        }
        return;
    }
act:
    if (!update_rotation_priority_peak(1000))
        return;
    clutch_rotating = 1;
    if (clutch_planned) {
        // already looking at the top of the planned block?
        int bx = clutch_planned_block.x, by = clutch_planned_block.y, bz = clutch_planned_block.z;
        Vec3 look = vec3_scale(java_get_entity_rotation_pitch(ctx->jvm, ctx->player), scale_4_4);
        Box top = {{bx + clutch_top_face_inset_x[0], by + clutch_top_face_inset_x[1], bz + clutch_top_face_inset_z[0]},
                   {bx + clutch_top_face_inset_z[1], (double)(by + 1), offset_0_98 + bz}};
        Vec3 hit;
        if (aabb_ray_hit(top, eye, look, &hit) && (double)by + threshold_0_99_in_clutch > hit.y)
            return;
        Rot want = rot_look_at(eye, clutch_aim_point);
        Rot d = rot_sub(want, cur);
        float yaw;
        if ((double)want.pitch > threshold_89_999)
            yaw = walk_update(clutch_random_walk_6, rot_make_floats(random_range_bound_neg_20_in_clutch, random_range_bound_20_in_clutch_a), walk_period_0_5_in_clutch) * clutch_randomisation;
        else
            yaw = wrap_angle(d.yaw);
        float speed = clutch_aim_speed;
        Range jr = rot_make_floats((float)((double)clutch_randomisation * factor_neg_0_25 + walk_period_1_in_clutch), one_in_clutch);
        float ys = walk_update_timed(clutch_random_walk_7, jr, walk_period_10_in_clutch, walk_period_1_in_clutch) * speed;
        jr = rot_make_floats((float)((double)clutch_randomisation * factor_neg_0_25 + walk_period_1_in_clutch), one_in_clutch);
        float ps = speed * walk_update_timed(clutch_random_walk_8, jr, walk_period_10_in_clutch, walk_period_1_in_clutch);
        float cy = limit_rotation_delta(neg(ys), ys, yaw);
        float cp = limit_rotation_delta(neg(ps), ps, d.pitch);
        float m1 = clutch_aim_multiplier + walk_update_timed(clutch_random_walk_7, rot_make_floats(random_range_bound_neg_0_05_in_clutch, random_range_bound_0_05_in_clutch), walk_period_10_in_clutch, walk_period_1_in_clutch);
        float m2 = clutch_aim_multiplier + walk_update_timed(clutch_random_walk_8, rot_make_floats(random_range_bound_neg_0_05_in_clutch, random_range_bound_0_05_in_clutch), walk_period_10_in_clutch, walk_period_1_in_clutch);
        float ry = lerp_unclamped_float(cy, m1 * yaw, clutch_aim_multiplier_strength);
        float rp = lerp_unclamped_float(cp, m2 * d.pitch, clutch_aim_multiplier_strength);
        apply_silent_rotation(Rot{ry, rp});
        return;
    }
    // snap back to the real rotation over the remaining ticks
    if (clutch_snapback_delay_second > 0)
        return;
    int left = clutch_snapback_ticks_left;
    if (left <= 0 || !is_silent_rotation_active())
        return;
    Rot now = rot_add(get_entity_rotation(ctx->jvm, ctx->player), get_silent_rotation_offset());
    Rot d = rot_sub(get_render_rotation(), now);
    d.yaw = wrap_angle(d.yaw);
    apply_silent_rotation(rot_scale(d, one_in_clutch / (float)left));
}

// disabling stops the late tick, so anything left planned would fire on the next enable
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };

extern "C" void clutch_on_toggle(EvHook *hook) {
    if (hook->ev != clutch_module_descriptor)
        return;
    clutch_planned = 0;
    clutch_rotating = 0;
    clutch_disable_module_after_snapback = 0;
    clutch_snapback_delay_second = 0;
    clutch_snapback_ticks_left = 0;
    clutch_ticks_left_jump_snapback = 0;
}
