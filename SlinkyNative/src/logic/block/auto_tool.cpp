// Auto Tool - descriptor +0x1d8 (switch to the best tool for the block being
// mined, optionally switching back afterwards) and +0x1e8 (free the block filter).
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };  // +0x10 / +0x18

extern "C" int auto_tool_slot_switch_back;              // slot to switch back to (-1 = none)
extern "C" int auto_tool_slot_we_switched;              // slot we switched to
extern "C" float auto_tool_activation_time_value;            // "activation_time"
extern "C" unsigned char auto_tool_allow_using_fists_value;    // "allow_using_fists"
extern "C" unsigned char auto_tool_allow_using_sword_value;    // "allow_using_sword"
extern "C" unsigned char auto_tool_allow_using_tools_value;    // "allow_using_tools"
extern "C" unsigned char auto_tool_allow_using_other_value;    // "allow_using_other"
extern "C" unsigned char auto_tool_require_sneak_value;    // "require_sneak"
extern "C" unsigned char auto_tool_restrict_allowed_blocks;    // "restrict_allowed_blocks"
extern "C" unsigned char auto_tool_switch_back_value;    // "switch_back"
extern "C" char auto_tool_module_descriptor[];           // switch cooldown timer
extern "C" int auto_tool_activation_countdown;              // activation countdown
extern "C" void *auto_tool_allowed_block_set[2];         // allowed-block set
extern "C" const float one_in_auto_tool;     // minimum dig speed (1.0)

FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void *, java_get_game_settings_attack_key, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void *, java_get_game_settings_sneak_key, (void *jvm, void *game_settings));
FN(unsigned char, is_key_down, (void *jvm, void *k));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));
FN(Vec3, java_get_moving_object_position_block_x, (void *jvm, void *moving_object_position));
FN(void *, java_call_variant, (void *jvm, void *world, int x, int y, int z));
FN(int, get_block_id, (void *jvm, void *blk));
FN(unsigned char, id_vector_contains, (void *set, int id));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *object, int slot));
FN(float, java_call_item_stack_get_str_vs_block, (void *jvm, void *item_stack, void *blk));
FN(unsigned char, claim_release_hotbar_module, (void *timer));
FN(void, release_owner, (void *timer));
FN(void, java_set_inventory_player_current_item, (void *jvm, void *inventory_player, int slot));
FN(void, libc_free, (void *ptr));

extern "C" void auto_tool_on_inventory_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game != 1)
        return;
    // the controller dispatch site reads the local player and the world without checking either, and
    // across a world change both can still be missing while the window has focus; every accessor
    // below dereferences them through JNI
    if (!ctx->world || !ctx->player)
        return;
    void *player = java_get_entity_player_inventory(ctx->jvm, ctx->player);
    int slot = java_get_inventory_player_current_item(ctx->jvm, player);
    int prev = auto_tool_slot_switch_back;
    int n;
    void *blk;
    if (prev != -1 && auto_tool_slot_we_switched != slot) {
        auto_tool_slot_switch_back = -1;
        goto restore;
    }
    if (prev == -1) {
        const unsigned char *allow;
        void *held = item_held_hotbar_slot(ctx->jvm, ctx->player);
        if (!held) {
            allow = &auto_tool_allow_using_fists_value;
        } else {
            switch (java_call_static_item_get_id_from_item(ctx->jvm, held) - 0x100) {
            case 0x0: case 0x1: case 0x2: case 0xd: case 0xe: case 0xf: case 0x11: case 0x12: case 0x13: case 0x15: case 0x16: case 0x17: case 0x1c: case 0x1d: case 0x1e: case 0x67:
                allow = &auto_tool_allow_using_tools_value;
                break;
            case 0xb: case 0xc: case 0x10: case 0x14: case 0x1b:
                allow = &auto_tool_allow_using_sword_value;
                break;
            default:
                allow = &auto_tool_allow_using_other_value;
                break;
            }
        }
        if (*allow != 1)
            goto reset;
    }
    {
        void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
        if (java_call_entity_player_is_using_item(ctx->jvm, ctx->player))
            goto reset;
        if (!java_get_key_binding_key_code(ctx->jvm, java_get_game_settings_attack_key(ctx->jvm, obj)))
            goto reset;
        if (auto_tool_require_sneak_value == 1 && !is_key_down(ctx->jvm, java_get_game_settings_sneak_key(ctx->jvm, obj)))
            goto reset;
        void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        if (!minecraft || java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) != 1)
            goto reset;
        Vec3 v = java_get_moving_object_position_block_x(ctx->jvm, minecraft);
        blk = java_call_variant(ctx->jvm, ctx->world, (int)v.x, (int)v.y, (int)v.z);
        if (!blk)
            goto reset;
        if (auto_tool_restrict_allowed_blocks == 1 && !id_vector_contains(auto_tool_allowed_block_set, get_block_id(ctx->jvm, blk)))
            goto reset;
    }
    n = auto_tool_activation_countdown;
    if (n > 0) {
        n -= 1;
        goto store;
    }
    {
        void *jvm = ctx->jvm;
        int best = slot;
        float bestv = one_in_auto_tool;
        #pragma clang loop unroll(disable)
        for (int i = 0; i < 9; i++) {
            int s = (slot + i) % 9;
            void *stack = java_call_iinventory_get_stack_in_slot(jvm, player, s);
            if (!stack)
                continue;
            float item_stack_get_str_vs_block = java_call_item_stack_get_str_vs_block(jvm, stack, blk);
            if (item_stack_get_str_vs_block > bestv)
                best = s;
            bestv = item_stack_get_str_vs_block > bestv ? item_stack_get_str_vs_block : bestv;
        }
        if (slot == best)
            return;
        if (claim_release_hotbar_module(auto_tool_module_descriptor)) {
            java_set_inventory_player_current_item(ctx->jvm, player, best);
            auto_tool_slot_we_switched = best;
            if (auto_tool_slot_switch_back == -1 && auto_tool_switch_back_value)
                auto_tool_slot_switch_back = slot;
        }
        if (best != -1)
            return;
        goto restore;
    }
reset:
    n = (int)auto_tool_activation_time_value;
store:
    auto_tool_activation_countdown = n;
restore:
    release_owner(auto_tool_module_descriptor);
    if (auto_tool_slot_switch_back != -1) {
        java_set_inventory_player_current_item(ctx->jvm, player, auto_tool_slot_switch_back);
        auto_tool_slot_switch_back = -1;
    }
}

extern "C" void auto_tool_on_shutdown(Hook *hook) {
    libc_free(auto_tool_allowed_block_set[0]);
    auto_tool_allowed_block_set[0] = auto_tool_allowed_block_set[1] = nullptr;
}

// disable: hand the hotbar back. The claim taken at the switch site is only dropped by this module's own
// "stopped mining" path (restore:), so switching Auto Tool off mid-mine left the single hotbar owner slot
// pointing at us forever - claim_release_hotbar_module() then refuses every other module (Auto Weapon,
// Scaffold, Block In, Clutch) for the rest of the session, and the player stays on the slot we picked.
extern "C" void auto_tool_on_toggle(EvHook *hook) {
    if (hook->on || hook->ev != auto_tool_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    if (auto_tool_slot_switch_back != -1) {
        // toggles also run from the menu and from unload, where there is no player to switch back
        if (ctx->player)
            java_set_inventory_player_current_item(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player), auto_tool_slot_switch_back);
        auto_tool_slot_switch_back = -1;
    }
    release_owner(auto_tool_module_descriptor);
    auto_tool_activation_countdown = 0;
}
