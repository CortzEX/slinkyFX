// Bridge Assist - sneaks at block edges while bridging backwards.
//   0x2de50 disable: release the sneak key if we were holding it
//   0x2e5a0 tick:    keep the module's keybind state in sync
//   0x2e260 tick:    hold / release sneak
//   0x2dec0 place:   allow placing only when aimed at the edge below us
//   0x2e7b0 helper:  is the ground under the next position air?
#include "slinky/engine.h"

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
struct Range { float lo, hi;
#ifdef __linux__
    ~Range() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Box { double x0, y0, z0, x1, y1, z1;
#ifdef __linux__
    ~Box() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct PlaceEvt { char padding_00[8]; Ctx *ctx; unsigned char left_click; unsigned char cancel; };   // +0x10 attack flag / +0x11 cancel, as in HEvent
typedef double v2d __attribute__((vector_size(16), aligned(16)));

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char bridge_assist_module_descriptor[];           // Bridge Assist module descriptor
extern "C" int bridge_assist_activation_mode;              // activation mode
extern "C" unsigned char bridge_assist_sneak_held_by_us;    // sneak held by us
extern "C" unsigned char bridge_assist_placing;
extern "C" unsigned char bridge_assist_restrict_placing_edge;    // restrict placing to the edge
extern "C" int bridge_assist_cooldown_after_sneaking_ticks;              // cooldown after sneaking ticks
extern "C" int bridge_assist_ticks_left_hold;              // ticks left to hold
extern "C" float bridge_assist_release_timer;            // release timer
extern "C" char bridge_assist_rng_states[], bridge_assist_rng_states_second[], bridge_assist_rng_states_third[];   // RNG states
extern "C" unsigned char bridge_assist_only_while_sneak_held;    // only while sneak held
extern "C" unsigned char bridge_assist_only_when_looking_down;    // only when looking down
extern "C" unsigned char bridge_assist_only_while_not_rising;    // only while not rising
extern "C" unsigned char bridge_assist_only_few_ticks_after_landing;    // only for a few ticks after landing
extern "C" unsigned char bridge_assist_also_air_while_falling;    // also in the air while falling
extern "C" unsigned char bridge_assist_randomise;    // randomise
extern "C" unsigned char bridge_assist_edge_only_placing;    // edge-only placing
extern "C" float bridge_assist_edge_margin;            // edge margin
extern "C" float bridge_assist_release_delay;            // release delay
extern "C" const double modulus_90, threshold_1_in_bridge_assist, threshold_neg_1_in_bridge_assist, quarter_in_bridge_assist, bridge_assist_edge_margin_clear, walk_period_30_in_bridge_assist, walk_period_60_in_bridge_assist,
                        offset_0_5_in_bridge_assist, offset_neg_0_01, walk_period_5_in_bridge_assist;
extern "C" const float threshold_30_in_bridge_assist, threshold_60, threshold_65, minus_one_in_bridge_assist, random_range_bound_neg_0_5, random_range_bound_0_5_in_bridge_assist,
                       random_range_bound_0_04_in_bridge_assist, random_range_bound_0_08_in_bridge_assist, random_range_bound_3_in_bridge_assist, random_range_bound_0_15_in_bridge_assist, one_in_bridge_assist, minus_one_in_bridge_assist_a;
extern "C" const v2d bridge_assist_abs_mask_second;       // abs mask (double)
extern "C" const v2d bridge_assist_abs_mask;       // abs mask (float)

FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_sneak_key, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(void, track_owner, (void *ctx, void *mod, unsigned char toggle));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *hit));
FN(unsigned, java_get_variant, (void *jvm, void *hit));
FN(unsigned char, java_get_entity_on_ground, (void *jvm, void *entity));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_back_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_left_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_right_key, (void *jvm, void *game_settings));
FN(unsigned char, is_key_down, (void *jvm, void *k));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(unsigned char, block_entity_id, (void *jvm, void *entity));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(double, libc_fmod, (double x, double y));
FN(double, libc_floor, (double x));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, vec3_add_eye_height_sneak, (Vec3 v));
FN(void *, java_get_moving_object_position_hit_vec, (void *jvm, void *hit));
FN(Vec3, java_get_vec3_x_coord, (void *jvm, void *bp));
FN(Vec3, java_get_adjacent_block_position, (void *jvm, void *hit));
FN(float, java_get_entity_living_base_move_forward, (void *jvm, void *entity_living_base));
FN(Vec3, java_get_entity_motion_x, (void *jvm, void *entity));
FN(void *, java_get_entity_bounding_box, (void *jvm, void *entity));
FN(Box, java_get_axis_aligned_bb_min_x, (void *jvm, void *bounding_box));
FN(Vec3, vec3_make, (double x, double y, double z));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(float, java_get_entity_living_base_land_movement_factor, (void *jvm, void *entity_living_base));
FN(Vec3, horizontal_motion, (Rot rotation, float fwd, float strafe, float speed));
FN(void *, java_call_variant, (void *jvm, void *world, int x, int y, int z));
FN(int, get_block_id, (void *jvm, void *block));
FN(Range, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Range r, double d));
FN(unsigned char, walk_should_change, (void *state, Range r, double d));
FN(float, random_rotation, (Range r));

