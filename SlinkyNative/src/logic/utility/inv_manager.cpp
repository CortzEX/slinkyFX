// Inv Manager - sorts the inventory with timed window clicks.
//   0x47090 tick: state machine (0 / 1 plan, 3 click, 4 / 5 drag out, 2 done)
//   0x479c0 plan step: pick the best stack for armour / hotbar slot `sub`
//   0x47f90 score an item stack for armour slot `slot` (falls back to the
//           mirrored slot when "swap_sides" is on and the item does not fit)
#include "slinky/engine.h"
#include "slinky/jids.h"

extern "C" unsigned char inv_manager_try_paired_slot;    // try the paired slot
extern "C" char refill_module_descriptor[];              // Refill drives the same screen
extern "C" int refill_pending_action;                    // Refill mid-cycle state

FN(void *, java_call_item_stack_get_item, (void *jvm, void *stack));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(int, java_get_item_stack_metadata, (void *jvm, void *stack));
FN(void *, java_call_item_stack_get_enchantment_tag_list, (void *jvm, void *stack));
FN(unsigned char, java_call_item_stack_has_display_name, (void *jvm, void *stack));
FN(unsigned short, score_item_inventory_sorter_category, (void **s, int slot, int id, int dmg, void *nbt, unsigned char flag));

extern "C" unsigned inv_manager_score_item_stack(void **s, void *stack, int slot) {
    void *item = java_call_item_stack_get_item(*s, stack);
    if (!item)
        return 0;
    int item_id = java_call_static_item_get_id_from_item(*s, item);
    int dmg = java_get_item_stack_metadata(*s, stack);
    void *nbt = java_call_item_stack_get_enchantment_tag_list(*s, stack);
    unsigned char flag = java_call_item_stack_has_display_name(*s, stack);
    unsigned r = score_item_inventory_sorter_category(s, slot, item_id, dmg, nbt, flag);
    if (inv_manager_try_paired_slot != 1)
        return r;
    if (r)
        return r << 16;
    int alt;
    switch (slot) {
    case 0x16: alt = 0x19; break;
    case 0x11: alt = 7; break;
    case 7: alt = 0x11; break;
    default: return 0;
    }
    return score_item_inventory_sorter_category(s, alt, item_id, dmg, nbt, flag);
}

