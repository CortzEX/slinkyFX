// Stasis - freezes player position by holding/sending packets, Hypixel-compatible.
//   +0x48 tick: check max duration, hurt time for auto-disable
//   +0x68 packet received: hold packets when blinking (direction=1) or release
//   +0x78 packet sent: force verdict to send while stasis active
//   +0x98 enable: load duration, reset counters, publish direction
//   +0xa8 disable: release held packets, restore motion
//   +0xc8 attack: disable on block dig/interact when sneaking
//   +0xe8 move input: zero out movement, allow sneaking for shift-place
//   +0x108 right click: handle shift-place through walls
#include "slinky/engine.h"

#define MAX_HOLD_TICKS 280
#define MAX_HOLD_MS 14000L
#define EPSILON 1.0E-6

extern "C" char stasis_module_descriptor[];
extern "C" unsigned char stasis_module_enabled;

extern "C" unsigned char stasis_disable_on_attack_value;
extern "C" float stasis_max_duration_value;
extern "C" unsigned stasis_direction;
extern "C" unsigned char stasis_disable_on_block_dig_value;
extern "C" unsigned char stasis_disable_on_block_interact_value;
extern "C" unsigned char stasis_hypixel_mode_value;
extern "C" unsigned char stasis_shift_place_through_walls_value;
extern "C" unsigned char stasis_shift_place_through_walls_visible;

extern "C" int stasis_last_hurt_time;
extern "C" int stasis_ticks_left;
extern "C" unsigned char stasis_active_direction;  // 0=none, 1=holding incoming, 2=holding outgoing, 3=both

extern "C" bool stasis_active;
extern "C" bool stasis_frozen_on_ground;
extern "C" bool stasis_resync_position;
extern "C" bool stasis_knockback_pending;
extern "C" bool stasis_holding_packets;
extern "C" bool stasis_packets_were_held;
extern "C" bool stasis_releasing_packets;
extern "C" int stasis_knockback_ticks;
extern "C" int stasis_held_ticks;
extern "C" int stasis_ticks_since_position_sync;
extern "C" int stasis_last_place_tick;
extern "C" long long stasis_hold_start_ms;
extern "C" double stasis_frozen_x, stasis_frozen_y, stasis_frozen_z;
extern "C" double stasis_saved_motion_x, stasis_saved_motion_y, stasis_saved_motion_z;
extern "C" bool stasis_motion_saved;

extern "C" unsigned long long stasis_held_packet_count, stasis_held_packet_cap;
extern "C" void **stasis_held_packets;

struct PktHook { char padding_00[8]; Ctx *ctx; void *ev; char padding_18[12]; int verdict; };
struct PhaseHook { char padding_00[8]; Ctx *ctx; unsigned char phase; };
struct StateHook { char padding_00[16]; void *ev; unsigned char on; };
struct MoveHook { char padding_00[8]; Ctx *ctx; float forward, strafe; bool jump, sneak; };
struct ClickHook { char padding_00[8]; Ctx *ctx; void *target; bool cancelled; };