static inline __attribute__((always_inline)) double dabs(double x) {
    x = __builtin_fabs(x);
    return x;
}
static inline __attribute__((always_inline)) float fabs_(float x) {
    x = __builtin_fabsf(x);
    return x;
}

extern "C" void bridge_assist_disable(EvHook *hook) {
    if (hook->ev == bridge_assist_module_descriptor && !hook->on && (bridge_assist_sneak_held_by_us & 1)) {
        Ctx *ctx = hook->ctx;
        void *k = java_get_game_settings_sneak_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft));
        java_set_key_binding_pressed(ctx->jvm, k, java_get_key_binding_key_code(ctx->jvm, k));
        bridge_assist_sneak_held_by_us = 0;
    }

}

extern "C" void bridge_assist_on_late_tick(Hook *hook) {
    if (bridge_assist_placing == 1) {
        Ctx *ctx = hook->ctx;
        asm("" : "+r"(ctx));

        // track_owner switches the hotbar through the player inventory, and the 0x12 dispatch site does not
        // check the local player - across a world change it can be gone while the flag is still set
        if (!ctx->player)
            return;
        track_owner(ctx, bridge_assist_module_descriptor, bridge_assist_activation_mode == 2);
        return;
    }
    EPILOG_NOP();
}

extern "C" unsigned char bridge_assist_edge_check(void *jvm, void *game_settings, void *world, void *player) {
    Box axis_aligned_bb_min_x = java_get_axis_aligned_bb_min_x(jvm, java_get_entity_bounding_box(jvm, player));
    Vec3 base = vec3_make((axis_aligned_bb_min_x.x0 + axis_aligned_bb_min_x.x1) * offset_0_5_in_bridge_assist, axis_aligned_bb_min_x.y0 + offset_neg_0_01, (axis_aligned_bb_min_x.z0 + axis_aligned_bb_min_x.z1) * offset_0_5_in_bridge_assist);
    Vec3 motion = java_get_entity_motion_x(jvm, player);
    double m = bridge_assist_edge_margin;
    if (bridge_assist_randomise == 1) {
        m -= walk_update(bridge_assist_rng_states, rot_make_floats(0.0f, random_range_bound_0_15_in_bridge_assist), walk_period_5_in_bridge_assist);
        if (0.0 > m)
            m = 0.0;
    }
    void *game_settings_key_bindings = java_get_game_settings_forward_key(jvm, game_settings), *key_binding = java_get_game_settings_back_key(jvm, game_settings), *k2 = java_get_game_settings_left_key(jvm, game_settings), *k3 = java_get_game_settings_right_key(jvm, game_settings);
    float fwd = is_key_down(jvm, game_settings_key_bindings) ? one_in_bridge_assist : 0.0f;
    if (is_key_down(jvm, key_binding))
        fwd += minus_one_in_bridge_assist_a;
    float str = is_key_down(jvm, k2) ? one_in_bridge_assist : 0.0f;
    if (is_key_down(jvm, k3))
        str += minus_one_in_bridge_assist_a;
    Rot rotation = get_entity_rotation(jvm, player);
    Vec3 dir = horizontal_motion(rotation, fwd, str, java_get_entity_living_base_land_movement_factor(jvm, player));
    Vec3 p = vec3_add(base, vec3_add(motion, dir));
    int x0 = (int)libc_floor(p.x - m);
    int z0 = (int)libc_floor(p.z - m);
    int y = (int)libc_floor(p.y);
    double x1d = libc_floor(p.x + m);
    double z1d = libc_floor(m + p.z);
    if (get_block_id(jvm, java_call_variant(jvm, world, x0, y, z0)))
        return 0;
    int z1 = (int)z1d;
    if (z0 != z1 && get_block_id(jvm, java_call_variant(jvm, world, x0, y, z1)))
        return 0;
    int x1 = (int)x1d;
    if (x0 == x1)
        return 1;
    if (get_block_id(jvm, java_call_variant(jvm, world, x1, y, z0)))
        return 0;
    if (z0 == z1)
        return 1;
    return !get_block_id(jvm, java_call_variant(jvm, world, x1, y, z1));
}

