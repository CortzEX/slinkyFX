// Auto Pot - throw splash healing potions when low.
//   +0x98 (event): on low-health trigger work out how many potions to throw
//   +0x48 (tick):  state machine 0 find potion -> 1 switch slot -> 2 throw
//                  -> 3 next/switch back -> 4 done (module disables itself)
//   +0x88 (input): arm an extra throw for "double_splash"
//   0x396e0:       randomised delay between steps
#include "slinky/engine.h"
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct InHook { char padding_00[8]; void *mod; unsigned char use; };
extern "C" char auto_pot_module_descriptor[];           // Auto Pot module descriptor (timer at +0)
extern "C" unsigned char auto_pot_use_non_health_value;    // "use_non_health"
extern "C" float auto_pot_delay_value;            // "delay"
extern "C" unsigned char auto_pot_prevent_waste_value;    // "prevent_waste"
extern "C" unsigned char auto_pot_double_splash_value;    // "double_splash"
extern "C" int auto_pot_stage;              // state
extern "C" int auto_pot_potion_slot;              // potion slot
extern "C" int auto_pot_original_slot;              // original slot
extern "C" unsigned char auto_pot_keep_original_slot;    // keep original slot
extern "C" float auto_pot_step_delay;            // step delay
extern "C" int auto_pot_potions_left_throw;              // potions left to throw
extern "C" char auto_pot_rng_states[];           // rng states
extern "C" char auto_pot_initial_stage[];
extern "C" int auto_pot_extra_throws;              // extra throws
extern "C" const float offset_1_in_auto_pot;     // 1.0
extern "C" float offset_neg_1_in_auto_pot;     // -1.0
extern "C" double offset_0_5_in_auto_pot;
extern "C" double factor_0_125;
extern "C" float range_high_0_5_in_auto_pot;
extern "C" double five_in_auto_pot;
extern "C" float range_low_0_01_in_auto_pot;
extern "C" float range_high_0_03_in_auto_pot;
extern "C" double auto_pot_default_delay_ticks;
extern "C" float range_high_1_in_auto_pot_a;

FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *object, int slot));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *item_stack));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(unsigned, java_get_item_stack_metadata, (void *jvm, void *stack));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(void, java_set_inventory_player_current_item, (void *jvm, void *inventory_player, int slot));
FN(unsigned char, claim_release_hotbar_module, (void *timer));
FN(void, release_owner, (void *timer));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(void, queue_module_toggle, (void *module_manager, void *module, int enabled));
FN(float, java_call_entity_living_base_get_health, (void *jvm, void *entity_living_base));
FN(float, java_call_entity_living_base_get_max_health, (void *jvm, void *entity_living_base));
FN(Rot, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Rot rng, double p));
FN(unsigned char, walk_should_change, (void *state, Rot rng, double p));
FN(float, random_rotation, (Rot rng));
extern "C" float auto_pot_asm_r_pins();

extern "C" void auto_pot_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (!ctx->player)
        return;
    #pragma clang loop unroll(disable)
    for (int iter = 0; iter < 4; iter++) {
        float t = auto_pot_step_delay;
        if (t > 0.0f) {
            auto_pot_step_delay = t + offset_neg_1_in_auto_pot;
            goto end;
        }
        if (!ctx->in_game) {
            auto_pot_keep_original_slot = 0;
            goto stop;
        }
        switch (auto_pot_stage) {
        case 0: {
            void *player = java_get_entity_player_inventory(ctx->jvm, ctx->player);
            int slot = 0;
            #pragma clang loop unroll(disable)
            for (;; slot++) {
                if (slot == 9) {
                    auto_pot_potions_left_throw = 0;
                    int st = 4 - auto_pot_keep_original_slot;
                    auto_pot_stage = st;
                    auto_pot_keep_original_slot = 0;
                    if (st != 4)
                        return;
                    goto disable;
                }
                void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, player, slot);
                if (!stack)
                    continue;
                void *item = java_call_item_stack_get_item(ctx->jvm, stack);
                if (!item || java_call_static_item_get_id_from_item(ctx->jvm, item) != 0x175)
                    continue;
                unsigned dmg = java_get_item_stack_metadata(ctx->jvm, stack);
                if (!(dmg & 0x4000))
                    continue;
                if ((dmg & 0xf) != 5 && !auto_pot_use_non_health_value)
                    continue;
                break;
            }
            auto_pot_potion_slot = slot;
            int orig;
            if (auto_pot_keep_original_slot == 1) {
                orig = auto_pot_original_slot;
            } else {
                orig = java_get_inventory_player_current_item(ctx->jvm, player);
                auto_pot_original_slot = orig;
                slot = auto_pot_potion_slot;
            }
            auto_pot_keep_original_slot = 0;
            auto_pot_stage = (slot == orig) + 1;
            continue;
        }
        case 1:
            if (!claim_release_hotbar_module(auto_pot_module_descriptor))
                goto stop;
            java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_pot_potion_slot);
            auto_pot_stage = 2;
            t = auto_pot_asm_r_pins();
            break;
        case 2:
            if (java_call_entity_player_is_using_item(ctx->jvm, ctx->player))
                goto end;
            java_increment_key_binding_press_time(ctx->jvm, java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)));
            auto_pot_potions_left_throw += -1;
            auto_pot_stage = 3;
            if (auto_pot_potion_slot == auto_pot_original_slot)
                continue;
            t = auto_pot_asm_r_pins() + offset_1_in_auto_pot;
            break;
        case 3:
            if (auto_pot_potions_left_throw > 0) {
                auto_pot_keep_original_slot = 1;
                auto_pot_stage = 0;
            } else {
                java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_pot_original_slot);
                auto_pot_stage = 4;
            }
            continue;
        case 4:
            release_owner(auto_pot_module_descriptor);
            goto end;
        default:
            __builtin_unreachable();
        }
        auto_pot_step_delay = t + auto_pot_step_delay;
    }
