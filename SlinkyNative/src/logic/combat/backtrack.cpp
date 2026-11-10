// Backtrack - hold the target's movement packets for up to a budget and release
// them on our own motion, explosions or a transaction mismatch.
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
struct Target { void *padding_00; void *ent; double dist2; };
struct RenderHook { char padding_00[8]; Vec3 cam; char padding_20[8]; float partial; };
struct PktHook { char padding_00[8]; void *ctx; void *ev; long long budget; int seq; int verdict; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char backtrack_module_descriptor[];           // Backtrack module descriptor
extern "C" int backtrack_last_released_sequence;              // last released sequence
extern "C" int backtrack_last_held_sequence;              // last held sequence
extern "C" unsigned char backtrack_spin_lock;    // spin lock
extern "C" long long backtrack_max_hold_budget;        // max hold budget
extern "C" int backtrack_target_entity_id;              // target entity id
extern "C" int backtrack_own_entity_id;              // own entity id
extern "C" unsigned char backtrack_active;    // active
extern "C" unsigned char backtrack_stop_on_velocity_setting;    // "stop_on_velocity"

extern "C" Vec3 backtrack_accumulated_offset;             // accumulated offset
extern "C" unsigned char backtrack_stop_on_velocity_setting_field_0x1f, backtrack_stop_on_velocity_setting_field_0x27;
extern "C" float backtrack_stop_on_velocity_setting_field_0x23;
extern "C" unsigned char backtrack_render_box;    // render box
extern "C" Vec3 backtrack_previous_current_server_position, backtrack_previous_current_server_position_in_reach_distance_checks_a, backtrack_previous_current_server_position_in_reach_distance_checks;   // previous / current / server position
extern "C" float backtrack_yaws, backtrack_yaws_in_reach_distance_checks_a, backtrack_yaws_in_reach_distance_checks;  // yaws
extern "C" int backtrack_cooldown_ticks;              // cooldown ticks
extern "C" float backtrack_line_width_setting;            // "line_width"
extern "C" float backtrack_color_setting[3];         // "color"
extern "C" unsigned char backtrack_rotate_box;    // rotate box
extern "C" unsigned char backtrack_filled_setting;    // "filled"
extern "C" float backtrack_max_target_hurt_time;            // max target hurt time
extern "C" float backtrack_min_max_range, backtrack_min_max_range_second;   // min / max range
extern "C" float backtrack_delay;            // max hold (ms)
extern "C" float backtrack_cooldown;            // cooldown
extern "C" unsigned char backtrack_require_mouse_pressed_value, backtrack_require_weapon_value, backtrack_disable_on_hit_value, backtrack_render_remote_pos;
extern "C" void (**ptr_to_pGlLineWidth)(float);
extern "C" void (**ptr_to_pGlPushMatrix)();
extern "C" void (**ptr_to_pGlPopMatrix)();
extern "C" void (**ptr_to_pGlTranslatef)(float, float, float);
extern "C" void (**ptr_to_pGlScalef)(float, float, float);
extern "C" void (**ptr_to_pGlRotatef)(float, float, float, float);
extern "C" void (**ptr_to_pGlColor3f)(float, float, float);
extern "C" void (**ptr_to_pGlColor4f)(float, float, float, float);
extern "C" const double offset_1_in_backtrack, threshold_neg_0_05_in_backtrack;
extern "C" const float one_in_backtrack, backtrack_box_line_width, half_turn_degrees_in_backtrack, backtrack_box_alpha;

FN(Vec3, vec3_lerp, (Vec3 a, Vec3 b, double t));
FN(Vec3, vec3_sub, (Vec3 a, Vec3 b));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(Vec3, vec3_make, (double x, double y, double z));
FN(Vec3, vec3_add_eye_height_sneak, (Vec3 v));
FN(float, lerp_unclamped_float, (float a, float b, float t));
FN(void, gl_array_xyz_float, ());
FN(void, gl_array_color1, ());
FN(void, gl_array_color2, ());
FN(void, disable_vertex_array_state, ());
FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, java_get_entity_other_player_mp_other_player_mpx, (void *jvm, void *entity_other_player_mp));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(int, java_get_entity_entity_id, (void *jvm, void *entity));
FN(unsigned char, entity_reach_check, (Ctx *ctx, void *entity, Vec3 eye));
FN(double, segment_distance, (Vec3 eye, Vec3 pos, Vec3 off));
FN(void, backtrack_record_server_position, (Vec3 off, float yaw));
FN(void *, java_jni_is_instance_of_s19_packet_entity_status, (void *ctx, void *event));
FN(int, java_get_s19_packet_entity_status_entity_id, (void *ctx, void *s19_packet_entity_status));
FN(void *, java_jni_is_instance_of_s06_packet_update_health, (void *ctx, void *event));
FN(void *, java_jni_is_instance_of_s29_packet_sound_effect, (void *ctx, void *event));
FN(void *, java_jni_is_instance_of_s12_packet_entity_velocity, (void *ctx, void *event));
FN(int, java_get_s12_packet_entity_velocity_entity_id, (void *ctx, void *s12_packet_entity_velocity));
FN(void *, java_jni_is_instance_of_s08_packet_player_pos_look, (void *ctx, void *event));
FN(void, check_entity_distance, (void **hook_ctx, unsigned char hold));