extern "C" void bridge_assist_tick(Hook *hook) {
    bridge_assist_restrict_placing_edge = 0;
    bridge_assist_placing = 0;
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1)
        return;
    // event 0x11 dispatches with the world it was called on and the dispatch site does not check the
    // local player; across a world change both can still be missing while the window has focus, and the
    // sneak/edge logic below reads them through JNI
    if (!ctx->world || !ctx->player)
        return;
    void *game_settings = get_game_settings(ctx->jvm, ctx->minecraft);
    if (bridge_assist_cooldown_after_sneaking_ticks > 0)
        bridge_assist_cooldown_after_sneaking_ticks -= 1;
    void *key = java_get_game_settings_sneak_key(ctx->jvm, game_settings);
    unsigned char held = java_get_key_binding_key_code(ctx->jvm, key);
    if (bridge_assist_only_while_sneak_held != held)
        goto release;
    if (bridge_assist_only_when_looking_down == 1 && threshold_65 > get_entity_rotation(ctx->jvm, ctx->player).pitch)
        goto release;
    if (bridge_assist_only_while_not_rising == 1 && java_get_entity_living_base_move_forward(ctx->jvm, ctx->player) > 0.0f)
        goto release;
    bridge_assist_placing = bridge_assist_activation_mode != 0;
    if (bridge_assist_only_few_ticks_after_landing == 1) {
        int n;
        if (block_entity_id(ctx->jvm, item_held_hotbar_slot(ctx->jvm, ctx->player))) {
            n = 2;
        } else {
            n = bridge_assist_ticks_left_hold;
            if (n <= 0)
                goto release;
            n -= 1;
        }
        bridge_assist_ticks_left_hold = n;
    }
    bridge_assist_sneak_held_by_us = 1;
    if (is_key_down(ctx->jvm, key)) {
        if (bridge_assist_only_while_sneak_held == 1) {
            if (java_get_entity_living_base_move_forward(ctx->jvm, ctx->player) >= 0.0f || bridge_assist_cooldown_after_sneaking_ticks <= 0)
                goto keep;
            goto check;
        }
        if (java_get_entity_on_ground(ctx->jvm, ctx->player))
            goto check;
        if (bridge_assist_also_air_while_falling == 1 && !(0.0 > java_get_entity_motion_x(ctx->jvm, ctx->player).y))
            goto check;
        goto timer;
    }
    if (!java_get_entity_on_ground(ctx->jvm, ctx->player)) {
        if (bridge_assist_also_air_while_falling != 1 || 0.0 > java_get_entity_motion_x(ctx->jvm, ctx->player).y)
            return;
    }
    if (!bridge_assist_edge_check(ctx->jvm, game_settings, ctx->world, ctx->player))
        return;
    java_set_key_binding_pressed(ctx->jvm, key, 1);
    goto keep;
check:
    if (bridge_assist_edge_check(ctx->jvm, game_settings, ctx->world, ctx->player))
        goto keep;
