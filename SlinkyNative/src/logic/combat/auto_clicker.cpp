// Auto Clicker - scheduled left clicks (attack key) with configurable CPS and
// randomised timing, released while a GUI is open.
#include "slinky/engine.h"

struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Range { float lo, hi;
#ifdef __linux__
    ~Range() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
extern "C" Rot auto_clicker_rotation_at_last_tick;              // rotation at the last tick
extern "C" unsigned char auto_clicker_attack_bound_mouse;    // attack bound to mouse (-99 = button 1)
extern "C" int auto_clicker_hit_type_under_crosshair;              // hit type under the crosshair
extern "C" unsigned char *const ptr_to_no_hit_delay_module_descriptor;   // client state (+0x14: screen open)

extern "C" unsigned char auto_clicker_burst_setting;    // "burst"
extern "C" char auto_clicker_rng_states[], auto_clicker_rng_states_second[], auto_clicker_rng_states_third[];   // rng states
extern "C" int auto_clicker_burst_clicks_left;              // burst clicks left
extern "C" double offset_0_02, offset_0_04, walk_period_300, walk_period_120_in_auto_clicker, factor_0_01_in_auto_clicker;
extern "C" float random_range_bound_1_in_auto_clicker_a, range_low_0_03, random_range_bound_0_06_in_auto_clicker, range_low_0_12, random_range_bound_0_15_in_auto_clicker_a;
FN(float, gauss_random, (void *st));
FN(Range, rot_make_floats, (float lo, float hi));
FN(unsigned char, walk_should_change, (void *state, Range r, double p));
FN(float, walk_update, (void *state, Range r, double p));
FN(float, random_rotation, (Range r));
extern "C" double auto_clicker_time_until_next_click_edge;           // time until the next click edge
extern "C" double auto_clicker_last_edge_delay;           // last edge delay
extern "C" char auto_clicker_random_walk_7[], auto_clicker_random_walk_6[], auto_clicker_random_walk_5[], auto_clicker_random_walk_1[], auto_clicker_gauss_rng_state[],
                auto_clicker_random_walk_2[], auto_clicker_random_walk_3[], auto_clicker_random_walk_4[];          // rng states
extern "C" unsigned char auto_clicker_lmb_was_down_gui;    // lmb was down in the GUI
extern "C" unsigned char auto_clicker_double_click_pending;    // double-click pending
extern "C" int auto_clicker_last_inventory_slot_clicked;              // last inventory slot clicked
extern "C" unsigned char auto_clicker_allow_inventory;    // "inventory_fill"
extern "C" unsigned char auto_clicker_inventory_random_delay_setting;    // "inventory_random_delay"
extern "C" unsigned char auto_clicker_break_blocks_setting;    // "break_blocks"
extern "C" unsigned char auto_clicker_require_weapon_value;    // "require_weapon"
extern "C" unsigned char auto_clicker_pause_when_using_setting;    // "pause_when_using"
extern "C" float auto_clicker_cps_setting;            // "cps"
extern "C" unsigned char auto_clicker_randomize_setting;    // "randomize"
extern "C" unsigned auto_clicker_pattern;         // "mode"
extern "C" double threshold_neg_0_2, hundredth, walk_period_10, auto_clicker_minimum_delay_seconds, auto_clicker_inventory_delay_seconds, walk_period_30,
                  walk_period_120, walk_period_5_in_auto_clicker, threshold_25, offset_0_08, one_in_auto_clicker, offset_neg_0_014;
extern "C" float random_range_bound_0_6_in_auto_clicker, random_range_bound_0_8, random_range_bound_0_04, random_range_bound_0_08, random_range_bound_0_06, random_range_bound_0_075,
                 random_range_bound_0_15_in_auto_clicker, random_range_bound_0_25, random_range_bound_0_2_in_auto_clicker, random_range_bound_0_3, auto_clicker_negative_jitter_bound, random_range_bound_0_1_in_auto_clicker,
                 random_range_bound_neg_0_05, random_range_bound_0_05, offset_1_in_auto_clicker, half_in_auto_clicker, divisor_20_in_auto_clicker, random_range_bound_0_94, random_range_bound_0_97;
struct Target { void *padding_00; void *ent; double dist2; };
struct TickHook { char padding_00[8]; Ctx *ctx; char padding_10[8]; double dt; };
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char auto_clicker_module_descriptor[];
FN(void *, get_timer, (void *jvm, void *timer));
FN(float, java_get_timer_timer_speed, (void *jvm, void *timer));
FN(void, post_synthetic_click, (int button, unsigned char down));
FN(float, rot_delta_length, (Rot a, Rot b));
FN(unsigned char, is_holding_accepted_weapon, (void *jvm, void *object));
FN(unsigned char, java_call_entity_player_is_using_item, (void *jvm, void *entity_player));
FN(void *, get_current_screen, (void *jvm, void *object));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(void *, java_get_gui_container_the_slot, (void *jvm, void *screen));
FN(void *, java_get_inventory_player_item_stack, (void *jvm, void *inv));
FN(int, java_get_slot_slot_number, (void *jvm, void *slot));
FN(void *, java_call_slot_get_stack, (void *jvm, void *slot));
FN(unsigned char, random_chance, (float chance));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_attack_key, (void *jvm, void *game_settings));
FN(int, get_event_key, (void *jvm, void *k));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(void, java_set_minecraft_left_click_counter, (void *jvm, void *minecraft, int y));
FN(unsigned char, input_down_or_recent, (char *keys, int code));   // engine/jni/silent_rotation_and_chat_colors.cpp
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *moving_object_position));