extern "C" void backtrack_packet_hook_under(PktHook *hook) {
    volatile bool busy;
    while ((busy = __atomic_exchange_n(&backtrack_spin_lock, (unsigned char)1, __ATOMIC_SEQ_CST)))
        __builtin_ia32_pause();
    if (backtrack_active == 1) {
        void *s19_packet_entity_status = java_jni_is_instance_of_s19_packet_entity_status(hook->ctx, hook->ev);
        if (s19_packet_entity_status) {
            if (java_get_s19_packet_entity_status_entity_id(hook->ctx, s19_packet_entity_status) == backtrack_target_entity_id)
                goto pass;
        } else if (java_jni_is_instance_of_s06_packet_update_health(hook->ctx, hook->ev) || java_jni_is_instance_of_s29_packet_sound_effect(hook->ctx, hook->ev)) {
            goto pass;
        }
        {
            void *s12_packet_entity_velocity = java_jni_is_instance_of_s12_packet_entity_velocity(hook->ctx, hook->ev);
            if (!s12_packet_entity_velocity) {
                if (java_jni_is_instance_of_s08_packet_player_pos_look(hook->ctx, hook->ev))
                    goto stop;
            } else if (backtrack_stop_on_velocity_setting && java_get_s12_packet_entity_velocity_entity_id(hook->ctx, s12_packet_entity_velocity) == backtrack_own_entity_id) {
                goto stop;
            }
        }
        if (hook->budget <= backtrack_max_hold_budget) {
            packet_hold(&hook->verdict);
            int seq = hook->seq;
            if (seq > backtrack_last_released_sequence) {
                backtrack_last_released_sequence = seq;
                check_entity_distance(&hook->ctx, 0);
            }
        } else if (hook->seq > backtrack_last_held_sequence) {
            // Running the offset accounting under another module's verdict is required: the packet may
            // have been held by fake_lag or blink, but the server-side position still has to be undone.
            check_entity_distance(&hook->ctx, 1);
            backtrack_last_held_sequence = hook->seq;
        }
        goto unlock;
pass:
        packet_flush(&hook->verdict);
        goto unlock;
stop:
        backtrack_active = 0;
    }
unlock:
    backtrack_spin_lock = 0;
}

static inline __attribute__((always_inline)) void lock() {
    while (__atomic_exchange_n(&backtrack_spin_lock, (unsigned char)1, __ATOMIC_SEQ_CST))
        __builtin_ia32_pause();
}

// render: box at the held (server-side) position of the target
extern "C" void backtrack_render_box_held(RenderHook *hook) {
    if (backtrack_render_box != 1)
        return;
    Vec3 rel = vec3_sub(vec3_lerp(backtrack_previous_current_server_position, backtrack_previous_current_server_position_in_reach_distance_checks_a, (double)hook->partial), hook->cam);
    gl_array_xyz_float();
    (*ptr_to_pGlLineWidth)(backtrack_line_width_setting);
    (*ptr_to_pGlPushMatrix)();
    (*ptr_to_pGlTranslatef)((float)rel.x, (float)(rel.y + offset_1_in_backtrack), (float)rel.z);
    float w = backtrack_box_line_width;
    (*ptr_to_pGlScalef)(w, one_in_backtrack, w);
    if (backtrack_rotate_box == 1) {
        float yaw = lerp_unclamped_float(backtrack_yaws, backtrack_yaws_in_reach_distance_checks_a, hook->partial);
        (*ptr_to_pGlRotatef)(half_turn_degrees_in_backtrack - yaw, 0.0f, one_in_backtrack, 0.0f);
    }
    if (backtrack_filled_setting == 1) {
        (*ptr_to_pGlColor4f)(backtrack_color_setting[0], backtrack_color_setting[1], backtrack_color_setting[2], backtrack_box_alpha);
        gl_array_color1();
    }
    (*ptr_to_pGlColor3f)(backtrack_color_setting[0], backtrack_color_setting[1], backtrack_color_setting[2]);
    gl_array_color2();
    (*ptr_to_pGlPopMatrix)();
    disable_vertex_array_state();
}