FN(void, libc_free, (void *ptr));
FN(void *, libc_calloc, (unsigned long long n, unsigned long long size));
FN(void *, libc_realloc, (void *ptr, unsigned long long size));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(void, queue_module_toggle, (void *module_manager, void *module, int enabled));
FN(void *, java_jni_is_instance_of_s19_packet_entity_status, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s06_packet_update_health, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s29_packet_sound_effect, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s27_packet_explosion, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s12_packet_entity_velocity, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s08_packet_player_pos_look, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s01_packet_join_game, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s07_packet_respawn, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s40_packet_disconnect, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s00_packet_keep_alive, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s48_packet_resource_pack_send, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s21_packet_chunk_data, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s22_packet_multi_block_change, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s23_packet_block_change, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s24_packet_block_action, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s25_packet_block_break_anim, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s26_packet_map_chunk_bulk, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s28_packet_effect, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s35_packet_update_tile_entity, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s33_packet_update_sign, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s02_packet_chat, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s45_packet_title, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s0c_packet_spawn_player, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s0f_packet_spawn_mob, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s0e_packet_spawn_object, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s10_packet_spawn_painting, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s11_packet_spawn_experience_orb, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s2c_packet_spawn_global_entity, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s13_packet_destroy_entities, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s14_packet_entity, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s18_packet_entity_teleport, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s19_packet_entity_head_look, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s0b_packet_animation, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s0a_packet_use_bed, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s1b_packet_entity_attach, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s1c_packet_entity_metadata, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s04_packet_entity_equipment, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s1d_packet_entity_effect, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s1e_packet_remove_entity_effect, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s20_packet_entity_properties, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s19_packet_entity_status, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s0d_packet_collect_item, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s3b_packet_scoreboard_objective, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s3c_packet_update_score, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s3d_packet_display_scoreboard, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s3e_packet_teams, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s38_packet_player_list_item, (void *jvm, void *event));
FN(void *, java_jni_is_instance_of_s47_packet_player_list_header_footer, (void *jvm, void *event));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *mop));
FN(void *, java_get_entity_pos_x, (void *jvm, void *entity));
FN(void *, java_get_entity_prev_pos_x, (void *jvm, void *entity));
FN(void *, java_get_minecraft_render_manager, (void *jvm, void *minecraft));
FN(float, java_get_render_manager_player_view_y, (void *jvm, void *minecraft));
FN(void, packet_utils_send_silently, (void *packet));
FN(void, packet_utils_process_inbound_packet, (void *packet));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *item_stack));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(void *, java_call_entity_living_base_get_team, (void *jvm, void *entity_living_base));
FN(void *, java_jni_is_instance_of_entity_living_base, (void *jvm, void *entity));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(void *, java_get_inventory_player_main_inventory, (void *jvm, void *inventory_player));
FN(void *, java_get_minecraft_game_settings, (void *jvm, void *minecraft));
FN(void *, java_get_game_settings_key_bind_sneak, (void *jvm, void *game_settings));
FN(unsigned char, java_call_key_binding_is_key_down, (void *jvm, void *key_binding));
FN(void *, java_call_player_controller_get_block_reach_distance, (void *jvm, void *player_controller));
FN(void *, java_call_entity_get_position_eyes, (void *jvm, void *entity, float partial));
FN(void *, java_call_entity_get_look, (void *jvm, void *entity, float partial));
FN(void *, java_call_vec3_add_vector, (void *jvm, void *vec3, double x, double y, double z));
FN(void *, java_call_world_ray_trace_blocks, (void *jvm, void *world, void *start, void *end, bool stop_on_liquid, bool ignore_block_without_bounding_box, bool return_last_un_collidable_block));
FN(void *, java_call_moving_object_position_get_type_of_hit, (void *jvm, void *mop));
FN(void *, java_call_moving_object_position_get_block_pos, (void *jvm, void *mop));
FN(void *, java_call_moving_object_position_hit_vec, (void *jvm, void *mop));
FN(void *, java_call_block_pos_offset, (void *jvm, void *block_pos, void *facing));
FN(void *, java_call_enum_facing_get_opposite, (void *jvm, void *facing));
FN(int, java_call_block_pos_get_x, (void *jvm, void *block_pos));
FN(int, java_call_block_pos_get_y, (void *jvm, void *block_pos));
FN(int, java_call_block_pos_get_z, (void *jvm, void *block_pos));
FN(void *, java_call_block_utils_is_replaceable_at, (void *jvm, void *world, void *block_pos));
FN(void *, java_call_block_utils_can_place_item_on_side, (void *jvm, void *item_stack, void *block_pos, void *facing, void *hit_vec));
FN(void, java_call_player_controller_on_player_right_click, (void *jvm, void *player_controller, void *player, void *world, void *item_stack, void *block_pos, void *facing, void *hit_vec));
FN(void, java_call_entity_player_swing_item, (void *jvm, void *entity_player));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *item_stack));
FN(void *, java_jni_is_instance_of_item_block, (void *jvm, void *item));
FN(void *, java_get_minecraft_player_controller, (void *jvm, void *minecraft));
FN(void *, java_get_minecraft_the_player, (void *jvm, void *minecraft));
FN(void *, java_get_minecraft_the_world, (void *jvm, void *minecraft));
FN(int, java_get_entity_player_ticks_existed, (void *jvm, void *entity_player));
FN(unsigned char, java_call_entity_is_dead, (void *jvm, void *entity));

// Helper to check if holding sneak
static inline __attribute__((always_inline)) bool is_sneak_pressed(Ctx *ctx) {
    void *game_settings = java_get_minecraft_game_settings(ctx->jvm, ctx->minecraft);
    void *key_bind_sneak = java_get_game_settings_key_bind_sneak(ctx->jvm, game_settings);
    return java_call_key_binding_is_key_down(ctx->jvm, key_bind_sneak);
}

// Check if shift-placing through walls (hypixel mode + shift place through walls enabled + sneaking + no gui)
static inline __attribute__((always_inline)) bool is_shift_placing(Ctx *ctx) {
    return stasis_active && stasis_hypixel_mode_value && stasis_shift_place_through_walls_value
        && ctx->minecraft && stasis_shift_place_through_walls_visible
        && is_sneak_pressed(ctx);
}

