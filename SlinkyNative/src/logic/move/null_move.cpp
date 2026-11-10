// Null Move - descriptor +0x148: resolve each pair of opposite movement keys like
// a snap-tap / SOCD keyboard - the direction pressed last wins, and letting go of
// it hands the axis straight back to the key that is still held.
#include "slinky/engine.h"

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char null_move_module_descriptor[];
extern "C" int null_move_key_state_0;   // forward / back axis
extern "C" int null_move_key_state_1;   // left / right axis
extern "C" int null_move_key_state_2;   // unused
extern "C" int null_move_key_state_3;   // unused

FN(unsigned char, is_in_game, (void *jvm, void *object));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_left_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_back_key, (void *jvm, void *game_settings));
FN(void *, java_get_game_settings_right_key, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));

// axis state word: winner in bits 0-1, the key this module released in bits 2-3,
// the physical key state sampled on the previous dispatch in bits 4-5
enum { KEY_NONE = 0, KEY_A = 1, KEY_B = 2 };

static void resolve_axis(int *state, Ctx *ctx, void *key_a, void *key_b) {
    int s = *state;
    int winner = s & 3;
    int suppressed = (s >> 2) & 3;
    unsigned char a_down = java_get_key_binding_key_code(ctx->jvm, key_a);
    unsigned char b_down = java_get_key_binding_key_code(ctx->jvm, key_b);
    unsigned char a_new = a_down && !((s >> 4) & 1);
    unsigned char b_new = b_down && !((s >> 5) & 1);
    if (a_down && b_down) {
        if (a_new & (b_new ^ 1))
            winner = KEY_A;
        else if (b_new & (a_new ^ 1))
            winner = KEY_B;
        else if (a_new & b_new)
            winner = KEY_NONE;   // both went down in the same frame: leave vanilla's cancellation
        // otherwise the key pressed last is still the one held, so the winner stands
        if (winner == KEY_A) {
            if (suppressed == KEY_A)
                java_set_key_binding_pressed(ctx->jvm, key_a, 1);
            java_set_key_binding_pressed(ctx->jvm, key_b, 0);
            suppressed = KEY_B;
        } else if (winner == KEY_B) {
            if (suppressed == KEY_B)
                java_set_key_binding_pressed(ctx->jvm, key_b, 1);
            java_set_key_binding_pressed(ctx->jvm, key_a, 0);
            suppressed = KEY_A;
        } else {
            if (suppressed == KEY_A)
                java_set_key_binding_pressed(ctx->jvm, key_a, 1);
            else if (suppressed == KEY_B)
                java_set_key_binding_pressed(ctx->jvm, key_b, 1);
            suppressed = KEY_NONE;
        }
    } else if (a_down || b_down) {
        winner = a_down ? KEY_A : KEY_B;
        // the winner was let go, so the key it had been hiding takes over again at once
        if (suppressed == KEY_A)
            java_set_key_binding_pressed(ctx->jvm, key_a, a_down);
        else if (suppressed == KEY_B)
            java_set_key_binding_pressed(ctx->jvm, key_b, b_down);
        suppressed = KEY_NONE;
    } else {
        winner = KEY_NONE;
        if (suppressed == KEY_A)
            java_set_key_binding_pressed(ctx->jvm, key_a, 0);
        else if (suppressed == KEY_B)
            java_set_key_binding_pressed(ctx->jvm, key_b, 0);
        suppressed = KEY_NONE;
    }
    *state = winner | (suppressed << 2) | ((int)a_down << 4) | ((int)b_down << 5);
}

extern "C" void null_move_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (!is_in_game(ctx->jvm, ctx->minecraft))
        return;
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *fwd = java_get_game_settings_forward_key(ctx->jvm, obj);
    void *back = java_get_game_settings_back_key(ctx->jvm, obj);
    void *left = java_get_game_settings_left_key(ctx->jvm, obj);
    void *right = java_get_game_settings_right_key(ctx->jvm, obj);
    // the tick event is frame-driven, so the same key state can be sampled more than once per game
    // tick; reading the physical keys keeps another module's synthetic hold out of the decision
    resolve_axis(&null_move_key_state_0, ctx, fwd, back);
    resolve_axis(&null_move_key_state_1, ctx, left, right);
}

// the four bindings this module re-writes, put back in step with the real keyboard
static void resync_move_keys(Ctx *ctx) {
    void *obj = get_game_settings(ctx->jvm, ctx->minecraft);
    void *keys[4] = { java_get_game_settings_forward_key(ctx->jvm, obj),
                      java_get_game_settings_left_key(ctx->jvm, obj),
                      java_get_game_settings_back_key(ctx->jvm, obj),
                      java_get_game_settings_right_key(ctx->jvm, obj) };
    for (int i = 0; i < 4; i++)
        java_set_key_binding_pressed(ctx->jvm, keys[i], java_get_key_binding_key_code(ctx->jvm, keys[i]));
}

extern "C" void null_move_on_toggle(EvHook *hook) {
    if (hook->on || hook->ev != null_move_module_descriptor)
        return;
    Ctx *ctx = hook->ctx;
    // disabling unregisters the tick handler, so a key this module forced down (or up) would keep
    // that state until the user physically tapped it - you could not walk backwards or strafe
    resync_move_keys(ctx);
    null_move_key_state_0 = 0;
    null_move_key_state_1 = 0;
    null_move_key_state_2 = 0;
    null_move_key_state_3 = 0;
}