// tick: decide whether to start / keep backtracking the current target
extern "C" void backtrack_tick_decide_whether(Hook *hook) {
    Ctx *ctx = hook->ctx;
    int n = backtrack_cooldown_ticks;
    if (n > 0) {
        backtrack_cooldown_ticks = n - 1;
        if (n != 1)
            return;
    }
    if (ctx->in_game != 1 || !ctx->player)
        goto reset;
    if (backtrack_require_mouse_pressed_value == 1 && !vertex_pointer_current(ctx))
        goto reset;
    if (backtrack_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
        goto reset;
    {
        int ht = java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
        if (java_get_entity_ticks_existed(ctx->jvm, ctx->player) - ht > 20)
            goto reset;
    }
    {
        Target *target = (Target *)ctx->target;
        if (!target->ent)
            goto reset;
        if ((float)java_get_entity_living_base_hurt_time(ctx->jvm, target->ent) > backtrack_max_target_hurt_time)
            goto reset;
        double dist2 = target->dist2;
        if ((double)(backtrack_min_max_range * backtrack_min_max_range) > dist2 || dist2 > (double)(backtrack_min_max_range_second * backtrack_min_max_range_second))
            goto reset;
        Vec3 eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
        if (java_variant(ctx->jvm))
            eye = vec3_add_eye_height_sneak(eye);
        Vec3 tp = java_get_entity_other_player_mp_other_player_mpx(ctx->jvm, target->ent);
        float yaw = get_entity_rotation(ctx->jvm, target->ent).yaw;
        int tid = java_get_entity_entity_id(ctx->jvm, target->ent);
        int own = java_get_entity_entity_id(ctx->jvm, ctx->player);
        lock();
        backtrack_max_hold_budget = (long long)backtrack_delay;
        int old_tid = backtrack_target_entity_id;
        backtrack_target_entity_id = tid;
        backtrack_own_entity_id = own;
        backtrack_stop_on_velocity_setting = backtrack_disable_on_hit_value;
        if (backtrack_active != 1) {
            if (!entity_reach_check(ctx, target->ent, eye)) {
                backtrack_render_box = 0;
                backtrack_spin_lock = 0;
                return;
            }
            backtrack_active = 1;
            backtrack_accumulated_offset = vec3_make(0.0, 0.0, 0.0);
            backtrack_stop_on_velocity_setting_field_0x1f = 0;
            backtrack_stop_on_velocity_setting_field_0x27 = 0;
            Vec3 position = java_get_entity_pos_x(ctx->jvm, target->ent);
            backtrack_previous_current_server_position = position;
            backtrack_previous_current_server_position_in_reach_distance_checks_a = position;
            backtrack_previous_current_server_position_in_reach_distance_checks = position;
            backtrack_yaws = yaw;
            backtrack_yaws_in_reach_distance_checks_a = yaw;
            backtrack_yaws_in_reach_distance_checks = yaw;
            backtrack_render_box = backtrack_render_remote_pos;
            backtrack_spin_lock = 0;
            return;
        }
        Vec3 off = backtrack_stop_on_velocity_setting_field_0x1f == 1 ? backtrack_accumulated_offset : vec3_add(backtrack_accumulated_offset, tp);
        double d = segment_distance(eye, tp, off);
        if (threshold_neg_0_05_in_backtrack > d || old_tid != tid) {
            backtrack_accumulated_offset = vec3_make(0.0, 0.0, 0.0);
            backtrack_stop_on_velocity_setting_field_0x1f = 0;
            backtrack_stop_on_velocity_setting_field_0x27 = 0;
            backtrack_render_box = 0;
            backtrack_active = 0;
            backtrack_cooldown_ticks = (int)backtrack_cooldown;
            backtrack_spin_lock = 0;
            return;
        }
        backtrack_record_server_position(off, backtrack_stop_on_velocity_setting_field_0x27 ? backtrack_stop_on_velocity_setting_field_0x23 : yaw);
        backtrack_spin_lock = 0;
        return;
    }
reset:
    lock();
    if (backtrack_active == 1) {
        backtrack_cooldown_ticks = (int)backtrack_cooldown;
        backtrack_active = 0;
    }
    backtrack_spin_lock = 0;
    backtrack_render_box = 0;
}

extern "C" void backtrack_on_toggle(EvHook *hook) {
    if (hook->ev != backtrack_module_descriptor || hook->on)
        return;
    // Disabling mid-window left `active` set: the next enable then skips the init block above and keeps
    // replaying the previous window's accumulated offset and held server position.
    lock();
    backtrack_active = 0;
    backtrack_spin_lock = 0;
    backtrack_render_box = 0;
}