struct Pair { float x, y;
#ifdef __linux__
    ~Pair() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Range { float lo, hi;
#ifdef __linux__
    ~Range() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct InvState {                   // 0x3049e0
    int state;
    int padding_04;
    unsigned long long mask;        // slots already handled
    int action;                     // 0 shift-click, 1 swap with hotbar `slot`, 2 pick up
    unsigned char slot, padding_15, padding_16, padding_17;
    int saved;                      // state to resume after a click
    unsigned char sub;              // current target slot within the state
    unsigned char window;           // window slot to click
    unsigned char advance;          // move to the next target after the click
    unsigned char padding_1f;
    float delay;
    int padding_24;
    Pair pos;                       // last mouse position (humanised delay)
};
struct InvHook { char padding_00[8]; Ctx *ctx; unsigned char busy; };
typedef unsigned char (*Step)(Ctx *c, InvState *st);

extern "C" InvState inv_manager_rotation_walk;
extern "C" char inv_manager_enum_slot_1[];                   // per-slot settings, 0x40 bytes each (+0x30 wanted type)
extern "C" float inv_manager_start_delay;                    // start delay
extern "C" float inv_manager_click_delay_b, inv_manager_click_delay_b_second;       // click delay = a / b
extern "C" unsigned char inv_manager_randomise;            // randomise
extern "C" unsigned char inv_manager_scale_by_mouse_distance;            // scale by mouse distance
extern "C" unsigned char *const inv_manager_slot_setting_ptrs[2]; // state 0 / 1 enabled
extern "C" const unsigned char inv_manager_targets_per_state[2];  // targets per state
extern "C" const Step ptr_to_plan_inventory_swap[2];           // plan step per state
extern "C" const float minus_one_in_inv_manager, minus_two_in_inv_manager, factor_0_5_in_inv_manager, random_range_bound_1_in_inv_manager, random_range_bound_1_25, random_range_bound_1_5;

FN(void *, get_current_screen, (void *jvm, void *object));
FN(unsigned char, java_jni_is_instance_of_gui_inventory, (void *jvm, void *g));
FN(void, java_gui_container_handle_mouse_click, (void *jvm, void *g, int slot, int button, int mode));
FN(Pair, slot_index, (int slot));
FN(float, rot_delta_length, (Pair a, Pair b));
FN(Range, rot_make_floats, (float lo, float hi));
FN(float, random_rotation, (Range r));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(void *, java_get_inventory_player_main_inventory, (void *jvm, void *inventory_player));
FN(int, java_get_item_stack_stack_size, (void *jvm, void *stack));
FN(int, java_call_item_stack_get_max_stack_size, (void *jvm, void *stack));
FN(unsigned char, java_call_static_item_stack_are_item_stack_tags_equal, (void *jvm, void *item_stack, void *other_object));

static inline __attribute__((always_inline)) void move_to(Pair p) {
    float d = inv_manager_click_delay_b / inv_manager_click_delay_b_second;
    if (inv_manager_scale_by_mouse_distance == 1) {
        float m = rot_delta_length(p, inv_manager_rotation_walk.pos) * factor_0_5_in_inv_manager;
        d *= m > random_range_bound_1_in_inv_manager ? m : random_range_bound_1_in_inv_manager;
    }
    if (inv_manager_randomise == 1)
        d *= random_rotation(rot_make_floats(random_range_bound_1_in_inv_manager, random_range_bound_1_25));
    inv_manager_rotation_walk.delay = d + inv_manager_rotation_walk.delay;
    inv_manager_rotation_walk.pos = p;
}

extern "C" void inv_manager_tick(InvHook *hook) {
    InvState &st = inv_manager_rotation_walk;
    Ctx *ctx = hook->ctx;
    // Refill opens the inventory and shift-clicks stacks by slot index; sorting the
    // same screen at the same time would move the stacks under its clicks. Drop any
    // plan its clicks may have invalidated. States 4/5 already hold a stack on the
    // cursor, so they finish (put it down) instead of being abandoned there.
    bool refill_busy = refill_module_descriptor[0x14] && refill_pending_action != 0;
    if (refill_busy && st.state != 4 && st.state != 5) {
        st.state = 2;
        return;
    }
    void *screen = get_current_screen(ctx->jvm, ctx->minecraft);
    if (!screen || !java_jni_is_instance_of_gui_inventory(ctx->jvm, screen)) {
        __builtin_memset(&st, 0, 0x20);
        st.delay = inv_manager_start_delay;
        if (inv_manager_randomise == 1)
            st.delay = random_rotation(rot_make_floats(random_range_bound_1_in_inv_manager, random_range_bound_1_5)) * st.delay;
        st.pos = slot_index(0xd);
        return;
    }
    int s = st.state;
    if (s == 2)
        return;
    if (st.delay > 0.0f) {
        st.delay += minus_one_in_inv_manager;
        if (st.delay > 0.0f)
            return;
    }
    hook->busy = 1;
    int button = 0, mode = 0;
    for (;;) {
        switch (s) {
        case 0:
        case 1:
            if (*inv_manager_slot_setting_ptrs[s]) {
                for (unsigned char n = inv_manager_targets_per_state[s]; st.sub < n; st.sub++) {
                    if (ptr_to_plan_inventory_swap[s](ctx, &st)) {
                        move_to(slot_index(st.window));
                        st.state = 3;
                        st.saved = s;
                        st.advance = s != 1;
                        return;
                    }
                }
            }
            st.state = ++s;
            __builtin_memset((char *)&st + 4, 0, 0x1c);
            continue;
        case 3: {
            int act = st.action;
            unsigned char slot = st.slot;
            if (act == 2) {
                button = 0;
                mode = 0;
            } else if (act == 1) {
                button = slot;
                mode = 2;
            } else if (act == 0) {
                button = 0;
                mode = 1;
            }
            java_gui_container_handle_mouse_click(ctx->jvm, screen, st.window, button, mode);
            st.mask |= (long long)(1 << (st.sub & 31));
            if (st.advance == 1)
                st.sub++;
            if (act == 2) {
                move_to(slot_index(slot));
                st.state = 4;
                st.window = slot;
                return;
            }
            s = st.state = st.saved;
            continue;
        }
        case 4: {
            java_gui_container_handle_mouse_click(ctx->jvm, screen, st.window, 0, 0);
            Pair p = st.pos;
            p.x += minus_two_in_inv_manager;
            move_to(p);
            st.state = 5;
            return;
        }
        case 5:
            java_gui_container_handle_mouse_click(ctx->jvm, screen, -999, 0, 0);    // drop outside the window
            if (refill_busy) {
                st.state = 2;   // Refill is driving this screen; do not plan a new sort
                return;
            }
            s = st.state = st.saved;
            continue;
        default:
            return;
        }
    }
}

static inline __attribute__((always_inline)) unsigned long long item_key(Ctx *ctx, void *stack) {
    void *item = java_call_item_stack_get_item(ctx->jvm, stack);
    if (!item)
        return 0;
    unsigned item_id = java_call_static_item_get_id_from_item(ctx->jvm, item);
    return ((unsigned long long)item_id << 32) | (unsigned long long)(long long)java_get_item_stack_metadata(ctx->jvm, stack);
}

extern "C" unsigned char inv_manager_plan_step(Ctx *ctx, InvState *st) {
    unsigned sub = st->sub;
    int want = *(int *)(inv_manager_enum_slot_1 + sub * 0x40 + 0x30);
    if (!want)
        return 0;
    void *inv = java_get_inventory_player_main_inventory(ctx->jvm, java_get_entity_player_inventory(ctx->jvm, ctx->player));
    JNIEnv *env = ((Jvm *)ctx->jvm)->env;
    auto get = (*env)->GetObjectArrayElement;
    void *cur = get(env, inv, sub);
    int count = 0, max = 0;
    unsigned long long key = 0;
    unsigned score = 0;
    if (cur) {
        count = java_get_item_stack_stack_size(ctx->jvm, cur);
        max = java_call_item_stack_get_max_stack_size(ctx->jvm, cur);
        key = item_key(ctx, cur);
        score = inv_manager_score_item_stack((void **)ctx, cur, want);
    }
    void *last;
    int i = 9;
    for (;;) {                                  // any empty main-inventory slot?
        last = get(env, inv, i);
        if (!last || i == 0x23)
            break;
        i++;
    }
    if (count <= 0)
        count = 0;
    unsigned best = count * score;
    unsigned best_slot = sub;
    unsigned char merge = 0;
    for (unsigned j = 0; j < 0x24; j++) {
        if (j == sub || (st->mask >> j & 1))
            continue;
        void *stack = get(env, inv, j);
        if (!stack)
            continue;
        int n = java_get_item_stack_stack_size(ctx->jvm, stack);
        unsigned long long k = item_key(ctx, stack);
        unsigned merged = 0;
        if (count < max && n < max && k == key && java_call_static_item_stack_are_item_stack_tags_equal(ctx->jvm, cur, stack)) {
            int total = count + n;
            merged = (total < max ? total : max) * score;
        }
        unsigned v = inv_manager_score_item_stack((void **)ctx, stack, want) * n;
        bool use_merge = v - 1 < merged;
        if (use_merge)
            v = merged;
        if (v > best && (!last || j >= 9 || !use_merge)) {
            best = v;
            best_slot = j;
            merge = use_merge;
        }
    }
    if (best_slot != sub) {
        st->padding_15 = st->padding_16 = st->padding_17 = 0;
        st->action = !merge;
        st->slot = merge ? 0 : sub;
        st->window = (int)best_slot >= 9 ? best_slot : best_slot + 0x24;
        st->mask |= (long long)(1 << (sub & 31));
        return 1;
    }
    if (best)
        st->mask |= (long long)(1 << (sub & 31));
    return 0;
}
