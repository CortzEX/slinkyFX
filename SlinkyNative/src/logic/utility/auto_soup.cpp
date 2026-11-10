// Auto Soup - eat mushroom soup when health is low.
#include "slinky/engine.h"
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
#include "slinky/jids.h"

struct InHook { char padding_00[8]; void *mod; unsigned char use; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char auto_soup_module_descriptor[];           // Auto Soup module descriptor
extern "C" float auto_soup_delay_scale;            // delay scale
extern "C" unsigned char auto_soup_input_consumed;    // input consumed
extern "C" int auto_soup_stage;              // state
extern "C" unsigned char auto_soup_busy;
extern "C" float auto_soup_step_delay;           // step delay
extern "C" char auto_soup_rng_states[];           // rng states
extern "C" char auto_soup_random_walk_1[];
extern "C" float range_low_0_5_in_auto_soup;
extern "C" const double auto_soup_default_period_ticks;
extern "C" const double walk_period_5_in_auto_soup;
extern "C" const float range_low_0_03_in_auto_soup;
extern "C" const float random_range_bound_0_06_in_auto_soup;
extern "C" const float random_range_bound_2_5;

extern "C" float auto_soup_single_soup_max_hp;            // "health"
extern "C" float auto_soup_delays_refill_health, auto_soup_double_soup_max_hp, auto_soup_delays_refill_health_second, auto_soup_delays_refill_health_third;   // delays / refill health
extern "C" unsigned char auto_soup_override_item;    // custom item list
extern "C" unsigned char auto_soup_drop_bowls_value;    // "drop_bowls"
extern "C" int auto_soup_soup_slot;              // soup slot
extern "C" int auto_soup_original_slot;              // original slot
extern "C" unsigned char auto_soup_eat_another_one;    // eat another one
extern "C" unsigned char auto_soup_item_leaves_bowl;    // item leaves a bowl
extern "C" char auto_soup_random_walk_2[];           // rng state
extern "C" char auto_soup_custom_item_set[];           // custom item set
extern "C" const float offset_1_in_auto_soup, random_range_bound_0_01, random_range_bound_0_08_in_auto_soup, offset_neg_1_in_auto_soup;
extern "C" const double walk_period_30_in_auto_soup;
FN(void, queue_module_toggle, (void *module_manager, void *module, int enabled));
FN(float, java_call_entity_living_base_get_health, (void *jvm, void *entity_living_base));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(void *, java_get_inventory_player_main_inventory, (void *jvm, void *inv));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *stack));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(unsigned char, id_vector_contains, (void *set, int id));
FN(unsigned char, claim_release_hotbar_module, (void *timer));
FN(void, release_owner, (void *timer));
FN(void, java_set_inventory_player_current_item, (void *jvm, void *inventory_player, int slot));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_key_8_or_9, (void *jvm, void *game_settings));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(Rot, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Rot rng, double p));
FN(unsigned char, walk_should_change, (void *state, Rot rng, double p));
FN(float, random_rotation, (Rot rng));

extern "C" void auto_soup_input_hook(InHook *hook) {
    if (hook->mod != auto_soup_module_descriptor)
        return;
    int st = auto_soup_stage;
    if (st == 0 || st == 6)
        return;
    hook->use = 1;
    auto_soup_input_consumed = 1;
}

extern "C" void auto_soup_enable(EvHook *hook) {
    if (hook->ev != auto_soup_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    if (hook->on != 1) {
        // stage 2..5: the hotbar has been switched to the soup slot and the tick handler
        // will not run again, so restore the original slot now.
        unsigned st = auto_soup_stage;
        if (ctx->player && st >= 2 && st <= 5 && auto_soup_soup_slot != auto_soup_original_slot)
            java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_soup_original_slot);
        auto_soup_eat_another_one = 0;
    }
    auto_soup_stage = 0;
    auto_soup_busy = 0;
    auto_soup_step_delay = 0.0f;
    auto_soup_input_consumed = 0;
}

extern "C" float auto_soup_asm_pins_keep(float base) {
    float k = range_low_0_5_in_auto_soup;
    Rot rng = rot_make_floats(0.0f, k);
    char *st = auto_soup_rng_states;
    asm("" : "+r"(st));

    double period = auto_soup_default_period_ticks;
    asm("" : "+x"(period));

    float a = walk_update(st, rng, period);
    rng = rot_make_floats(0.0f, k);
    float s = walk_update(st, rng, walk_period_5_in_auto_soup) + a;
    float lo = range_low_0_03_in_auto_soup;
    asm("" : "+x"(lo));

    rng = rot_make_floats(lo, random_range_bound_0_06_in_auto_soup);
    if (walk_should_change(auto_soup_random_walk_1, rng, period)) {
        lo = range_low_0_5_in_auto_soup;
        asm("" : "+x"(lo));

        s += random_rotation(rot_make_floats(lo, random_range_bound_2_5));
    }
    return s * auto_soup_delay_scale + base;
}