end:
    if (auto_pot_stage != 4)
        return;
    goto disable;
stop:
    auto_pot_stage = 4;
disable:
    queue_module_toggle(ctx->module_manager, auto_pot_module_descriptor, 0);
}

extern "C" void auto_pot_on_input(InHook *hook) {
    // the original evaluates all three conditions with setcc and ORs them
    unsigned char other = hook->mod != auto_pot_module_descriptor;
    asm("" : "+r"(other));

    unsigned char done = auto_pot_stage == 4;
    asm("" : "+r"(done));

    done |= other;
    int extra = auto_pot_extra_throws;
    unsigned char none = extra <= 0;
    asm("" : "+r"(none));

    none |= done;
    if (!none) {
        hook->use = 1;
        auto_pot_potions_left_throw += 1;
        auto_pot_extra_throws = extra - 1;
    }


    SHARED_EPILOG();
}

extern "C" void auto_pot_on_toggle(EvHook *hook) {
    // Declared before the first goto: MSVC lets a jump step over an initialisation, the SysV C++ front end
    // makes it an error, and the same file has to build for both.
    Ctx *ctx = hook->ctx;
    float health;
    int n;
    if (hook->ev != auto_pot_module_descriptor)
        goto end;
    if (hook->on != 1) {
        // stage 1..3: the hotbar may already be on the potion slot - the tick handler is gone
        // once the module is disabled, so put it back here.
        unsigned st = auto_pot_stage;
        if (ctx->player && st >= 1 && st <= 3 && auto_pot_potion_slot != auto_pot_original_slot)
            java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_pot_original_slot);
        auto_pot_stage = 0;
        auto_pot_potions_left_throw = 0;
        auto_pot_keep_original_slot = 0;
        auto_pot_extra_throws = 0;
        auto_pot_step_delay = 0.0f;
        goto end;
    }
    if (ctx->in_game != 1 || !ctx->player)
        goto end;
    health = java_call_entity_living_base_get_health(ctx->jvm, ctx->player);
    n = (int)(((double)(java_call_entity_living_base_get_max_health(ctx->jvm, ctx->player) - health) + offset_0_5_in_auto_pot) * factor_0_125);
    if (n == 0) {
        n = 1;
        if (auto_pot_prevent_waste_value == 1) {
            auto_pot_stage = 4;
            goto end;
        }
    }
    if (auto_pot_double_splash_value == 1) {
        auto_pot_potions_left_throw = n;
        auto_pot_extra_throws = 0;
    } else {
        auto_pot_potions_left_throw = 1;
        auto_pot_extra_throws = auto_pot_prevent_waste_value ? n - 1 : 5;
    }
end:

}

extern "C" float auto_pot_asm_r_pins() {
    float d = auto_pot_delay_value;
    float r = 0.0f;
    asm("" : "+x"(r));

    float lo = 0.0f, hi = range_high_0_5_in_auto_pot;
    Rot rng = rot_make_floats(lo, hi);
    char *st = auto_pot_rng_states;
    asm("" : "+r"(st));

    double k = five_in_auto_pot;
    float m = d - walk_update(st, rng, k);
    r = r > m ? r : m;
    lo = range_low_0_01_in_auto_pot;
    hi = range_high_0_03_in_auto_pot;
    rng = rot_make_floats(lo, hi);
    st = auto_pot_initial_stage;
    asm("" : "+r"(st));

    k = auto_pot_default_delay_ticks;
    if (walk_should_change(st, rng, k)) {
        lo = range_high_0_5_in_auto_pot;
        hi = range_high_1_in_auto_pot_a;
        r += random_rotation(rot_make_floats(lo, hi));
    }
    return r;
}
