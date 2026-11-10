// Auto Block - hold the use (block) key with a sword while fighting, with
// optional "lag" (packet hold) windows and a client-side fake block animation.
//   +0x48 tick, +0x78 packet hook, +0x98 disable, +0x128 attack hook, +0x168 render
#include "slinky/engine.h"

struct AttackHook { char padding_00[8]; Ctx *ctx; unsigned char phase; unsigned char cancel; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct PktHook { char padding_00[0x24]; int verdict; };
struct Target { void *padding_00; void *ent; double dist2; };
struct Mouse { unsigned char padding_00, lmb, rmb; };

extern "C" float auto_block_max_hold_time_value;            // "max_hold_time"
extern "C" float auto_block_lag_chance_value;            // "lag_chance"
extern "C" float auto_block_lag_duration_value;            // "lag_duration"
extern "C" float auto_block_max_hurt_time_value;            // "max_hurt_time"
extern "C" unsigned char auto_block_require_left_mouse_pressed_value;    // "require_left_mouse_pressed"
extern "C" unsigned char auto_block_require_right_mouse_pressed_value;    // "require_right_mouse_pressed"
extern "C" unsigned char auto_block_require_damage_value;    // "require_damage"
extern "C" unsigned char auto_block_fake_block_animation;    // "fake_block_animation"
extern "C" unsigned char auto_block_fake_block_animation_require_target_value;    // "fake_block_animation_require_target"
extern "C" float auto_block_range_value;            // "range"
extern "C" unsigned char auto_block_lag_flush_attack_value;    // "lag_flush_attack"
extern "C" unsigned char auto_block_lag_reblock_value;    // "lag_reblock"
extern "C" char auto_block_module_descriptor[];           // Auto Block module descriptor
extern "C" int auto_block_re_block_cooldown;              // re-block cooldown
extern "C" int auto_block_ticks_held;              // ticks held
extern "C" unsigned auto_block_ticks_since_target_hit;         // ticks since target hit
extern "C" int auto_block_last_target_id;              // last target id
extern "C" unsigned char auto_block_fake_animation_this_tick;    // fake animation this tick
extern "C" unsigned char auto_block_animation_started;    // animation started
extern "C" unsigned char auto_block_right_mouse_required;    // right mouse required
extern "C" int auto_block_lag_ticks_left;              // lag ticks left (atomic)
extern "C" unsigned char auto_block_blocking;    // blocking
extern "C" unsigned char auto_block_use_key_forced;    // use key forced
extern "C" int auto_block_animation_counter;              // animation counter

FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void, java_set_entity_player_item_in_use_count, (void *jvm, void *entity_player, int n));
FN(int, java_get_entity_living_base_hurt_time, (void *jvm, void *entity_living_base));
FN(void *, item_held_hotbar_slot, (void *jvm, void *slot));
FN(unsigned char, item_sword, (void *jvm, void *item));
FN(unsigned char, timer_range_is_delay_pending, ());
FN(unsigned char, random_chance, (float chance));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_use_item_key, (void *jvm, void *game_settings));
FN(unsigned char, java_get_key_binding_key_code, (void *jvm, void *key_binding));
FN(void, java_set_key_binding_pressed, (void *jvm, void *key_binding, unsigned char on));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(int, java_get_entity_living_base_last_attacker_time, (void *jvm, void *entity_living_base));
FN(int, java_get_entity_entity_id, (void *jvm, void *entity));
FN(unsigned char, input_down_or_recent, (char *keys, int code));   // engine/jni/silent_rotation_and_chat_colors.cpp

#define AXCHG(x, v) __atomic_exchange_n(&(x), (v), __ATOMIC_SEQ_CST)
#define ALOAD(x) __atomic_load_n(&(x), __ATOMIC_SEQ_CST)

static inline __attribute__((always_inline)) int rand_round(float v) {
    int whole = (int)v;
    return random_chance(v - (float)(int)v) + whole;
}

static inline __attribute__((always_inline)) void *use_key(Ctx *ctx) {
    return java_get_game_settings_use_item_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft));
}

