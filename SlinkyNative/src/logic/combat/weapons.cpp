// Weapons - item classification shared by Auto Weapon / Auto Tool style modules.
//   weapons_check_held_item_cached: (cached per hotbar key) is the held item a weapon we accept?
//   weapons_is_item_allowed_by_class: does item id `id` of this item belong to an enabled weapon class?
//   weapons_find_first_weapon_slot: best hotbar slot (0..8) or -1
//   weapons_on_shutdown: clear the id cache
#include "slinky/engine.h"

struct SlotSetting { char padding_00[0x18]; unsigned char on; char padding_19[7]; };
struct IdCache { void *p, *q; };
extern "C" unsigned char weapons_fist_value;    // "fist"
extern "C" SlotSetting weapons_bool_slot_1[];    // per-slot "allow" toggles
extern "C" unsigned char weapons_enchant_sharpness_value;    // "enchant_sharpness"
extern "C" unsigned char weapons_enchant_knockback_value;    // "enchant_knockback"
extern "C" unsigned char weapons_enchant_fire_aspect_value;    // "enchant_fire_aspect"
extern "C" unsigned char weapons_axes_value;    // "axes"
extern "C" unsigned char weapons_swords_value;    // "swords"
extern "C" int weapons_cached_key;              // cached key
extern "C" unsigned char weapons_cached_verdict;    // cached verdict
extern "C" unsigned weapons_cached_enchant_flags;         // cached enchant flags
extern "C" IdCache weapons_slot_id_vector;

FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(int, java_get_inventory_player_current_item, (void *jvm, void *inventory_player));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *object, int slot));
FN(unsigned, get_weapon_enchant_flags, (void *jvm, void *item));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *item_stack));
FN(unsigned char, weapons_is_item_allowed_by_class_entry, (void *jvm, void *item));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(unsigned char, id_vector_contains, (IdCache *cache, int id));
FN(void, libc_free, (void *ptr));

extern "C" unsigned char weapons_check_held_item_cached(void *jvm, void *entity_player, unsigned *out) {
    int key = java_get_entity_ticks_existed(jvm, entity_player);
    if (key != weapons_cached_key) {
        weapons_cached_key = key;
        weapons_cached_enchant_flags = 0;
        void *player = java_get_entity_player_inventory(jvm, entity_player);
        int slot = java_get_inventory_player_current_item(jvm, player);
        void *item = java_call_iinventory_get_stack_in_slot(jvm, player, slot);
        unsigned char r;
        if (!item) {
            r = weapons_fist_value;
        } else {
            unsigned weapon_enchant_flags = get_weapon_enchant_flags(jvm, item);
            weapons_cached_enchant_flags = weapon_enchant_flags;
            r = 1;
            if (weapons_bool_slot_1[slot].on)
                goto store;
            if ((weapon_enchant_flags & 1) && weapons_enchant_sharpness_value)
                goto store;
            if ((weapon_enchant_flags & 2) && weapons_enchant_knockback_value)
                goto store;
            if (weapon_enchant_flags >= 4 && weapons_enchant_fire_aspect_value)
                goto store;
            r = weapons_is_item_allowed_by_class_entry(jvm, java_call_item_stack_get_item(jvm, item));
        }
store:
        weapons_cached_verdict = r;
    }
    if (out)
        *out = weapons_cached_enchant_flags;
    return weapons_cached_verdict;
}

extern "C" unsigned char weapons_is_item_allowed_by_class(void *jvm, void *item) {
    if (item) {
        int item_id = java_call_static_item_get_id_from_item(jvm, item);
        switch (item_id) {
        case 0x10b: case 0x10c: case 0x110: case 0x114: case 0x11b:
            return weapons_swords_value;
        case 0x102: case 0x10f: case 0x113: case 0x117: case 0x11e:
            return weapons_axes_value;
        default:
            if (item_id)
                return id_vector_contains(&weapons_slot_id_vector, item_id);
        }
    }
    return weapons_fist_value;
}

extern "C" int weapons_find_first_weapon_slot(void **ctx, void *player) {
    long i = 0;
    for (; i < 9; i++) {
        if (weapons_bool_slot_1[i].on)
            return (int)i;
        void *item = java_call_iinventory_get_stack_in_slot(*ctx, player, (int)i);
        if (!item)
            continue;
        void *jvm = *ctx;
        unsigned weapon_enchant_flags = get_weapon_enchant_flags(jvm, item);
        if ((weapon_enchant_flags & 1) && weapons_enchant_sharpness_value)
            return (int)i;
        if ((weapon_enchant_flags & 2) && weapons_enchant_knockback_value)
            return (int)i;
        if (weapon_enchant_flags >= 4 && weapons_enchant_fire_aspect_value)
            return (int)i;
        if (weapons_is_item_allowed_by_class_entry(jvm, java_call_item_stack_get_item(jvm, item)))
            return (int)i;
    }
    return -1;
}

extern "C" void weapons_on_shutdown(Hook *hook) {
    libc_free(weapons_slot_id_vector.p);
    weapons_slot_id_vector = IdCache{};
}