timer:
    bridge_assist_release_timer += minus_one_in_bridge_assist;
    if (bridge_assist_release_timer > 0.0f)
        goto keep;
    java_set_key_binding_pressed(ctx->jvm, key, 0);
    {
        float d = bridge_assist_release_delay;
        if (bridge_assist_randomise == 1) {
            d += walk_update(bridge_assist_rng_states_second, rot_make_floats(random_range_bound_neg_0_5, random_range_bound_0_5_in_bridge_assist), walk_period_30_in_bridge_assist);
            if (walk_should_change(bridge_assist_rng_states_third, rot_make_floats(random_range_bound_0_04_in_bridge_assist, random_range_bound_0_08_in_bridge_assist), walk_period_60_in_bridge_assist))
                d += random_rotation(rot_make_floats(0.0f, random_range_bound_3_in_bridge_assist));
        }
        bridge_assist_release_timer = d + bridge_assist_release_timer;
    }
    return;
keep:
    bridge_assist_restrict_placing_edge = bridge_assist_edge_only_placing;
    return;
release:
    if (bridge_assist_sneak_held_by_us == 1)
        java_set_key_binding_pressed(ctx->jvm, key, held);
    bridge_assist_sneak_held_by_us = 0;
}

extern "C" void bridge_assist_place(PlaceEvt *h) {
    if (h->left_click || bridge_assist_cooldown_after_sneaking_ticks == 0x14)
        return;
    Ctx *ctx = h->ctx;
    // the click dispatch sites build the context without checking the local player, and both the edge
    // test below and the held-item check at the end of the handler read the entity through JNI
    if (!ctx->player)
        return;
    if (bridge_assist_restrict_placing_edge == 1) {
        void *hit = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (hit && java_get_moving_object_position_type_ordinal(ctx->jvm, hit) == 1 && java_get_variant(ctx->jvm, hit) >= 2 && java_get_entity_on_ground(ctx->jvm, ctx->player)) {
            void *game_settings = get_game_settings(ctx->jvm, ctx->minecraft);
            void *back = java_get_game_settings_back_key(ctx->jvm, game_settings), *left = java_get_game_settings_left_key(ctx->jvm, game_settings), *right = java_get_game_settings_right_key(ctx->jvm, game_settings);
            if (is_key_down(ctx->jvm, back) && !is_key_down(ctx->jvm, left) && !is_key_down(ctx->jvm, right)) {
                float yaw = fabs_((float)libc_fmod(get_entity_rotation(ctx->jvm, ctx->player).yaw, modulus_90));
                if (!(threshold_30_in_bridge_assist > yaw) && !(yaw > threshold_60)) {
                    Vec3 pos = java_get_entity_pos_x(ctx->jvm, ctx->player);
                    if (java_variant(ctx->jvm))
                        pos = vec3_add_eye_height_sneak(pos);
                    Vec3 vec3_x_coord = java_get_vec3_x_coord(ctx->jvm, java_get_moving_object_position_hit_vec(ctx->jvm, hit));
                    double dy = vec3_x_coord.y - pos.y;
                    if (!(dabs(vec3_x_coord.x - pos.x) > threshold_1_in_bridge_assist) && !(threshold_neg_1_in_bridge_assist > dy) && !(dy > 0.0) &&
                        !(dabs(vec3_x_coord.z - pos.z) > threshold_1_in_bridge_assist)) {
                        Vec3 variant = java_get_adjacent_block_position(ctx->jvm, hit);
                        const double one = threshold_1_in_bridge_assist;
                        bool in_z = variant.z < pos.z && pos.z < variant.z + one;
                        bool in_x = variant.x < pos.x && pos.x < variant.x + one;
                        double fx = dabs(libc_fmod(vec3_x_coord.x, one));
                        double fz = libc_fmod(vec3_x_coord.z, one);
                        double edge = in_x && in_z ? quarter_in_bridge_assist : bridge_assist_edge_margin_clear;
                        double far = one - edge;
                        bool ok = !(far < fx) && !(fx < edge);
                        if (!ok) {
                            fz = dabs(fz);
                            ok = !(far < fz) && !(fz < edge);
                        }
                        if (ok) {
                            // the click is the placement we want: leave the event's cancel byte at 0 so
                            // it reaches the game (the engine skips rightClickMouse when cancel is set)
                            return;
                        }
                    }
                }
            }
        }
    }
    if (block_entity_id(ctx->jvm, item_held_hotbar_slot(ctx->jvm, ctx->player)))
        bridge_assist_cooldown_after_sneaking_ticks = 0x14;
}
