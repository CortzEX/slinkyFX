// Stasis — Freezes your position using packet holding (Hypixel-compatible).
// category: utility   descriptor: 0x2ec800   settings block: 0x2ec9f8
// functions: gui stasis_gui, register stasis_register, status stasis_status
// implementation: src/logic/utility/stasis.cpp
#include "slinky/api.h"

S(str_main_in_stasis); // "Main"
S(str_disable_on); // "Disable on"
S(str_direction_in_stasis); // "direction"
S(str_max_duration); // "max_duration"
S(str_disable_on_attack); // "disable_on_attack"
S(str_disable_on_block_interact); // "disable_on_block_interact"
S(str_disable_on_block_dig); // "disable_on_block_dig"
S(str_hypixel_mode); // "hypixel_mode"
S(str_shift_place_through_walls); // "shift_place_through_walls"
S(str_s_in_stasis); // "%s"

D(stasis_disable_on_attack_setting); // setting bool "disable_on_attack"
D(stasis_max_duration_setting); // setting num "max_duration"
D(stasis_enum_option_labels_1); // enum option labels (const char *[])
D(stasis_direction_setting); // setting enum "direction"
extern "C" unsigned stasis_direction; // current value of setting "direction" (+0x30)
D(stasis_disable_on_block_dig_setting); // setting bool "disable_on_block_dig"
D(stasis_disable_on_block_interact_setting); // setting bool "disable_on_block_interact"
D(stasis_hypixel_mode_setting); // setting bool "hypixel_mode"
D(stasis_shift_place_through_walls_setting); // setting bool "shift_place_through_walls"
extern "C" unsigned char stasis_shift_place_through_walls_visible; // visibility flag

extern "C" void stasis_gui(void *gui, void *window) {
    W_label(gui, window, str_main_in_stasis);
    W_enumw(window, gui, stasis_direction_setting);
    W_slider(window, gui, stasis_max_duration_setting);
    W_bool(window, gui, stasis_hypixel_mode_setting);
    if (stasis_shift_place_through_walls_visible) {
        W_bool(window, gui, stasis_shift_place_through_walls_setting);
    }
    W_label(gui, window, str_disable_on);
    W_bool(window, gui, stasis_disable_on_attack_setting);
    W_bool(window, gui, stasis_disable_on_block_interact_setting);
    W_bool(window, gui, stasis_disable_on_block_dig_setting);
}

extern "C" void stasis_register(void *module) {
    R_enum(module, str_direction_in_stasis, stasis_direction_setting);
    R_num(module, str_max_duration, stasis_max_duration_setting);
    R_bool(module, str_disable_on_attack, stasis_disable_on_attack_setting);
    R_bool(module, str_disable_on_block_interact, stasis_disable_on_block_interact_setting);
    R_bool(module, str_disable_on_block_dig, stasis_disable_on_block_dig_setting);
    R_bool(module, str_hypixel_mode, stasis_hypixel_mode_setting);
    R_bool(module, str_shift_place_through_walls, stasis_shift_place_through_walls_setting);
}

extern "C" void stasis_status(void *self) {
    log_fmt(self, 0x20, str_s_in_stasis, ((const char **)stasis_enum_option_labels_1)[stasis_direction]);
}