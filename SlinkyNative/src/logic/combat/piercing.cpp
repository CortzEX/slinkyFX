// Piercing - decide which of two entities an attack should hit.
//   piercing_decide_which_entity(ctx, e1, e2, inside_bb): 0 = e1 preferred, 1 = e2 preferred, 2 = no preference
//   piercing_should_pierce_blocks(ctx): true when "blocks" piercing is active
//   piercing_on_reach_query(hook): arm flag
#include "slinky/engine.h"

extern "C" unsigned char piercing_module_enabled;   // module enabled
extern "C" unsigned char piercing_require_inside_bb_value;   // "require_inside_bb"
extern "C" unsigned char piercing_require_weapon_value;   // "require_weapon"
extern "C" unsigned char piercing_non_players_value;   // "non_players"
extern "C" bool piercing_friendlies_value;   // "friendlies"
extern "C" bool piercing_bots_value;   // "bots"
extern "C" unsigned char piercing_enemies_value;   // "enemies"
extern "C" int piercing_sort_by_value;             // "sort_by"
extern "C" unsigned char piercing_blocks_value;   // "blocks"

FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(void *, java_jni_is_instance_of_entity_other_player_mp, (void *jvm, void *candidate));
FN(unsigned, entity_flags_get, (void *jvm, void *t));
FN(float, java_call_entity_living_base_get_health, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_entity_id, (void *jvm, void *entity));

static inline unsigned mix(unsigned x) {
    x = (x ^ (x >> 16)) * 0x45d9f3b;
    x = (x ^ (x >> 16)) * 0x45d9f3b;
    return x ^ (x >> 16);
}

extern "C" int piercing_decide_which_entity(Ctx *ctx, void *candidate, void *e2, unsigned char inside_bb) {
    int ret = 2;
    void *entity_living_base, *entity;
    unsigned fl;
    if (piercing_module_enabled != 1) goto done;
    if (!ctx->player) goto done;
    if (!inside_bb && piercing_require_inside_bb_value) goto done;
    if (piercing_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player)) goto done;
    entity = java_jni_is_instance_of_entity_other_player_mp(ctx->jvm, e2);
    if (!entity) {
        ret = 2 - piercing_non_players_value;
        goto done;
    }
    fl = entity_flags_get(ctx->jvm, entity);
    if ((piercing_friendlies_value & ((fl & 5) != 0)) || (piercing_bots_value & ((fl & 2) >> 1))) {
        ret = 1;
        goto done;
    }
    entity_living_base = java_jni_is_instance_of_entity_other_player_mp(ctx->jvm, candidate);
    if (!entity_living_base) {
        ret = 0;
        goto done;
    }
    if (piercing_enemies_value != 1) goto done;
    if (piercing_sort_by_value == 1) {
        float health = java_call_entity_living_base_get_health(ctx->jvm, entity_living_base);
        float entity_living_base_get_health = java_call_entity_living_base_get_health(ctx->jvm, entity);
        if (entity_living_base_get_health > health) { ret = 1; goto done; }
        if (health > entity_living_base_get_health) { ret = 0; goto done; }
    } else if (piercing_sort_by_value == 0) {
        int entity_living_base_hurt_time = java_get_entity_living_base_hurt_time(ctx->jvm, entity_living_base);
        int s2 = java_get_entity_living_base_hurt_time(ctx->jvm, entity);
        if (s2 > entity_living_base_hurt_time) { ret = 1; goto done; }
        if (s2 < entity_living_base_hurt_time) { ret = 0; goto done; }
    }
    {
        unsigned h1 = mix(java_get_entity_ticks_existed(ctx->jvm, entity_living_base) + java_get_entity_entity_id(ctx->jvm, entity_living_base));
        unsigned h2 = mix(java_get_entity_ticks_existed(ctx->jvm, entity) + java_get_entity_entity_id(ctx->jvm, entity));
        ret = (int)h2 <= (int)h1;
    }
done:
    return ret;
}

extern "C" unsigned char piercing_should_pierce_blocks(Ctx *ctx) {
    unsigned char r = 0;
    if (piercing_module_enabled != 1 || !piercing_blocks_value)
        goto end;
    if (!ctx->player) {
        r = 0;
        goto end;
    }
    if (piercing_require_weapon_value == 1 && __builtin_expect(!is_holding_accepted_weapon(ctx->jvm, ctx->player), 1)) {
        r = 0;
        goto end;
    }
    r = 1;
end:

    return r;
}

extern "C" void piercing_on_reach_query(unsigned char *h) {
    h[0x10] = 1;
}