// main tick: 0 check health -> 1 find soup -> 2 switch -> 3 eat -> 4 drop bowl
// -> 5 switch back (or eat again) -> 6 cool down
extern "C" float auto_soup_asm_pins_keep(float base);
extern "C" void auto_soup_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (!ctx->player)
        return;
    if (auto_soup_input_consumed == 1 && (auto_soup_stage == 6 || auto_soup_stage == 0)) {
        queue_module_toggle(ctx->module_manager, auto_soup_module_descriptor, 0);
        return;
    }
    if (ctx->in_game != 1) {
        auto_soup_busy = 0;
        auto_soup_stage = 0;
        return;
    }
    for (int iter = 0; iter < 3; iter++) {
        float t = auto_soup_step_delay;
        if (t > 0.0f) {
            auto_soup_step_delay = t + offset_neg_1_in_auto_soup;
            return;
        }
        switch (auto_soup_stage) {
        case 0: {
            float health = java_call_entity_living_base_get_health(ctx->jvm, ctx->player);
            if (health > auto_soup_single_soup_max_hp)
                return;
            auto_soup_step_delay = auto_soup_asm_pins_keep(auto_soup_delays_refill_health) + auto_soup_step_delay;
            auto_soup_eat_another_one = auto_soup_double_soup_max_hp >= health;
            auto_soup_stage = 1;
            break;
        }
        case 1: {
            void *inv = java_get_entity_player_inventory(ctx->jvm, ctx->player);
            unsigned char keep = auto_soup_busy;
            int cur = java_get_inventory_player_current_item(ctx->jvm, inv);
            void *hotbar = java_get_inventory_player_main_inventory(ctx->jvm, inv);
            int found = -1;
            for (int i = 0; i < 9; i++) {
                int s = (cur + i) % 9;
                if (keep & (s == cur))
                    continue;
                void *jvm = ctx->jvm;
                JNIEnv *env = ((Jvm *)jvm)->env;
                void *stack = (*env)->GetObjectArrayElement(env, hotbar, s);
                int item_id = stack ? java_call_static_item_get_id_from_item(jvm, java_call_item_stack_get_item(jvm, stack)) : 0;
                if (auto_soup_override_item == 1) {
                    if (!id_vector_contains(auto_soup_custom_item_set, item_id))
                        continue;
                } else if (item_id != 0x19d && item_id != 0x11a) {
                    continue;
                }
                auto_soup_item_leaves_bowl = (item_id == 0x11a) | (item_id == 0x19d);
                found = s;
                if (!walk_should_change(auto_soup_random_walk_2, rot_make_floats(random_range_bound_0_01, random_range_bound_0_08_in_auto_soup), walk_period_30_in_auto_soup))
                    break;
            }
            if (found == -1) {
                auto_soup_stage = 6 - auto_soup_busy;
                auto_soup_busy = 0;
                return;
            }
            auto_soup_soup_slot = found;
            int orig;
            if (auto_soup_busy == 1) {
                orig = auto_soup_original_slot;
            } else {
                orig = java_get_inventory_player_current_item(ctx->jvm, inv);
                auto_soup_original_slot = orig;
                found = auto_soup_soup_slot;
            }
            auto_soup_busy = 0;
            auto_soup_stage = (found == orig) | 2;
            break;
        }
        case 2:
            if (!claim_release_hotbar_module(auto_soup_module_descriptor)) {
                auto_soup_stage = 0;
                return;
            }
            java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_soup_soup_slot);
            auto_soup_stage = 3;
            auto_soup_step_delay = auto_soup_asm_pins_keep(auto_soup_delays_refill_health_second) + auto_soup_step_delay;
            if (java_call_entity_player_is_using_item(ctx->jvm, ctx->player)) {
                float x = auto_soup_step_delay;
                if (0.0f >= x)
                    auto_soup_step_delay = x + offset_1_in_auto_soup;
            }
            break;
        case 3:
            java_increment_key_binding_press_time(ctx->jvm, java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)));
            if (auto_soup_drop_bowls_value == 1 && auto_soup_item_leaves_bowl) {
                auto_soup_stage = 4;
                float x = auto_soup_asm_pins_keep(auto_soup_delays_refill_health_second) + auto_soup_step_delay;
                auto_soup_step_delay = x > 0.0f ? x : x + offset_1_in_auto_soup;
                break;
            }
            goto eaten;
        case 4:
            java_increment_key_binding_press_time(ctx->jvm, java_get_game_settings_key_8_or_9(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)));
        eaten:
            auto_soup_stage = 5;
            if (auto_soup_soup_slot != auto_soup_original_slot) {
                float x = auto_soup_asm_pins_keep(auto_soup_delays_refill_health_second) + auto_soup_step_delay;
                auto_soup_step_delay = x;
                if (0.0f >= x)
                    auto_soup_step_delay = x + offset_1_in_auto_soup;
            }
            break;
        case 5:
            release_owner(auto_soup_module_descriptor);
            if (auto_soup_eat_another_one == 1) {
                auto_soup_eat_another_one = 0;
                auto_soup_busy = 1;
                auto_soup_stage = 1;
                break;
            }
            java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_soup_original_slot);
            auto_soup_stage = 6;
            break;
        case 6:
            auto_soup_busy = 0;
            auto_soup_step_delay = auto_soup_asm_pins_keep(auto_soup_delays_refill_health_third) + auto_soup_step_delay;
            auto_soup_stage = 0;
            break;
        }
    }
}