extern "C" void auto_block_on_tick(Hook *hook) {
    auto_block_fake_animation_this_tick = 0;
    Ctx *ctx = hook->ctx;
    if (auto_block_animation_started == 1) {
        if (ctx->player && !java_call_entity_player_is_using_item(ctx->jvm, ctx->player))
            java_set_entity_player_item_in_use_count(ctx->jvm, ctx->player, 0);
        auto_block_animation_started = 0;
    }
    if (auto_block_re_block_cooldown != 0)
        auto_block_re_block_cooldown -= 1;
    // ctx->player can still be null while inGameHasFocus is set (respawn / world transition) and every
    // accessor below reads a field straight off it, so take the same release-and-stop-lag path.
    if (!ctx->in_game || !ctx->player) {
        if (auto_block_use_key_forced == 1) {
            void *k = use_key(ctx);
            java_set_key_binding_pressed(ctx->jvm, k, java_get_key_binding_key_code(ctx->jvm, k));
            auto_block_use_key_forced = 0;
        }
        auto_block_right_mouse_required = 0;
        goto stop_lag;
    }
    {
        int entity_living_base_hurt_time = java_get_entity_living_base_hurt_time(ctx->jvm, ctx->player);
        void *jvm = ctx->jvm;
        unsigned char sword = item_sword(jvm, item_held_hotbar_slot(jvm, ctx->player));
        auto_block_right_mouse_required = 0;
        unsigned char want = 0, forced = 0, fake = 0;
        if (!sword) {
            if (auto_block_use_key_forced != 1)
                goto publish;
            goto release;
        }
        // A hardware / AHK / mouse-macro click is released inside one frame, so a plain poll of the key
        // table would read 0 on every tick and auto-block would never engage outside the built-in clicker.
        if (auto_block_require_left_mouse_pressed_value == 1 && !input_down_or_recent((char *)ctx->key_state_table, 1))
            goto release;
        if (timer_range_is_delay_pending())
            goto release;
        {
            unsigned char rmb = auto_block_require_right_mouse_pressed_value;
            auto_block_right_mouse_required = rmb;
            // Requiring the button means held now or clicked this instant: a hardware, AHK or mouse-macro press is
            // released inside one frame, so the plain level would never satisfy it. Requiring nothing keeps the
            // level read, which is the question vanilla's own blocking asks.
            if (rmb ? !input_down_or_recent((char *)ctx->key_state_table, 2) : ((Mouse *)ctx->key_state_table)->rmb)
                goto release;
        }
        want = 1;
        if (entity_living_base_hurt_time == 0 && auto_block_require_damage_value)
            goto release;
        if (entity_living_base_hurt_time > rand_round(auto_block_max_hurt_time_value))
            goto release;
        {
            unsigned char blocking = auto_block_blocking;
            want = 1;
            forced = 1;
            // Only a block the module is actually holding takes the key. This line used to write `blocking` every
            // tick, so a module that was not blocking wrote 0 - and with "require right mouse pressed" that emptied
            // the use key the player was physically holding, before the game could turn the click into a chest, an
            // NPC panel or any other interaction.
            if (blocking == 1) {
                void *k = use_key(ctx);
                java_set_key_binding_pressed(ctx->jvm, k, 1);
                java_increment_key_binding_press_time(ctx->jvm, k);
            }
            goto forced_set;
        }
release:
        {
            // Hand back only the key the module took, and hand it back the way the player left it. The old code
            // forced the key up whenever require-right-mouse was on, so on every tick it was not blocking it
            // swallowed the player's own right click: chests, NPC panels, doors and everything else that has to
            // reach the game's interact path stayed dead while that setting was enabled.
            if (auto_block_use_key_forced == 1) {
                void *k = use_key(ctx);
                java_set_key_binding_pressed(ctx->jvm, k, java_get_key_binding_key_code(ctx->jvm, k));
            }
            forced = 0;
        }
forced_set:
        auto_block_use_key_forced = forced;
        if (want && auto_block_fake_block_animation) {
            fake = 1;
            if (auto_block_fake_block_animation_require_target_value == 1) {
                Target *target = (Target *)ctx->target;
                fake = target->ent ? (double)(auto_block_range_value * auto_block_range_value) > target->dist2 : 0;
            }
        }
publish:
        auto_block_fake_animation_this_tick = fake;
        if (entity_living_base_hurt_time == 10)
            auto_block_re_block_cooldown = 0;
        unsigned ticks = auto_block_ticks_since_target_hit + 1;
        auto_block_ticks_since_target_hit = ticks;
        unsigned char fresh = 0;
        if (ticks >= 10) {
            fresh = java_get_entity_ticks_existed(ctx->jvm, ctx->player) == java_get_entity_living_base_last_attacker_time(ctx->jvm, ctx->player) + 1;
            if (fresh)
                auto_block_ticks_since_target_hit = 0;
        }
        void *entity = ((Target *)ctx->target)->ent;
        int entity_entity_id = entity ? java_get_entity_entity_id(ctx->jvm, entity) : 0;
        if (entity_entity_id != auto_block_last_target_id) {
            auto_block_last_target_id = entity_entity_id;
            auto_block_ticks_since_target_hit = 0;
            fresh = 1;
        }
        if (ALOAD(auto_block_lag_ticks_left) != 0) {
            if ((fresh & auto_block_lag_flush_attack_value) == 1) {
                AXCHG(auto_block_lag_ticks_left, 0);
                if (auto_block_lag_reblock_value != 1)
                    goto hold;
            } else {
                __atomic_fetch_sub(&auto_block_lag_ticks_left, 1, __ATOMIC_SEQ_CST);
                if (auto_block_lag_reblock_value != 1)
                    goto hold;
            }
            if (ALOAD(auto_block_lag_ticks_left) == 0) {
                auto_block_re_block_cooldown = 0;
            }
        }
hold:
        if (auto_block_blocking) {
            int s = auto_block_ticks_held + 1;
            float mx = auto_block_max_hold_time_value;
            int whole = (int)mx;

            auto_block_ticks_held = s;
            int lim = random_chance(mx - (float)(int)mx) + whole;
            unsigned char under = s < lim;

            if (forced & under)
                return;
            java_set_key_binding_pressed(ctx->jvm, use_key(ctx), 0);
            auto_block_use_key_forced = 1;
            if (random_chance(auto_block_lag_chance_value))
                AXCHG(auto_block_lag_ticks_left, rand_round(auto_block_lag_duration_value));
            auto_block_blocking = 0;
            return;
        }
        if (!(forced & (auto_block_re_block_cooldown == 0)))
            return;
        Target *t = (Target *)ctx->target;
        if (!t->ent || !((double)(auto_block_range_value * auto_block_range_value) > t->dist2))
            return;
        void *k = use_key(ctx);
        java_set_key_binding_pressed(ctx->jvm, k, 1);
        java_increment_key_binding_press_time(ctx->jvm, k);
        auto_block_use_key_forced = 1;
        auto_block_blocking = 1;
        auto_block_re_block_cooldown = 10;
        auto_block_ticks_held = 0;
    }
stop_lag:
    AXCHG(auto_block_lag_ticks_left, 0);
}