static int auto_clicker_attack_key;

extern "C" void auto_clicker_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    auto_clicker_attack_key = get_event_key(ctx->jvm, java_get_game_settings_attack_key(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)));
    auto_clicker_attack_bound_mouse = auto_clicker_attack_key == -99;
    if (ctx->in_game != 1 || !ctx->player)
        return;
    auto_clicker_rotation_at_last_tick = get_entity_rotation(ctx->jvm, ctx->player);
    if (ptr_to_no_hit_delay_module_descriptor[0x14])
        return;
    java_set_minecraft_left_click_counter(ctx->jvm, ctx->minecraft, 0);
}

extern "C" void auto_clicker_on_player_update(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game == 1) {
        void *minecraft = java_get_minecraft_object_mouse_over(ctx->jvm, ctx->minecraft);
        auto_clicker_hit_type_under_crosshair = minecraft ? java_get_moving_object_position_type_ordinal(ctx->jvm, minecraft) : 0;
    }
}

// extra "burst" clicks: returns true (and pushes *delay out) while a burst is running
extern "C" unsigned char auto_clicker_extra_burst_clicks(float *delay, float base) {
    if (auto_clicker_burst_setting != 1) {
        auto_clicker_burst_clicks_left = 0;     // turning it off mid-burst must end the burst, not just stop starting one
        return 0;
    }
    int n = auto_clicker_burst_clicks_left;
    if (n <= 0) {
        double bd = base;
        Range r = {(float)(offset_0_02 + bd), (float)(bd + offset_0_04)};

        char *st = auto_clicker_rng_states;

        if (!walk_should_change(st, r, walk_period_300))
            return 0;
        float v = random_rotation(rot_make_floats(0.0f, random_range_bound_1_in_auto_clicker_a));
        float lo = range_low_0_03;

        Range r1 = rot_make_floats(lo, random_range_bound_0_06_in_auto_clicker);
        char *st1 = auto_clicker_rng_states_second;

        n = 3;
        if (!(walk_update(st1, r1, walk_period_120_in_auto_clicker) > v)) {
            lo = range_low_0_12;

            Range r2 = rot_make_floats(lo, random_range_bound_0_15_in_auto_clicker_a);
            n = (walk_update(auto_clicker_rng_states_second, r2, walk_period_120_in_auto_clicker) > v) + 1;
        }
    }
    auto_clicker_burst_clicks_left = n - 1;
    double add = (double)gauss_random(auto_clicker_rng_states_third) * factor_0_01_in_auto_clicker + offset_0_02;
    *delay = (float)((double)*delay + add);
    return 1;
}

// aim-change bonus: clicking slows down when the crosshair moves fast / no target in reach
static inline __attribute__((always_inline)) float aim_bonus(Ctx *ctx) {
    float x = rot_delta_length(get_entity_rotation(ctx->jvm, ctx->player), auto_clicker_rotation_at_last_tick) / divisor_20_in_auto_clicker + 0.0f;
    Target *target = (Target *)ctx->target;
    if (!target->ent || target->dist2 > threshold_25)
        x = (float)((double)x + offset_0_08);
    return x;
}

