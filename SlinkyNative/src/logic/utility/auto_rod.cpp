// Auto Rod - cast and reel the fishing rod automatically.
#include "slinky/engine.h"
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct AABB { Vec3 lo, hi;
#ifdef __linux__
    ~AABB() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Target { void *padding_00; void *ent; double dist2; };
struct InHook { char padding_00[8]; void *mod; unsigned char use; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct AttackHook { char padding_00[8]; Ctx *ctx; unsigned char phase; unsigned char cancel; };
extern "C" char auto_rod_module_descriptor[];           // Auto Rod module descriptor
extern "C" unsigned char timer_range_min_dist_setting_field_0x58;
extern "C" unsigned char auto_rod_input_taken;    // input taken
extern "C" int auto_rod_stage;              // state
extern "C" unsigned char auto_rod_rod_swing_seen;    // rod swing seen

extern "C" float auto_rod_throw_delay;            // throw delay
extern "C" unsigned char auto_rod_randomise;    // randomise
extern "C" float auto_rod_pull_speed;            // pull speed
extern "C" char auto_rod_rng_states[], auto_rod_rng_states_second[], auto_rod_rng_states_third[], auto_rod_rng_states_fourth[];   // rng states
extern "C" float random_range_bound_0_5_in_auto_rod, range_low_0_01_in_auto_rod, random_range_bound_0_03_in_auto_rod, random_range_bound_1_in_auto_rod, twenty_in_auto_rod,
                 range_low_0_5_in_auto_rod_a, random_range_bound_1_in_auto_rod_a, range_low_0_01_in_auto_rod_a, random_range_bound_0_03_in_auto_rod_a, random_range_bound_2_in_auto_rod;
extern "C" double walk_period_5_in_auto_rod, walk_period_30_in_auto_rod, walk_period_1_in_auto_rod, walk_period_30_in_auto_rod_a;

extern "C" int auto_rod_slot_switch_back;              // slot to switch back to (-1 none)
extern "C" unsigned char auto_rod_abort_when_aiming_at_entity;    // abort when aiming at an entity
extern "C" unsigned char auto_rod_prefer_snowballs_eggs;    // prefer snowballs / eggs
extern "C" unsigned char auto_rod_switch_best_weapon_afterwards;    // switch to best weapon afterwards
extern "C" float auto_rod_projectile_speed;            // projectile speed
extern "C" int auto_rod_rod_slot;              // rod slot
extern "C" float auto_rod_step_delay;            // step delay
extern "C" int auto_rod_wait_ticks;              // wait ticks
extern "C" unsigned char auto_rod_using_fishing_rod;    // using a fishing rod
extern "C" unsigned char auto_rod_rod_swing_seen;
extern "C" const float minus_one_in_auto_rod;     // -1.0
extern "C" const double auto_rod_throw_delay_seconds, offset_1_8;
extern "C" const float ten_in_auto_rod;
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(void, java_set_inventory_player_current_item, (void *jvm, void *inventory_player, int slot));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(void, release_owner, (void *timer));
FN(unsigned char, claim_release_hotbar_module, (void *timer));
FN(void, queue_module_toggle, (void *module_manager, void *module, int enabled));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *object, int slot));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *stack));
FN(int, weapons_find_first_weapon_slot_entry, (Ctx *ctx, void *slot));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(void, java_set_key_binding_press_time, (void *jvm, void *key_binding, int n));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_variant, (void *jvm));
FN(Vec3, vec3_add_eye_height, (Vec3 v));
FN(Vec3, java_get_entity_other_player_mp_other_player_mpx, (void *jvm, void *entity_other_player_mp));
FN(Vec3, vec3_make, (double x, double y, double z));
FN(Vec3, vec3_sub, (Vec3 a, Vec3 b));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(AABB, aabb_make, (Vec3 lo, Vec3 hi));
FN(Vec3, aabb_clamp_point, (AABB box, Vec3 p));
FN(double, vec3_distance, (Vec3 a, Vec3 b));
FN(float, libc_ceilf, (float v));
FN(Rot, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Rot rng, double p));
FN(unsigned char, walk_should_change, (void *state, Rot rng, double p));
FN(float, random_rotation, (Rot rng));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));

extern "C" void auto_rod_on_input(InHook *hook) {
    if (__builtin_expect(auto_rod_input_taken != 0, 0) || hook->mod != auto_rod_module_descriptor)
        return;
    if (timer_range_min_dist_setting_field_0x58 == 1) {
        auto_rod_input_taken = 1;
    } else {
        int st = auto_rod_stage;
        if (st == 6)
            return;
        if (st == 3)
            auto_rod_stage = 7;
    }
    JOIN();
    hook->use = 1;
}

static inline void restore_slot(Ctx *ctx);