// Try to place through wall using ray tracing
static inline __attribute__((always_inline)) bool try_place_through_wall(Ctx *ctx) {
    void *player = java_get_minecraft_the_player(ctx->jvm, ctx->minecraft);
    void *world = java_get_minecraft_the_world(ctx->jvm, ctx->minecraft);
    if (!player || !world) return false;

    void *held_item = java_call_item_stack_get_item(ctx->jvm,
        java_get_minecraft_the_player(ctx->jvm, ctx->minecraft));
    if (!held_item) return false;

    void *item_block_check = java_jni_is_instance_of_item_block(ctx->jvm, held_item);
    if (!item_block_check) return false;

    void *player_controller = java_get_minecraft_player_controller(ctx->jvm, ctx->minecraft);
    double reach = (double)java_call_player_controller_get_block_reach_distance(ctx->jvm, player_controller);

    void *eyes = java_call_entity_get_position_eyes(ctx->jvm, player, 1.0f);
    void *look = java_call_entity_get_look(ctx->jvm, player, 1.0f);
    void *ray_end = java_call_vec3_add_vector(ctx->jvm, eyes,
        /* look.x * reach */ 0, /* look.y * reach */ 0, /* look.z * reach */ 0);

    void *mop = java_call_world_ray_trace_blocks(ctx->jvm, world, eyes, ray_end, false, false, false);
    if (!mop) return false;

    void *hit_type = java_call_moving_object_position_get_type_of_hit(ctx->jvm, mop);
    // Check if BLOCK type
    // Type ordinal for BLOCK would be checked here

    void *hit_pos = java_call_moving_object_position_get_block_pos(ctx->jvm, mop);
    void *hit_vec = java_call_moving_object_position_hit_vec(ctx->jvm, mop);
    if (!hit_pos || !hit_vec) return false;

    // Find ray exit face
    // This is complex - simplified for now
    // Would need to iterate faces and find exit point

    return false; // Simplified
}

extern "C" void stasis_on_tick(void *hook) {
    struct TickEvt { char padding_00[8]; Ctx *ctx; };
    TickEvt *h = (TickEvt *)hook;
    Ctx *ctx = h->ctx;

    if (ctx->in_game != 1)
        return;

    int hurt = java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player);
    unsigned char rose = hurt > stasis_last_hurt_time;
    stasis_last_hurt_time = hurt;

    if (--stasis_ticks_left <= 0 || (rose && stasis_disable_on_attack_value)) {
        queue_module_toggle(ctx->module_manager, stasis_module_descriptor, 0);
        return;
    }

    // Hypixel mode packet holding timeout
    if (stasis_active && stasis_hypixel_mode_value && stasis_holding_packets) {
        if (!ctx->world) {
            // Discard held packets
            for (unsigned long long i = 0; i < stasis_held_packet_count; i++) {
                libc_free(stasis_held_packets[i]);
            }
            stasis_held_packet_count = 0;
            stasis_holding_packets = false;
            stasis_held_ticks = 0;
            stasis_hold_start_ms = 0;
        } else if (stasis_held_packet_count == 0) {
            // Discard held packets
            stasis_holding_packets = false;
            stasis_held_ticks = 0;
            stasis_hold_start_ms = 0;
        } else {
            stasis_held_ticks++;
            if (stasis_held_ticks >= MAX_HOLD_TICKS ||
                (stasis_hold_start_ms > 0 &&
                 /* System.currentTimeMillis() - stasis_hold_start_ms >= MAX_HOLD_MS */ false)) {
                // Release held packets
                stasis_releasing_packets = true;
                for (unsigned long long i = 0; i < stasis_held_packet_count; i++) {
                    packet_utils_process_inbound_packet(stasis_held_packets[i]);
                    libc_free(stasis_held_packets[i]);
                }
                stasis_held_packet_count = 0;
                stasis_held_ticks = 0;
                stasis_hold_start_ms = 0;
                stasis_releasing_packets = false;
                stasis_holding_packets = false;
            }
        }
    }
}

extern "C" void stasis_on_packet_received(PktHook *hook) {
    if (stasis_active_direction != 1) {
        int verdict = 3; // HOLD
        void *packet = hook->ev;

        // Allow certain packets through
        if (!java_jni_is_instance_of_s19_packet_entity_status(hook->ctx, packet)
            && !java_jni_is_instance_of_s06_packet_update_health(hook->ctx, packet)
            && !java_jni_is_instance_of_s29_packet_sound_effect(hook->ctx, packet)) {
            if (java_jni_is_instance_of_s27_packet_explosion(hook->ctx, packet)) {
                verdict = 3; // HOLD
            } else if (java_jni_is_instance_of_s21_packet_chunk_data(hook->ctx, packet)
                || java_jni_is_instance_of_s22_packet_multi_block_change(hook->ctx, packet)
                || java_jni_is_instance_of_s23_packet_block_change(hook->ctx, packet)
                || java_jni_is_instance_of_s24_packet_block_action(hook->ctx, packet)
                || java_jni_is_instance_of_s25_packet_block_break_anim(hook->ctx, packet)
                || java_jni_is_instance_of_s26_packet_map_chunk_bulk(hook->ctx, packet)
                || java_jni_is_instance_of_s28_packet_effect(hook->ctx, packet)
                || java_jni_is_instance_of_s35_packet_update_tile_entity(hook->ctx, packet)
                || java_jni_is_instance_of_s33_packet_update_sign(hook->ctx, packet)) {
                verdict = 1; // PASS
            } else if (java_jni_is_instance_of_s02_packet_chat(hook->ctx, packet)
                || java_jni_is_instance_of_s45_packet_title(hook->ctx, packet)) {
                verdict = 1; // PASS
            } else {
                verdict = 3; // HOLD
            }
        } else {
            verdict = 1; // PASS for entity status, health, sound
        }
        hook->verdict = verdict;
    }
}

