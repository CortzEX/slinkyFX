// Ghost Hand - descriptor +0xc8: raise the active flag; 0x46d10: may the
// attack go "through" (ignore) the entity `e` with the held item?
#include "slinky/engine.h"

struct Setting { char padding_00[0x18]; unsigned char on; };

extern "C" unsigned char ghost_hand_module_enabled;    // module enabled
extern "C" unsigned char ghost_hand_require_mouse_setting;    // "require_mouse"
extern "C" unsigned char ghost_hand_allow_with_fists;    // allow with fists
extern "C" unsigned char ghost_hand_swords;    // swords
extern "C" unsigned char ghost_hand_tools;    // tools
extern "C" unsigned char ghost_hand_bows;    // bows
extern "C" unsigned char ghost_hand_flint_steel;    // flint and steel
extern "C" unsigned char ghost_hand_cobweb;    // cobweb
extern "C" unsigned char ghost_hand_other_items;    // other items
extern "C" unsigned char ghost_hand_non_living_entities;    // non-living entities
extern "C" Setting ghost_hand_bots_setting;          // bots
extern "C" Setting ghost_hand_friendlies_setting;          // friends / teammates
extern "C" Setting ghost_hand_enemies_setting;          // others

FN(unsigned char, vertex_pointer_current, (Ctx *ctx));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(void *, java_jni_is_instance_of_entity_other_player_mp, (void *jvm, void *candidate));
FN(unsigned, entity_flags_get, (void *jvm, void *t));

extern "C" unsigned char ghost_hand_should_ignore_entity(Ctx *ctx, void *entity) {
    if (ghost_hand_module_enabled != 1)
        return 0;
    if (ghost_hand_require_mouse_setting == 1 && !vertex_pointer_current(ctx))
        return 0;
    const unsigned char *allow;
    // the reach query comes from the mouse-over dispatch site, which leaves ctx->player NULL once the
    // world is unloaded; the held-item read below goes straight into a JNI field access
    if (!ctx->player)
        return 0;
    void *held = item_held_hotbar_slot(ctx->jvm, ctx->player);
    if (!held) {
        allow = &ghost_hand_allow_with_fists;
    } else {
        int item_id = java_call_static_item_get_id_from_item(ctx->jvm, held);
        switch (item_id) {
        case 0x100: case 0x101: case 0x102: case 0x10d: case 0x10e: case 0x10f: case 0x111: case 0x112: case 0x113: case 0x115: case 0x116: case 0x117: case 0x11c: case 0x11d: case 0x11e: case 0x167: allow = &ghost_hand_tools; break;
        case 0x10b: case 0x10c: case 0x110: case 0x114: case 0x11b: allow = &ghost_hand_swords; break;
        case 0x145: case 0x146: case 0x147: allow = &ghost_hand_bows; break;
        case 0x103: allow = &ghost_hand_flint_steel; break;
        case 0x1e: allow = &ghost_hand_cobweb; break;
        default: allow = &ghost_hand_other_items; break;
        }
    }
    if (*allow != 1)
        return 0;
    void *entity_other_player_mp = java_jni_is_instance_of_entity_other_player_mp(ctx->jvm, entity);
    if (!entity_other_player_mp)
        return ghost_hand_non_living_entities;
    unsigned fl = entity_flags_get(ctx->jvm, entity_other_player_mp);
    Setting *s = (fl & 5) ? &ghost_hand_friendlies_setting : &ghost_hand_enemies_setting;
    s = (fl & 2) ? &ghost_hand_bots_setting : s;
    return s->on;
}

extern "C" void ghost_hand_on_reach_query(char *s) {
    s[0x10] = 1;
}