extern "C" void auto_rod_on_toggle(EvHook *hook) {
    if (hook->ev != auto_rod_module_descriptor)
        return;
    auto_rod_rod_swing_seen = 0;
    auto_rod_input_taken = 0;
    auto_rod_wait_ticks = 0;
    auto_rod_step_delay = 0.0f;
    auto_rod_stage = 0;
    if (hook->on != 1) {
        Ctx *ctx = hook->ctx;
        if (ctx->player)
            restore_slot(ctx);
        auto_rod_slot_switch_back = -1;
    }
}

extern "C" void auto_rod_on_attack(AttackHook *hook) {
    if (__builtin_expect(hook->phase == 0, 1)) {
        Ctx *ctx = hook->ctx;
        if (!ctx->player)
            return;
        void *held = item_held_hotbar_slot(ctx->jvm, ctx->player);
        if (held && java_call_static_item_get_id_from_item(ctx->jvm, held) == 0x15a) {
            if (auto_rod_rod_swing_seen)
                hook->cancel = 1;
            else
                auto_rod_rod_swing_seen = 1;
        }
    }

}

// randomised throw delay
extern "C" float auto_rod_randomised_throw_delay() {
    float d = auto_rod_throw_delay;
    if (auto_rod_randomise == 1) {
        Rot rng = rot_make_floats(0.0f, random_range_bound_0_5_in_auto_rod);
        char *st = auto_rod_rng_states;
        asm("" : "+r"(st));

        float m = d - walk_update(st, rng, walk_period_5_in_auto_rod);
        d = 0.0f;
        d = d > m ? d : m;
        float lo = range_low_0_01_in_auto_rod;
        asm("" : "+x"(lo));

        rng = rot_make_floats(lo, random_range_bound_0_03_in_auto_rod);
        st = auto_rod_rng_states_second;
        asm("" : "+r"(st));

        if (walk_should_change(st, rng, walk_period_30_in_auto_rod)) {
            lo = random_range_bound_0_5_in_auto_rod;
            asm("" : "+x"(lo));

            d += random_rotation(rot_make_floats(lo, random_range_bound_1_in_auto_rod));
        }
    }
    return d;
}

// randomised rod pull interval
extern "C" float auto_rod_randomised_rod_pull() {
    float base = auto_rod_pull_speed;
    float v;
    if (auto_rod_randomise) {
        float lo = range_low_0_5_in_auto_rod_a;
        asm("" : "+x"(lo));

        Rot rng = rot_make_floats(lo, random_range_bound_1_in_auto_rod_a);
        char *st = auto_rod_rng_states_third;
        asm("" : "+r"(st));

        float r = walk_update(st, rng, walk_period_1_in_auto_rod);
        float br = base * r;

        v = twenty_in_auto_rod / br;
        lo = range_low_0_01_in_auto_rod_a;
        asm("" : "+x"(lo));

        rng = rot_make_floats(lo, random_range_bound_0_03_in_auto_rod_a);
        st = auto_rod_rng_states_fourth;
        asm("" : "+r"(st));

        if (walk_should_change(st, rng, walk_period_30_in_auto_rod_a)) {
            lo = random_range_bound_1_in_auto_rod_a;
            asm("" : "+x"(lo));

            v += random_rotation(rot_make_floats(lo, random_range_bound_2_in_auto_rod));
        }
    } else {
        v = twenty_in_auto_rod / base;
    }
    return v;
}

// ticks the bobber needs to reach the target (distance to its hitbox / speed)
extern "C" int auto_rod_ticks_bobber_needs(Ctx *ctx) {
    void *ent = ((Target *)ctx->target)->ent;
    float t;
    if (!ent) {
        t = ten_in_auto_rod / auto_rod_projectile_speed;
    } else {
        Vec3 eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
        if (!java_variant(ctx->jvm))
            eye = vec3_add_eye_height(eye);
        Vec3 pos = java_get_entity_other_player_mp_other_player_mpx(ctx->jvm, ent);
        double k = auto_rod_throw_delay_seconds;
        Vec3 lo = vec3_sub(pos, vec3_make(k, 0.0, k));
        Vec3 hi = vec3_add(pos, vec3_make(k, offset_1_8, k));
        Vec3 hit = aabb_clamp_point(aabb_make(lo, hi), eye);
        t = (float)(vec3_distance(eye, hit) / (double)auto_rod_projectile_speed);
    }
    return (int)libc_ceilf(t);
}

static inline __attribute__((always_inline)) void restore_slot(Ctx *ctx) {
    if (auto_rod_slot_switch_back != -1) {
        java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_rod_slot_switch_back);
        auto_rod_slot_switch_back = -1;
    }
}

