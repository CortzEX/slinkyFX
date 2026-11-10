// Chest Stealer — Automatically loots chests for you.
// category: utility   descriptor: 0x2f5288   settings block: 0x2f5480
// functions: gui chest_stealer_gui, register chest_stealer_register
// implementation: src/logic/utility/chest_stealer.cpp
#include "slinky/api.h"

S(str_main_in_chest_stealer); // "Main"
S(str_delay_after_open_in_chest_stealer); // "delay_after_open"
S(str_delay_before_close_in_chest_stealer); // "delay_before_close"
S(str_speed_in_chest_stealer); // "speed"
S(str_smart_speed_in_chest_stealer); // "smart_speed"
S(str_require_vanilla_title); // "require_vanilla_title"

D(chest_stealer_delay_after_open_setting); // setting num "delay_after_open"
D(chest_stealer_require_vanilla_title_setting); // setting bool "require_vanilla_title"
D(chest_stealer_delay_before_close_setting); // setting num "delay_before_close"
D(chest_stealer_speed_setting); // setting num "speed"
D(chest_stealer_smart_speed_setting); // setting bool "smart_speed"

extern "C" void chest_stealer_gui(void *gui, void *window) {
    W_label(gui, window, str_main_in_chest_stealer);
    W_slider(window, gui, chest_stealer_delay_after_open_setting);
    W_slider(window, gui, chest_stealer_delay_before_close_setting);
    W_slider(window, gui, chest_stealer_speed_setting);
    W_bool(window, gui, chest_stealer_smart_speed_setting);
    W_bool(window, gui, chest_stealer_require_vanilla_title_setting);
}

extern "C" void chest_stealer_register(void *module) {
    R_num(module, str_delay_after_open_in_chest_stealer, chest_stealer_delay_after_open_setting);
    R_num(module, str_delay_before_close_in_chest_stealer, chest_stealer_delay_before_close_setting);
    R_num(module, str_speed_in_chest_stealer, chest_stealer_speed_setting);
    R_bool(module, str_smart_speed_in_chest_stealer, chest_stealer_smart_speed_setting);
    R_bool(module, str_require_vanilla_title, chest_stealer_require_vanilla_title_setting);
}
