// Anti Debuff - descriptor +0x168: strip blindness / nausea effects.
#include "slinky/engine.h"

extern "C" unsigned char anti_debuff_blindness_value;  // "blindness"
extern "C" unsigned char anti_debuff_nausea_value;  // "nausea"

FN(void *, java_potion_potion_types, (void *jvm, int effect_id));
FN(void *, java_call_entity_living_base_get_active_potion_effect, (void *jvm, void *entity_living_base, void *effect));
FN(void, java_set_potion_effect_duration, (void *jvm, void *effect, int value));
FN(void, java_set_entity_player_sp_time_in_portal, (void *jvm, void *entity_player_sp, float value));
FN(void, java_set_entity_player_sp_prev_time_in_portal, (void *jvm, void *entity_player_sp, float value));

extern "C" void anti_debuff_on_late_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    // event 0x12 (updateEntities) dispatches without checking the player, and a NULL jobject passed to a
    // JNI field access faults
    if (!ctx->player)
        return;
    if (anti_debuff_blindness_value == 1) {
        void *effect = java_call_entity_living_base_get_active_potion_effect(ctx->jvm, ctx->player, java_potion_potion_types(ctx->jvm, 15));
        if (effect)
            java_set_potion_effect_duration(ctx->jvm, effect, 0);
    }
    if (anti_debuff_nausea_value == 1) {
        java_set_entity_player_sp_time_in_portal(ctx->jvm, ctx->player, 0.0f);
        java_set_entity_player_sp_prev_time_in_portal(ctx->jvm, ctx->player, 0.0f);
        return;
    }

}