extern "C" void auto_block_on_packet_sent(PktHook *hook) {
    if (ALOAD(auto_block_lag_ticks_left) != 0)
        packet_hold(&hook->verdict);
}

extern "C" void auto_block_on_toggle(EvHook *hook) {
    if (hook->ev != auto_block_module_descriptor || hook->on)
        return;
    // The hold path leaves use_key_forced set while blocking is already cleared (auto_block.cpp `hold:`),
    // so keying the cleanup on `blocking` alone could hand a disabled module an owned use key.
    if (auto_block_use_key_forced == 1 || (auto_block_blocking & 1)) {
        if (auto_block_use_key_forced == 1) {
            Ctx *ctx = hook->ctx;
            void *k = use_key(ctx);
            java_set_key_binding_pressed(ctx->jvm, k, java_get_key_binding_key_code(ctx->jvm, k));
            auto_block_use_key_forced = 0;
        }
        auto_block_blocking = 0;
        auto_block_ticks_held = 0;
    }
    AXCHG(auto_block_lag_ticks_left, 0);
}

extern "C" void auto_block_on_attack(AttackHook *hook) {
    if (hook->phase == 0) {
        Ctx *ctx = hook->ctx;
        if (ctx->player && !item_sword(ctx->jvm, item_held_hotbar_slot(ctx->jvm, ctx->player)))
            AXCHG(auto_block_lag_ticks_left, 0);
        JOIN();
        // Event 0xe is both mouse buttons, and phase 0 is the right one. This used to cancel the game's right
        // click whenever "require right mouse pressed" was on and the module was not already blocking, so the
        // click never reached the interact path: chests, NPC panels, doors and everything else stayed dead.
        // Auto Block owns only the block it holds itself; it must not swallow the player's right click.
    }


    SHARED_EPILOG();
}

extern "C" void auto_block_on_late_tick(Hook *hook) {
    if (auto_block_fake_animation_this_tick) {
        Ctx *ctx = hook->ctx;
        if (ctx->player) {
            auto_block_animation_started = 1;
            int n = auto_block_animation_counter - 1;
            auto_block_animation_counter = n;
            java_set_entity_player_item_in_use_count(ctx->jvm, ctx->player, n);
        }
        return;
    }
    auto_block_animation_counter = 0x11940;
}
