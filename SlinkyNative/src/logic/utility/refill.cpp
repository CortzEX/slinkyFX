// Refill - move selected items into the hotbar and keep them stacked.
#include "slinky/engine.h"
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char refill_module_descriptor[];           // Refill module descriptor
extern "C" unsigned char refill_soup_class_offset;    // soup class offset
extern "C" unsigned char refill_refill_potion;    // potions enabled
extern "C" unsigned char refill_non_health_potion_class_offset;    // non-health potion class offset
extern "C" int refill_pending_action;              // state
extern "C" unsigned char refill_slot_classes[27];   // slot classes

struct InvHook { char padding_00[8]; Ctx *ctx; unsigned char handled; };
extern "C" float refill_close_delay_setting;            // "close_delay"
extern "C" float refill_open_delay_setting;            // "open_delay"
extern "C" float refill_speed_value;            // "speed"
extern "C" unsigned char refill_distance_based_delay;    // distance based delay
extern "C" unsigned refill_click_button_mode;         // click button mode (0/1 fixed, 2 random)
extern "C" char refill_open_delay_timer[], refill_close_delay_timer[], refill_random_walk_1[], refill_random_walk_2[], refill_gauss_rng_state[], refill_random_walk_3[];
extern "C" float refill_step_delay;            // step delay
extern "C" int refill_last_slot;              // last slot
extern "C" float refill_cell_angle;
extern "C" int refill_slot_being_moved;              // slot being moved
extern "C" int refill_random_button;              // random button
extern "C" const float random_range_bound_0_9_in_refill, random_range_bound_1_in_refill, random_range_bound_0_8_in_refill, random_range_bound_1_2, random_range_bound_0_04_in_refill, random_range_bound_0_08_in_refill,
                       random_range_bound_1_4, random_range_bound_1_8, random_range_bound_2_in_refill, random_range_bound_27, pi_b, offset_neg_1_in_refill;
extern "C" const double walk_period_10_in_refill, walk_period_30_in_refill, factor_0_3_in_refill, offset_1_in_refill;
FN(void *, get_current_screen, (void *jvm, void *object));
FN(unsigned char, java_jni_is_instance_of_gui_inventory, (void *jvm, void *screen));
FN(Rot, rot_make_floats, (float lo, float hi));
FN(float, walk_update, (void *state, Rot rng, double p));
FN(unsigned char, walk_should_change, (void *state, Rot rng, double p));
FN(float, random_rotation, (Rot rng));
FN(float, gauss_random, (void *st));
FN(int, refill_pick_inventory_cell, (int last, float bias, float *dist));
FN(void, refill_disable_and_close_screen, (Ctx *ctx));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, get_game_screen, (void *jvm, void *object));
FN(void, java_increment_key_binding_press_time, (void *jvm, void *key_binding));
FN(void, java_gui_container_handle_mouse_click, (void *jvm, void *screen, int slot, int button, int mode));
FN(void, queue_module_toggle, (void *module_manager, void *module, int enabled));
FN(void, keyboard_send_key, (void *jvm, void *screen, int x, int y));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *inv, int slot));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *stack));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(unsigned, java_get_item_stack_metadata, (void *jvm, void *stack));

extern "C" void refill_enable(EvHook *hook) {
    if (hook->ev != refill_module_descriptor)
        return;
    refill_pending_action = 0;
    if (hook->on != 1) {
        // a leftover wait would stall the first step after the module is switched back on
        refill_step_delay = 0.0f;
        refill_slot_being_moved = -1;
    }
}

// class: 0 = empty, 1 = other / soup-less, 2 = splash heal potion,
//        refill_non_health_potion_class_offset+1 = other splash potion, refill_soup_class_offset+1 = mushroom soup
extern "C" void refill_classify_27_main(Ctx *ctx) {
    void *inv = java_get_entity_player_inventory(ctx->jvm, ctx->player);
    for (long i = 0; i != 27; i++) {
        unsigned char cls;
        void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, inv, (int)i + 9);
        if (!stack)
            goto empty;
        {
            void *jvm = ctx->jvm;
            void *item = java_call_item_stack_get_item(jvm, stack);
            if (!item)
                goto empty;
            int item_id = java_call_static_item_get_id_from_item(jvm, item);
            if (!item_id)
                goto empty;
            switch (item_id) {
            case 0x175:
                cls = 1;
                if (refill_refill_potion == 1) {
                    unsigned dmg = java_get_item_stack_metadata(jvm, stack);
                    if (dmg & 0x4000) {
                        cls = 2;
                        if ((dmg & 0xf) != 5)
                            cls = refill_non_health_potion_class_offset + 1;
                    }
                }
                break;
            case 0x11a:
                cls = refill_soup_class_offset + 1;
                break;
            default:
                cls = 1;
                break;
            }
            goto store;
        }
empty:
        cls = 0;
store:
        refill_slot_classes[i] = cls;
    }
}