// tick: generate press / release edges of the attack button at the configured cps
extern "C" unsigned char auto_clicker_extra_burst_clicks(float *delay, float base);
extern "C" void auto_clicker_on_frame(TickHook *hook) {
    if (auto_clicker_attack_key != -100 && auto_clicker_attack_key != -99)
        return;   // auto clicker synthesises mouse buttons; no mouse attack bind, nothing to click
    Ctx *ctx = hook->ctx;
    double dt = hook->dt;
    double acc = auto_clicker_time_until_next_click_edge - (double)java_get_timer_timer_speed(ctx->jvm, get_timer(ctx->jvm, ctx->minecraft)) * dt;
    acc = threshold_neg_0_2 > acc ? threshold_neg_0_2 : acc;
    auto_clicker_time_until_next_click_edge = acc;
    if (!(0.0 >= acc))
        return;
    unsigned char *mouse = (unsigned char *)ctx->key_state_table;
    unsigned char held = mouse[auto_clicker_attack_bound_mouse + 1];
    unsigned char edge = (held & 2) >> 1;
    double reset = hundredth;
    for (;;) {
        if (ctx->in_game == 0 || !ctx->player)
            goto gui;
        if (!((held | edge) & 1))
            goto stop;
        if (auto_clicker_hit_type_under_crosshair == 1 && auto_clicker_break_blocks_setting)
            goto cancel;
        if (auto_clicker_require_weapon_value == 1 && !is_holding_accepted_weapon(ctx->jvm, ctx->player))
            goto cancel;
        if (auto_clicker_pause_when_using_setting == 1) {
            if (java_call_entity_player_is_using_item(ctx->jvm, ctx->player) || ((held & edge) & 1))
                goto cancel;
        } else if ((held & edge) & 1) {
            held = 1;
            goto release;
        }
        {
            int btn = auto_clicker_attack_bound_mouse ^ 1;
            if (held & 1) {
                post_synthetic_click(btn, 0);
                auto_clicker_time_until_next_click_edge = auto_clicker_last_edge_delay + auto_clicker_time_until_next_click_edge;
            } else {
                post_synthetic_click(btn, 1);
                float cps = auto_clicker_cps_setting;
                if (auto_clicker_randomize_setting == 1) {
                    float r15 = walk_update(auto_clicker_random_walk_1, rot_make_floats(random_range_bound_0_2_in_auto_clicker, random_range_bound_0_3), walk_period_30);
                    float k = auto_clicker_negative_jitter_bound;
                    float r6 = walk_update(auto_clicker_random_walk_2, rot_make_floats(k, random_range_bound_0_1_in_auto_clicker), walk_period_120);
                    float r7 = walk_update(auto_clicker_random_walk_3, rot_make_floats(k, random_range_bound_0_1_in_auto_clicker), walk_period_30);
                    float r0 = walk_update(auto_clicker_random_walk_4, rot_make_floats(random_range_bound_neg_0_05, random_range_bound_0_05), walk_period_5_in_auto_clicker);
                    cps *= r6 + offset_1_in_auto_clicker + r7 + r0;
                    cps *= gauss_random(auto_clicker_gauss_rng_state) * r15 + offset_1_in_auto_clicker;
                }
                float delay;
                if (auto_clicker_pattern == 1) {
                    if (auto_clicker_double_click_pending == 1) {
                        delay = __builtin_bit_cast(float, 0x3c656042u);
                        auto_clicker_double_click_pending = 0;
                    } else {
                        delay = (float)(one_in_auto_clicker / (double)cps + offset_neg_0_014);
                        if (!auto_clicker_extra_burst_clicks(&delay, aim_bonus(ctx)))
                            auto_clicker_double_click_pending = random_chance((float)walk_should_change(auto_clicker_random_walk_5, rot_make_floats(random_range_bound_0_94, random_range_bound_0_97), walk_period_30));
                    }
                } else {
                    delay = half_in_auto_clicker / cps;
                    auto_clicker_extra_burst_clicks(&delay, aim_bonus(ctx));
                }
                double dd = delay;
                auto_clicker_time_until_next_click_edge = auto_clicker_time_until_next_click_edge + dd;
                auto_clicker_last_edge_delay = dd;
            }
        }
        held ^= 1;
        edge ^= 1;
        if (0.0 < auto_clicker_time_until_next_click_edge)
            return;
    }
cancel:
    reset = 0.0;
    if (edge & 1) {
release:
        post_synthetic_click(auto_clicker_attack_bound_mouse ^ 1, (~held) & 1);
        reset = 0.0;
    }
stop:
    auto_clicker_time_until_next_click_edge = reset;
    auto_clicker_last_inventory_slot_clicked = -1;
    return;
gui:
    {
        if (edge & 1)
            post_synthetic_click(auto_clicker_attack_bound_mouse ^ 1, (~held) & 1);
        if (!get_current_screen(ctx->jvm, ctx->minecraft))
            goto stop;
        unsigned char *key_state_table = (unsigned char *)ctx->key_state_table;
        // Held now or clicked this instant: the level alone needs the button down across two polls, which only a
        // held mouse or the built-in clicker ever gives, so a macro or AHK user got no inventory clicking at all.
        unsigned char lmb = input_down_or_recent((char *)key_state_table, 1);
        double v;
        if (lmb == 1 && auto_clicker_lmb_was_down_gui == 1) {
            if (auto_clicker_allow_inventory != 1 || !ctx->player || (!(key_state_table[0x62] & 1) && !(key_state_table[0x63] & 1)))
                goto plain;
            void *jvm = ctx->jvm;
            void *scr = get_current_screen(jvm, ctx->minecraft);
            void *inv = java_get_entity_player_inventory(jvm, ctx->player);
            void *slot = java_get_gui_container_the_slot(jvm, scr);
            if (!slot || java_get_inventory_player_item_stack(jvm, inv))
                goto plain;
            int idx = java_get_slot_slot_number(jvm, slot);
            if (idx == auto_clicker_last_inventory_slot_clicked)
                goto plain;
            auto_clicker_last_inventory_slot_clicked = idx;
            if (!java_call_slot_get_stack(jvm, slot) && walk_should_change(auto_clicker_random_walk_6, rot_make_floats(random_range_bound_0_6_in_auto_clicker, random_range_bound_0_8), walk_period_10))
                goto plain;
            post_synthetic_click(1, 1);
            if (auto_clicker_inventory_random_delay_setting != 1) {
                v = auto_clicker_minimum_delay_seconds;
                goto add;
            }
            Range r = walk_should_change(auto_clicker_random_walk_7, rot_make_floats(random_range_bound_0_04, random_range_bound_0_08), walk_period_10)
                          ? rot_make_floats(random_range_bound_0_15_in_auto_clicker, random_range_bound_0_25) : rot_make_floats(random_range_bound_0_06, random_range_bound_0_075);
            v = random_rotation(r);
            goto add;
        }
        auto_clicker_lmb_was_down_gui = lmb == 1;
plain:
        v = auto_clicker_inventory_random_delay_setting ? auto_clicker_inventory_delay_seconds : auto_clicker_minimum_delay_seconds;
add:
        auto_clicker_time_until_next_click_edge = v + auto_clicker_time_until_next_click_edge;
    }
}

// Toggled off with a synthetic press still outstanding, the module never posted its release: the game kept
// attacking on its own and the button read as held for every require-mouse-pressed gate. Bit 1 of the state
// byte is the synthetic-press marker; the posted release clears both marker and held bits when the window
// procedure runs, so the table itself is left alone here.
extern "C" void auto_clicker_on_toggle(EvHook *hook) {
    if (hook->ev != auto_clicker_module_descriptor || hook->on)
        return;
    Ctx *ctx = hook->ctx;
    if (ctx->key_state_table && (((unsigned char *)ctx->key_state_table)[auto_clicker_attack_bound_mouse + 1] & 2))
        post_synthetic_click(auto_clicker_attack_bound_mouse ^ 1, 0);
    auto_clicker_time_until_next_click_edge = 0.0;
    auto_clicker_last_edge_delay = 0.0;
    auto_clicker_burst_clicks_left = 0;
    auto_clicker_double_click_pending = 0;
    auto_clicker_lmb_was_down_gui = 0;
    auto_clicker_last_inventory_slot_clicked = -1;
}
