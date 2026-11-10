// Lag Range - attack only while the target's hurt time is inside the configured range.
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct RenderHook { char padding_00[8]; Vec3 cam; char padding_20[8]; float partial; };   // +0x08 / +0x28
struct Target { void *padding_00; void *ent; double dist2; };
struct PktHook { char padding_00[8]; void *ctx; void *ev; long long budget; int seq; int verdict; };
struct Range { float lo, hi;
#ifdef __linux__
    ~Range() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct PhaseHook { char padding_00[8]; Ctx *ctx; unsigned char phase; };
extern "C" unsigned char lag_range_hit_time_unchanged;
extern "C" float lag_range_line_width_setting;            // "line_width"
extern "C" float lag_range_color_setting[3];         // "color"
extern "C" unsigned char lag_range_filled_setting;    // "filled"
extern "C" unsigned char lag_range_have_target_position;    // have a target position
extern "C" Vec3 lag_range_target_position;             // target position (previous tick)
extern "C" Vec3 lag_range_target_position_second;             // target position (this tick)
extern "C" void (**ptr_to_pGlLineWidth)(float);
extern "C" void (**ptr_to_pGlPushMatrix)();
extern "C" void (**ptr_to_pGlPopMatrix)();
extern "C" void (**ptr_to_pGlTranslatef)(float, float, float);
extern "C" void (**ptr_to_pGlScalef)(float, float, float);
extern "C" void (**ptr_to_pGlColor3f)(float, float, float);
extern "C" void (**ptr_to_pGlColor4f)(float, float, float, float);
extern "C" const double offset_1_in_lag_range;
extern "C" const float one_in_lag_range, lag_range_box_line_width, lag_range_box_alpha;

extern "C" int lag_range_last_packet_sequence;              // last packet sequence
extern "C" unsigned char lag_range_spin_lock;    // spin lock
extern "C" long long lag_range_max_hold_budget;        // max hold budget
extern "C" unsigned char lag_range_target_moved_closer;    // target moved closer
extern "C" unsigned char lag_range_hold_active, lag_range_track_entity_action, lag_range_hold_just_released;
extern "C" Vec3 lag_range_target_position_when_window_started;             // target position when the window started
extern "C" unsigned char lag_range_active;    // active
extern "C" Vec3 lag_range_latest_target_position_from_packets;             // latest target position from packets
extern "C" unsigned char lag_range_sprinting_last_tick;    // sprinting last tick
extern "C" int lag_range_pause_ticks;              // pause ticks (potion)
extern "C" char lag_range_random_walk_1[];           // rng state
extern "C" float lag_range_delay;            // max hold (ms)
extern "C" unsigned char lag_range_require_weapon_value, lag_range_flush_splash_potion_value, lag_range_flush_sprint_reset_value, lag_range_render_remote_pos;
extern "C" const float random_range_bound_10_in_lag_range, random_range_bound_15_in_lag_range;
extern "C" const double walk_period_10_in_lag_range;
FN(void *, java_jni_is_instance_of_c03_packet_player, (void *ctx, void *event));
FN(unsigned char, java_get_c03_packet_player_moving, (void *ctx, void *c03_packet_player));
FN(Vec3, java_get_c03_packet_player_a, (void *ctx, void *c03_packet_player));
FN(void *, java_jni_is_instance_of_c02_packet_use_entity, (void *ctx, void *event));
FN(void *, java_jni_is_instance_of_c0_bpacket_entity_action, (void *ctx, void *event));
FN(int, java_c0_bpacket_entity_action, (void *ctx, void *object));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *object, int slot));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(unsigned, java_get_item_stack_metadata, (void *jvm, void *stack));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, vec3_add_eye_height_sneak, (Vec3 v));
FN(Range, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Range r, double p));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(int, java_get_game_settings_third_person_view, (void *jvm, void *game_settings));
FN(Vec3, java_get_entity_other_player_mp_other_player_mpx, (void *jvm, void *entity_other_player_mp));
FN(Vec3, vec3_lerp, (Vec3 a, Vec3 b, double t));
FN(Vec3, vec3_sub, (Vec3 a, Vec3 b));
FN(void, gl_array_xyz_float, ());
FN(void, gl_array_color1, ());
FN(void, gl_array_color2, ());
FN(void, disable_vertex_array_state, ());

FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));

extern "C" void lag_range_attack_hook(PhaseHook *hook) {
    if (hook->phase != 1)
        return;
    Ctx *ctx = hook->ctx;
    if (!ctx->player)
        return;
    int entity_ticks_existed = java_get_entity_ticks_existed(ctx->jvm, ctx->player);
    lag_range_hit_time_unchanged = entity_ticks_existed == java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
}

// render: box at the (interpolated) server-side target position
extern "C" void lag_range_render_box_interpolated(RenderHook *hook) {
    if (lag_range_have_target_position == 1) {
        Vec3 rel = vec3_sub(vec3_lerp(lag_range_target_position, lag_range_target_position_second, (double)hook->partial), hook->cam);
        gl_array_xyz_float();
        (*ptr_to_pGlLineWidth)(lag_range_line_width_setting);
        (*ptr_to_pGlPushMatrix)();
        (*ptr_to_pGlTranslatef)((float)rel.x, (float)(rel.y + offset_1_in_lag_range), (float)rel.z);
        float w = lag_range_box_line_width;
        (*ptr_to_pGlScalef)(w, one_in_lag_range, w);
        if (lag_range_filled_setting == 1) {
            (*ptr_to_pGlColor4f)(lag_range_color_setting[0], lag_range_color_setting[1], lag_range_color_setting[2], lag_range_box_alpha);
            gl_array_color1();
        }
        (*ptr_to_pGlColor3f)(lag_range_color_setting[0], lag_range_color_setting[1], lag_range_color_setting[2]);
        gl_array_color2();
        (*ptr_to_pGlPopMatrix)();
        disable_vertex_array_state();
    }
}

static inline __attribute__((always_inline)) void lock() {
    while (__atomic_exchange_n(&lag_range_spin_lock, (unsigned char)1, __ATOMIC_SEQ_CST))
        __builtin_ia32_pause();
}

// packet hook: hold outgoing packets while the target walks towards us
extern "C" void lag_range_packet_hook_hold(PktHook *hook) {
    lock();
    unsigned char act = lag_range_active;
    if (hook->seq > lag_range_last_packet_sequence) {
        if (act) {
            void *packet = java_jni_is_instance_of_c03_packet_player(hook->ctx, hook->ev);
            if (packet && java_get_c03_packet_player_moving(hook->ctx, packet)) {
                Vec3 c03_packet_player_a = java_get_c03_packet_player_a(hook->ctx, packet);
                double before = vec3_distance_sq(lag_range_target_position_when_window_started, lag_range_latest_target_position_from_packets);
                lag_range_target_moved_closer = before > vec3_distance_sq(lag_range_target_position_when_window_started, c03_packet_player_a);
            }
        }
        lag_range_last_packet_sequence = hook->seq;
        act = lag_range_active;
    }
    if (act && lag_range_target_moved_closer && !lag_range_hold_active && !lag_range_hold_just_released && !lag_range_track_entity_action && hook->budget < lag_range_max_hold_budget) {
        packet_hold(&hook->verdict);
    } else {
        // The bookkeeping below has to run even when another module already wrote a verdict: gating it
        // on verdict == 0 let criticals (which holds every outgoing packet) freeze the C02/C03 tracking
        // and the next lag window never opened at all.
        void *c03_packet_player = java_jni_is_instance_of_c03_packet_player(hook->ctx, hook->ev);
        if (c03_packet_player) {
            lag_range_hold_just_released = 0;
            if (java_get_c03_packet_player_moving(hook->ctx, c03_packet_player))
                lag_range_latest_target_position_from_packets = java_get_c03_packet_player_a(hook->ctx, c03_packet_player);
        } else {
            void *c02_packet_use_entity = java_jni_is_instance_of_c02_packet_use_entity(hook->ctx, hook->ev);
            if (c02_packet_use_entity) {
                if (lag_range_hold_active) {
                    lag_range_hold_active = 0;
                    lag_range_hold_just_released = 1;
                }
            } else {
                void *c0_bpacket_entity_action = java_jni_is_instance_of_c0_bpacket_entity_action(hook->ctx, hook->ev);
                if (c0_bpacket_entity_action && lag_range_track_entity_action && java_c0_bpacket_entity_action(hook->ctx, c0_bpacket_entity_action) == 4) {
                    lag_range_track_entity_action = 0;
                    lag_range_hold_just_released = 1;
                }
            }
        }
    }
    lag_range_spin_lock = 0;
}