// main tick: 0 find rod -> 1 switch -> 2 throw -> 3 wait -> 4/5 switch back / reel -> 6 done, 7 cancel
extern "C" float auto_rod_randomised_throw_delay();
extern "C" float auto_rod_randomised_rod_pull();
extern "C" void auto_rod_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1 || !ctx->player)
        return;
    if (auto_rod_input_taken) {
        restore_slot(ctx);
        release_owner(auto_rod_module_descriptor);
        queue_module_toggle(ctx->module_manager, auto_rod_module_descriptor, 0);
        auto_rod_input_taken = 0;
        return;
    }
    for (int iter = 0; iter < 3; iter++) {
        if (auto_rod_abort_when_aiming_at_entity == 1) {
            void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
            if (minecraft && java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) == 2) {
                restore_slot(ctx);
                release_owner(auto_rod_module_descriptor);
                auto_rod_stage = 6;
                goto finished;
            }
        }
        if (auto_rod_stage == 7) {
            restore_slot(ctx);
            release_owner(auto_rod_module_descriptor);
            auto_rod_stage = 0;
            return;
        }
        if (auto_rod_wait_ticks)
            auto_rod_wait_ticks -= 1;
        float d = auto_rod_step_delay;
        if (d > 0.0f) {
            d += minus_one_in_auto_rod;
            auto_rod_step_delay = d;
            if (d > 0.0f)
                goto done;
        }
        void *inv = java_get_entity_player_inventory(ctx->jvm, ctx->player);
        int cur = java_get_inventory_player_current_item(ctx->jvm, inv);
        unsigned st = auto_rod_stage;
        int slot = auto_rod_rod_slot;
        if (st >= 2 && cur != slot) {
            release_owner(auto_rod_module_descriptor);
            auto_rod_stage = 6;
            goto finished;
        }
        int found = -1;
        switch (st) {
        case 0:
        case 2: {
            auto_rod_using_fishing_rod = 0;
            bool pref = false;
            for (int i = 0; i < 9; i++) {
                void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, inv, i);
                if (!stack)
                    continue;
                void *item = java_call_item_stack_get_item(ctx->jvm, stack);
                if (!item)
                    continue;
                int item_id = java_call_static_item_get_id_from_item(ctx->jvm, item);
                if (item_id == 0x14c || item_id == 0x158) {
                    if (auto_rod_prefer_snowballs_eggs == 1) {
                        auto_rod_using_fishing_rod = 0;
                        found = i;
                        pref = true;
                        break;
                    }
                    continue;
                }
                if (item_id != 0x15a)
                    continue;
                auto_rod_using_fishing_rod = 1;
                found = i;
                if (!auto_rod_prefer_snowballs_eggs) {
                    pref = true;
                    break;
                }
            }
            if (st == 0) {
                if (found == -1 || !claim_release_hotbar_module(auto_rod_module_descriptor)) {
                    auto_rod_stage = 6;
                    goto finished;
                }
                auto_rod_rod_slot = found;
                auto_rod_slot_switch_back = cur;
                if (auto_rod_switch_best_weapon_afterwards == 1) {
                    int b = weapons_find_first_weapon_slot_entry(ctx, inv);
                    if (b != -1)
                        auto_rod_slot_switch_back = b;
                    cur = auto_rod_slot_switch_back;
                }
                auto_rod_stage = (auto_rod_rod_slot == cur) + 1;
                continue;
            }
            (void)pref;
            if (found == -1) {
                auto_rod_stage = 5;
                return;
            }
            if (found == cur)
                goto throw_;
            auto_rod_rod_slot = found;
            auto_rod_stage = 1;
            auto_rod_step_delay += auto_rod_randomised_throw_delay();
            continue;
        }
        case 1:
            java_set_inventory_player_current_item(ctx->jvm, inv, slot);
            auto_rod_stage = 2;
            auto_rod_rod_swing_seen = 0;
            auto_rod_step_delay += auto_rod_randomised_throw_delay();
            continue;
        case 3:
            if (auto_rod_wait_ticks != 0)
                goto done;
            auto_rod_stage = auto_rod_slot_switch_back == slot ? 4 : 5;
            auto_rod_step_delay += auto_rod_randomised_throw_delay();
            continue;
        case 4:
            java_set_inventory_player_current_item(ctx->jvm, inv, (auto_rod_slot_switch_back + 1) % 9);
            auto_rod_stage = 5;
            return;
        case 5:
            auto_rod_stage = 6;
            restore_slot(ctx);
            release_owner(auto_rod_module_descriptor);
            auto_rod_step_delay += auto_rod_randomised_throw_delay();
            goto done;
        default:
            goto done;
        }
throw_:
        java_set_key_binding_press_time(ctx->jvm, java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)), 1);
        if (auto_rod_using_fishing_rod == 1) {
            auto_rod_stage = 3;
            auto_rod_wait_ticks = auto_rod_ticks_bobber_needs(ctx);
            auto_rod_step_delay += auto_rod_randomised_throw_delay();
        } else if (timer_range_min_dist_setting_field_0x58 == 1) {
            auto_rod_step_delay += auto_rod_randomised_rod_pull();
        } else {
            auto_rod_stage = 5;
            auto_rod_step_delay += auto_rod_randomised_throw_delay();
        }
        goto done;
    }
done:
    if (auto_rod_stage != 6)
        return;
finished:
    if (timer_range_min_dist_setting_field_0x58 == 1) {
        auto_rod_stage = 0;
        return;
    }
    queue_module_toggle(ctx->module_manager, auto_rod_module_descriptor, 0);
}
