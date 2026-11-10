// Fast Place - repeats the use key while it is held, at a fixed rate.
//   0x2fbc0 tick: after the activation delay, click whenever the timer allows and the
//           placement would not land inside the player
#include "slinky/engine.h"

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
extern "C" float fast_place_click_timer;            // click timer
extern "C" unsigned char fast_place_use_key_held_last_tick;    // use key held last tick
extern "C" int fast_place_activation_delay_left;              // activation delay left
extern "C" char fast_place_random_walk_1[];           // RNG state
extern "C" float fast_place_max_cps;            // clicks per second
extern "C" float fast_place_activation_delay;            // activation delay
extern "C" const float minus_one_in_fast_place, random_range_bound_0_02, random_range_bound_0_08_in_fast_place;
extern "C" const double walk_period_20, offset_1_in_fast_place;

FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(int, java_get_key_binding_press_time, (void *jvm, void *key_binding));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(unsigned char, block_entity_id, (void *jvm, void *entity));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *hit));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(Vec3, java_get_adjacent_block_position, (void *jvm, void *hit));
FN(Vec3, vec3_make, (double x, double y, double z));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(Box, aabb_make, (Vec3 lo, Vec3 hi));
FN(void *, java_get_entity_bounding_box, (void *jvm, void *entity));
FN(Box, java_get_axis_aligned_bb_min_x, (void *jvm, void *bounding_box));
FN(unsigned char, aabb_intersects, (Box a, Box b));
FN(Range, rot_make_floats, (float lo, float hi));
FN(unsigned char, walk_should_change, (void *state, Range r, double d));

extern "C" void fast_place_tick(Hook *hook) {
    float t = fast_place_click_timer;
    if (t > 0.0f) {
        t += minus_one_in_fast_place;
        fast_place_click_timer = t;
        if (t > 0.0f)
            return;
    }
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1)
        return;
    // the mouse-over dispatch site leaves ctx->player NULL once the world is unloaded, even while the
    // window still has focus, and the held-item check below reads it through JNI
    if (!ctx->player)
        return;
    void *key = java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft));
    unsigned char was_held = fast_place_use_key_held_last_tick;
    unsigned char held = java_get_key_binding_key_code(ctx->jvm, key);
    fast_place_use_key_held_last_tick = held;
    if (!held) {
        fast_place_activation_delay_left = (int)fast_place_activation_delay;
        return;
    }
    if (fast_place_activation_delay_left > 0) {
        fast_place_activation_delay_left -= 1;
        return;
    }
    if (java_get_key_binding_press_time(ctx->jvm, key) > 0)
        return;
    t = fast_place_click_timer;
    if (!(0.0f >= t))
        return;
    bool first = !was_held;                     // the first press clicks by itself
    for (;;) {
        t = (float)((double)t + walk_period_20 / (double)fast_place_max_cps);
        fast_place_click_timer = t;
        if (!first && block_entity_id(ctx->jvm, item_held_hotbar_slot(ctx->jvm, ctx->player))) {
            bool click;
            void *hit = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
            if (hit && java_get_moving_object_position_type_ordinal(ctx->jvm, hit) == 1) {
                void *stack = item_held_hotbar_slot(ctx->jvm, ctx->player);
                if (stack && java_call_static_item_get_id_from_item(ctx->jvm, stack) == 0x41) {
                    click = true;               // ladders always
                } else {
                    // only when the new block would not overlap the player
                    Vec3 variant = java_get_adjacent_block_position(ctx->jvm, hit);
                    Box cell = aabb_make(variant, vec3_add(variant, vec3_make(offset_1_in_fast_place, offset_1_in_fast_place, offset_1_in_fast_place)));
                    Box axis_aligned_bb_min_x = java_get_axis_aligned_bb_min_x(ctx->jvm, java_get_entity_bounding_box(ctx->jvm, ctx->player));
                    click = !aabb_intersects(cell, axis_aligned_bb_min_x) ||
                            walk_should_change(fast_place_random_walk_1, rot_make_floats(random_range_bound_0_02, random_range_bound_0_08_in_fast_place), walk_period_20);
                }
            } else {
                click = walk_should_change(fast_place_random_walk_1, rot_make_floats(random_range_bound_0_02, random_range_bound_0_08_in_fast_place), walk_period_20);
            }
            if (click)
                java_increment_key_binding_press_time(ctx->jvm, key);
            t = fast_place_click_timer;
        }
        first = false;
        if (!(0.0f >= t))
            return;
    }
}
