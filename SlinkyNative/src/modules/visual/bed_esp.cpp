// Bed ESP — Shows bed defense through blocks with team colors.
// category: visual   descriptor: 0x2f2000   settings block: 0x2f2200
// functions: gui bed_esp_gui, register bed_esp_register
// implementation: src/logic/visual/bed_esp.cpp
#include "slinky/api.h"

S(str_main_in_bed_esp); // "Main"
S(str_customization_in_bed_esp); // "Customization"
S(str_range_in_bed_esp); // "range"
S(str_show_bed_defense); // "show_bed_defense"
S(str_defence_hud); // "defence_hud"
S(str_opacity_in_bed_esp); // "opacity"
S(str_rounding_in_bed_esp); // "rounding"
S(str_scale_in_bed_esp); // "scale"
S(str_auto_scale_in_bed_esp); // "auto_scale"
S(str_show_block_count); // "show_block_count"
S(str_block_count_font); // "block_count_font"
S(str_exposed_outline); // "exposed_outline"
S(str_exposed_color); // "exposed_color"
S(str_outline_exposed_layers); // "outline_exposed_layers"
S(str_bed_team_color_mode); // "bed_team_color_mode"

D(bed_esp_range_setting); // setting num "range"
D(bed_esp_show_bed_defense_setting); // setting bool "show_bed_defense"
D(bed_esp_defence_hud_setting); // setting bool "defence_hud"
D(bed_esp_opacity_setting); // setting num "opacity"
D(bed_esp_rounding_setting); // setting num "rounding"
D(bed_esp_scale_setting); // setting num "scale"
D(bed_esp_auto_scale_setting); // setting bool "auto_scale"
D(bed_esp_show_block_count_setting); // setting bool "show_block_count"
D(bed_esp_block_count_font_setting); // setting font "block_count_font"
D(bed_esp_exposed_outline_setting); // setting bool "exposed_outline"
D(bed_esp_exposed_color_setting); // setting key "exposed_color"
D(bed_esp_outline_exposed_layers_setting); // setting bool "outline_exposed_layers"
D(bed_esp_bed_team_color_mode_setting); // setting enum "bed_team_color_mode"
extern "C" unsigned bed_esp_bed_team_color_mode; // current value of setting "bed_team_color_mode" (+0x30)
D(bed_esp_enum_option_labels_1); // enum option labels (const char *[])

extern "C" void bed_esp_gui(void *gui, void *window) {
    W_label(gui, window, str_main_in_bed_esp);
    W_slider(window, gui, bed_esp_range_setting);
    W_bool(window, gui, bed_esp_show_bed_defense_setting);
    W_label(gui, window, str_customization_in_bed_esp);
    W_bool(window, gui, bed_esp_defence_hud_setting);
    if (*(unsigned char *)bed_esp_defence_hud_setting) {
        W_slider(window, gui, bed_esp_opacity_setting);
        W_slider(window, gui, bed_esp_rounding_setting);
        W_slider(window, gui, bed_esp_scale_setting);
        W_bool(window, gui, bed_esp_auto_scale_setting);
        W_bool(window, gui, bed_esp_show_block_count_setting);
        if (*(unsigned char *)bed_esp_show_block_count_setting) {
            W_items(window, gui, bed_esp_block_count_font_setting);
        }
    }
    W_bool(window, gui, bed_esp_exposed_outline_setting);
    if (*(unsigned char *)bed_esp_exposed_outline_setting) {
        W_color(window, gui, bed_esp_exposed_color_setting);
        W_bool(window, gui, bed_esp_outline_exposed_layers_setting);
    }
    W_enumw(window, gui, bed_esp_bed_team_color_mode_setting);
}

extern "C" void bed_esp_register(void *module) {
    R_num(module, str_range_in_bed_esp, bed_esp_range_setting);
    R_bool(module, str_show_bed_defense, bed_esp_show_bed_defense_setting);
    R_bool(module, str_defence_hud, bed_esp_defence_hud_setting);
    R_num(module, str_opacity_in_bed_esp, bed_esp_opacity_setting);
    R_num(module, str_rounding_in_bed_esp, bed_esp_rounding_setting);
    R_num(module, str_scale_in_bed_esp, bed_esp_scale_setting);
    R_bool(module, str_auto_scale_in_bed_esp, bed_esp_auto_scale_setting);
    R_bool(module, str_show_block_count, bed_esp_show_block_count_setting);
    R_items(module, str_block_count_font, bed_esp_block_count_font_setting);
    R_bool(module, str_exposed_outline, bed_esp_exposed_outline_setting);
    R_key(module, str_exposed_color, bed_esp_exposed_color_setting);
    R_bool(module, str_outline_exposed_layers, bed_esp_outline_exposed_layers_setting);
    R_enum(module, str_bed_team_color_mode, bed_esp_bed_team_color_mode_setting);
}