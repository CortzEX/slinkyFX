// Sprint - descriptor +0x158 / +0x198.
// +0x158: keep the sprint key state in sync (and re-assert sprint on the player)
//         unless a GUI is open and "while_in_gui" is off.
// +0x198: boost motion for the directions allowed by the settings.
#include "slinky/engine.h"
#define H0(c) (c)
#define H1(c) __builtin_expect(!!(c), 1)
#define H2(c) (c)

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char sprint_module_descriptor[];
extern "C" unsigned char sprint_while_in_gui_value;   // "while_in_gui"
extern "C" unsigned char sprint_while_using_item_value;   // "while_using_item"
extern "C" unsigned char sprint_while_backwards_value;   // "while_backwards"
extern "C" unsigned char sprint_while_sideways_value;   // "while_sideways"
extern "C" unsigned char sprint_sprint_currently_forced;   // sprint currently forced

struct SprintState { char padding_00[8]; Ctx *ctx; };
struct MoveState { char padding_00[8]; Ctx *ctx; float strafe; float forward; float speed; };  // +0x10/+0x14/+0x18

FN(void *, get_current_screen, (void *jvm, void *object));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_key_17_or_8, (void *jvm, void *game_settings));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(unsigned char, java_call_entity_is_sprinting, (void *jvm, void *entity));
FN(void, java_call_entity_set_sprinting, (void *jvm, void *entity, bool on));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));

extern "C" void sprint_on_player_update(SprintState *s) {
    Ctx *ctx = s->ctx;
    unsigned char allowed = ((get_current_screen(ctx->jvm, ctx->minecraft) == 0) | sprint_while_in_gui_value) & 1;
    java_set_key_binding_pressed(ctx->jvm, java_get_game_settings_key_17_or_8(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)), allowed);
    void *player = ctx->player;
    if (player) {
        if (!allowed) {
            if (sprint_sprint_currently_forced == 1)
                java_call_entity_set_sprinting(ctx->jvm, player, false);
            return;
        }
        if (java_call_entity_is_sprinting(ctx->jvm, player))
            java_call_entity_set_sprinting(ctx->jvm, ctx->player, true);
    }
    sprint_sprint_currently_forced = allowed;
}

extern "C" void sprint_on_toggle(EvHook *hook) {
    if (hook->ev != sprint_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    if (hook->on) {
        sprint_sprint_currently_forced = 0;
        return;
    }
    // This handler rewrites the sprint binding every tick and unregisters on disable, so the key it
    // held down stays "pressed" and you keep auto-sprinting until you tap the sprint key for real.
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *key = java_get_game_settings_key_17_or_8(ctx->jvm, obj);
    java_set_key_binding_pressed(ctx->jvm, key, java_get_key_binding_key_code(ctx->jvm, key));
    if (sprint_sprint_currently_forced == 1 && ctx->player)
        java_call_entity_set_sprinting(ctx->jvm, ctx->player, false);
    sprint_sprint_currently_forced = 0;
}

extern "C" void sprint_on_movement_input(MoveState *s) {
    Ctx *ctx = s->ctx;
    if (s->forward == 0.0f && s->strafe == 0.0f)
        return;
    unsigned char using_item = java_call_entity_player_is_using_item(ctx->jvm, ctx->player);
    if (using_item && !sprint_while_using_item_value)
        return;
    if (s->forward < 0.0f) {
        if (H0(!sprint_while_backwards_value))
            return;
    } else if (s->forward == 0.0f) {
        if (s->strafe != 0.0f) {
            if (H1(!sprint_while_sideways_value))
                return;
        } else if (H2(!using_item))
            return;
    } else if (!using_item)
        return;
    s->speed = (float)(s->speed * 1.3);
    java_call_entity_set_sprinting(ctx->jvm, ctx->player, true);
}