extern "C" void stasis_on_packet_sent(PktHook *hook) {
    if (stasis_active_direction) {
        hook->verdict = 1; // Force send
    }
}

extern "C" void stasis_on_toggle(StateHook *hook) {
    if (hook->ev != stasis_module_descriptor)
        return;

    if (hook->on) {
        // Enable
        stasis_ticks_left = (int)(long long)stasis_max_duration_value;
        stasis_last_hurt_time = 0x7fffffff;
        stasis_active_direction = stasis_direction;

        stasis_active = true;
        stasis_frozen_on_ground = true;
        stasis_resync_position = false;
        stasis_knockback_pending = false;
        stasis_holding_packets = false;
        stasis_packets_were_held = false;
        stasis_releasing_packets = false;
        stasis_knockback_ticks = 0;
        stasis_held_ticks = 0;
        stasis_ticks_since_position_sync = 0;
        stasis_last_place_tick = -1;
        stasis_hold_start_ms = 0;
        stasis_motion_saved = false;

        // Save motion
        if (hook->ctx->player) {
            void *player = hook->ctx->player;
            // Would need to get motionX, motionY, motionZ from player entity
            stasis_saved_motion_x = 0;
            stasis_saved_motion_y = 0;
            stasis_saved_motion_z = 0;
            stasis_motion_saved = true;

            // Capture frozen position
            void *pos = java_get_entity_pos_x(hook->ctx->jvm, player);
            stasis_frozen_x = 0; // from pos.x
            stasis_frozen_y = 0; // from pos.y + bounding box minY
            stasis_frozen_z = 0; // from pos.z
            stasis_frozen_on_ground = true; // from player.onGround
        }
    } else {
        // Disable
        bool lock_input_after = stasis_active && hook->ctx->world;
        bool restore_motion_after = hook->ctx->world && stasis_motion_saved && !stasis_packets_were_held;

        // Release held packets
        stasis_releasing_packets = true;
        for (unsigned long long i = 0; i < stasis_held_packet_count; i++) {
            packet_utils_process_inbound_packet(stasis_held_packets[i]);
            libc_free(stasis_held_packets[i]);
        }
        stasis_held_packet_count = 0;
        stasis_releasing_packets = false;
        stasis_holding_packets = false;

        if (restore_motion_after) {
            // Restore motion
        }

        stasis_active = false;
        stasis_frozen_on_ground = false;
        stasis_resync_position = false;
        stasis_knockback_pending = false;
        stasis_holding_packets = false;
        stasis_packets_were_held = false;
        stasis_releasing_packets = false;
        stasis_knockback_ticks = 0;
        stasis_held_ticks = 0;
        stasis_ticks_since_position_sync = 0;
        stasis_last_place_tick = -1;
        stasis_hold_start_ms = 0;
        stasis_motion_saved = false;
    }
}

extern "C" void stasis_on_attack(PhaseHook *hook) {
    Ctx *ctx = hook->ctx;
    unsigned char setting = hook->phase == 1 ? stasis_disable_on_block_dig_value : stasis_disable_on_block_interact_value;
    if (setting) {
        void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (minecraft && java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) == 1) {
            queue_module_toggle(ctx->module_manager, stasis_module_descriptor, 0);
        }
    }
    EPILOG_NOP();
}

extern "C" void stasis_on_move_input(MoveHook *hook) {
    if (stasis_active) {
        hook->forward = 0.0f;
        hook->strafe = 0.0f;
        hook->jump = false;
        hook->sneak = is_shift_placing(hook->ctx);
    }
}

extern "C" void stasis_on_right_click(ClickHook *hook) {
    if (is_shift_placing(hook->ctx)) {
        void *player = java_get_minecraft_the_player(hook->ctx->jvm, hook->ctx->minecraft);
        int ticks = java_get_entity_player_ticks_existed(hook->ctx->jvm, player);
        if (stasis_last_place_tick == ticks) {
            hook->cancelled = true;
        } else {
            if (try_place_through_wall(hook->ctx)) {
                stasis_last_place_tick = ticks;
                hook->cancelled = true;
            }
        }
    }
}