static inline __attribute__((always_inline)) void add_delay(float base, char *st) {
    double d = (double)base - (double)walk_update(st, rot_make_floats(0.0f, random_range_bound_2_in_refill), walk_period_30_in_refill);
    d = d > 0.0 ? d : 0.0;
    refill_step_delay = (float)((double)refill_step_delay + d);
}

// tick: open the inventory, shift-click soups / potions into the hotbar, close it
//   state 0 open, 1 pick timing, 2 choose slot, 3 click, 4 done
extern "C" void refill_classify_27_main(Ctx *c);
extern "C" void refill_on_inventory_tick(InvHook *hook) {
    Ctx *ctx = hook->ctx;
    void *scr = get_current_screen(ctx->jvm, ctx->minecraft);
    if (refill_pending_action != 0) {
        if (!scr || !java_jni_is_instance_of_gui_inventory(ctx->jvm, scr))
            goto disable;
        hook->handled = 1;
    }
    {
        float t = refill_step_delay;
        if (t > 0.0f) {
            refill_step_delay = t + offset_neg_1_in_refill;
            return;
        }
        if (!(0.0f >= t))
            return;
    }
    {
        int btn = 0;
        for (;;) {
            if (refill_pending_action == 4)
                goto disable;
            if (!ctx->player)
                goto finish;
            {
                void *inv = java_get_entity_player_inventory(ctx->jvm, ctx->player);
                int i = 0;
                for (; i < 9; i++) {
                    void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, inv, i);
                    if (!stack)
                        break;
                    void *item = java_call_item_stack_get_item(ctx->jvm, stack);
                    if (!item || !java_call_static_item_get_id_from_item(ctx->jvm, item))
                        break;
                }
                if (i == 9)
                    goto finish;
            }
            switch (refill_pending_action) {
            case 0: {
                refill_pending_action = 1;
                if (scr)
                    return;
                refill_classify_27_main(ctx);
                float dist;
                if (refill_pick_inventory_cell(refill_last_slot, refill_cell_angle, &dist) == -1) {
                    refill_disable_and_close_screen(ctx);
                    return;
                }
                java_increment_key_binding_press_time(ctx->jvm, get_game_screen(ctx->jvm, get_game_settings(ctx->jvm, ctx->minecraft)));
                return;
            }
            case 1:
                refill_last_slot = (int)random_rotation(rot_make_floats(0.0f, random_range_bound_27));
                refill_cell_angle = random_rotation(rot_make_floats(0.0f, pi_b));
                refill_pending_action = 2;
                add_delay(refill_open_delay_setting, refill_open_delay_timer);
                break;
            case 2: {
                float dist;
                int result = refill_pick_inventory_cell(refill_last_slot, refill_cell_angle, &dist);
                refill_slot_being_moved = result;
                if (result == -1) {
                    add_delay(refill_close_delay_setting, refill_close_delay_timer);
                    refill_pending_action = 4;
                    return;
                }
                float q = walk_update(refill_random_walk_1, rot_make_floats(random_range_bound_0_8_in_refill, random_range_bound_1_2), walk_period_30_in_refill) * refill_speed_value;
                q = (float)((double)q * ((double)gauss_random(refill_gauss_rng_state) * factor_0_3_in_refill + offset_1_in_refill));
                if (walk_should_change(refill_random_walk_2, rot_make_floats(random_range_bound_0_04_in_refill, random_range_bound_0_08_in_refill), walk_period_30_in_refill))
                    q /= random_rotation(rot_make_floats(random_range_bound_1_4, random_range_bound_1_8));
                float num = refill_distance_based_delay ? dist : random_range_bound_1_in_refill;
                refill_step_delay = num / q + refill_step_delay;
                refill_last_slot = refill_slot_being_moved;
                refill_pending_action = 3;
                break;
            }
            case 3: {
                unsigned mode = refill_click_button_mode;
                if (mode < 2) {
                    btn = (int)mode;
                } else if (mode == 2) {
                    btn = refill_random_button;
                    if (walk_should_change(refill_random_walk_3, rot_make_floats(random_range_bound_0_9_in_refill, random_range_bound_1_in_refill), walk_period_10_in_refill))
                        refill_random_button = refill_random_button == 0;
                }
                java_gui_container_handle_mouse_click(ctx->jvm, scr, refill_slot_being_moved + 9, btn, 1);
                refill_slot_classes[refill_slot_being_moved] = 0;
                refill_pending_action = 2;
                break;
            }
            }
            if (!(0.0f >= refill_step_delay))
                return;
        }
    }
finish:
    add_delay(refill_close_delay_setting, refill_close_delay_timer);
    refill_pending_action = 4;
    if (!(0.0f >= refill_step_delay))
        return;
disable:
    queue_module_toggle(ctx->module_manager, refill_module_descriptor, 0);
    {
        void *screen = get_current_screen(ctx->jvm, ctx->minecraft);
        if (screen)
            keyboard_send_key(ctx->jvm, screen, 0, 1);
    }
}