// tick: decide whether a lag window may be opened for the current target
extern "C" void lag_range_tick_decide_whether(Hook *hook) {
    unsigned char hit_equal = lag_range_hit_time_unchanged;
    lag_range_hit_time_unchanged = 0;
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1 || !ctx->player)
        goto off;
    if (lag_range_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        goto off;
    if (lag_range_flush_splash_potion_value == 1) {
        void *inv = java_get_entity_player_inventory(ctx->jvm, ctx->player);
        void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, inv, java_get_inventory_player_current_item(ctx->jvm, inv));
        if (stack) {
            void *held = item_held_hotbar_slot(ctx->jvm, ctx->player);
            if (held && java_call_static_item_get_id_from_item(ctx->jvm, held) == 0x175 && (java_get_item_stack_metadata(ctx->jvm, stack) & 0x4000)) {
                lag_range_pause_ticks = 15;
                goto off;
            }
        }
        int n = lag_range_pause_ticks;
        if (n > 0) {
            lag_range_pause_ticks = n - 1;
            goto off;
        }
    }
    {
        unsigned char sprint = java_call_entity_is_sprinting(ctx->jvm, ctx->player);
        unsigned char stopped = (lag_range_sprinting_last_tick & 1) ? lag_range_flush_sprint_reset_value : 0;
        if (sprint)
            stopped = 0;
        lag_range_sprinting_last_tick = sprint;
        Target *target = (Target *)ctx->target;
        if (!target->ent)
            goto off;
        double dist2 = target->dist2;
        double r = walk_update(lag_range_random_walk_1, rot_make_floats(random_range_bound_10_in_lag_range, random_range_bound_15_in_lag_range), walk_period_10_in_lag_range);
        if (dist2 > r * r)
            goto off;
        unsigned char low_hurt = java_get_entity_living_base_hurt_time(ctx->jvm, target->ent) < 5;
        unsigned char moving = 0;
        if (lag_range_render_remote_pos == 1)
            moving = java_get_game_settings_third_person_view(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)) != 0;
        hit_equal &= low_hurt;
        Vec3 tp = java_get_entity_other_player_mp_other_player_mpx(ctx->jvm, target->ent);
        lock();
        lag_range_max_hold_budget = (long long)lag_range_delay;
        lag_range_hold_active = hit_equal;
        lag_range_track_entity_action = stopped;
        lag_range_active = 1;
        lag_range_target_position_when_window_started = tp;
        lag_range_target_position = lag_range_have_target_position == 1 ? lag_range_target_position_second : lag_range_latest_target_position_from_packets;
        lag_range_target_position_second = lag_range_latest_target_position_from_packets;
        lag_range_have_target_position = moving ? lag_range_target_moved_closer : 0;
        lag_range_spin_lock = 0;
        return;
    }
off:
    lock();
    lag_range_hold_just_released = 0;
    lag_range_hold_active = 0;
    lag_range_track_entity_action = 0;
    lag_range_active = 0;
    lag_range_spin_lock = 0;
    lag_range_have_target_position = 0;
}
