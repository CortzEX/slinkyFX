// Chest Stealer - GUI tick. After "delay after open" it plans a sweep of the chest,
// shift-clicks one stack per interval (never more than one click per tick) and waits
// for the server to answer each click before sending the next one. Two clicks in a row
// that leave the stack untouched mean the inventory is full or the server refused them,
// and the module stops instead of hammering the server. Then it waits "delay before
// close" and closes the chest; the GUI it closed is latched, so a server that ignores
// Escape is not looted again on the next cycle. Only slots reported by
// GuiChest.lowerChestInventory are clicked:
// its slots 0..n-1 are exactly window slots 0..n-1 and getStackInSlot() returns null
// past the end, so a click can never land on a player-inventory slot (nothing can ever
// be moved from the player into the chest).
#include "slinky/engine.h"
#include "slinky/jcall.h"

struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};

// layouts of the engine's random-walk / gaussian state (see math_helpers.cpp)
struct Walk { double t0; float a, b; };
struct Gauss { float spare; bool has; };

S(str_container_chest);        // "container.chest"
S(str_container_chestdouble);  // "container.chestDouble"

extern "C" float chest_stealer_delay_after_open_value;                     // "delay_after_open" current value
extern "C" unsigned char chest_stealer_require_vanilla_title_value;        // "require_vanilla_title"
// setting blocks: FloatSet has its value at +0x30, BoolSet at +0x18 (profiles.cpp visitors)
extern "C" unsigned char chest_stealer_delay_before_close_setting[];
extern "C" unsigned char chest_stealer_speed_setting[];
extern "C" unsigned char chest_stealer_smart_speed_setting[];
extern "C" const float range_low_1_in_chest_gui, random_range_bound_1_5_in_chest_gui;

FN(void *, get_current_screen, (void *jvm, void *object));
FN(unsigned char, java_jni_is_instance_of_gui_chest, (void *jvm, void *scr));
FN(void *, java_get_gui_chest_lower_chest_inventory, (void *jvm, void *scr));
FN(void *, java_jni_is_instance_of_inventory_basic, (void *jvm, void *candidate));
FN(void *, java_get_inventory_basic, (void *jvm, void *inventory_basic));
FN(unsigned char, java_string_equal_translation_key, (void *jvm, void *title, const char *name));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *inv, int slot));
FN(int, java_get_item_stack_stack_size, (void *jvm, void *stack));
FN(void, java_gui_container_handle_mouse_click, (void *jvm, void *scr, int slot, int button, int mode));
FN(void, keyboard_send_key, (void *jvm, void *screen, int c, int key));
FN(Rot, rot_make_floats, (float lo, float hi));
FN(float, random_rotation, (Rot rng));
FN(float, gauss_random, (void *state));
FN(float, walk_update, (void *state, Rot range, double period));
FN(unsigned char, walk_should_change, (void *state, Rot range, double period));

static int cs_phase;              // 0 idle, 1 wait after open, 2 steal, 3 wait before close
static float cs_wait;             // ticks left before the next action
static unsigned long long cs_plan;    // chest slots that held a stack when the sweep was planned
static unsigned long long cs_tried;   // slots already handled this cycle
static int cs_last_slot;          // last clicked slot, for the smart-speed spacing
static int cs_pending_slot = -1;  // slot of the click the server has not answered yet
static int cs_pending_size;       // its stack size before the click
static float cs_pending_wait;     // ticks the server still has to answer that click
static int cs_failed;             // consecutive clicks that moved nothing
static jobject cs_screen;         // global ref to the chest GUI this cycle belongs to
static Walk cs_walk_speed, cs_walk_pause;
static Gauss cs_gauss;

// a shift-click travels to the server and only its answer changes the chest, so a click
// is given a full second before it counts as refused (a normal connection answers in 1-3 ticks)
static const float cs_answer_window = 20.0f;

static inline float setting_float(unsigned char *setting) { return *(float *)(setting + 0x30); }

// waits use the original client's own humanisation window (x1.0..x1.5)
static inline float wait_ticks(float value) {
    if (!(value > 0.0f)) return 0.0f;
    return value * random_rotation(rot_make_floats(range_low_1_in_chest_gui, random_range_bound_1_5_in_chest_gui));
}

// straight-line spacing of two slots on the 9-wide chest grid
static inline float slot_distance(int a, int b) {
    float dr = (float)(a / 9 - b / 9), dc = (float)(a % 9 - b % 9);
    return __builtin_sqrtf(dr * dr + dc * dc);
}

// interval between two clicks: the same walk/gaussian humanisation the client's Refill
// uses, slowed down when the next stack is far from the last one (Smart speed)
static inline float click_interval(int slot) {
    float speed = setting_float(chest_stealer_speed_setting);
    if (!(speed >= 1.0f)) speed = 1.0f;
    if (speed > 4.0f) speed = 4.0f;
    float q = walk_update(&cs_walk_speed, rot_make_floats(0.8f, 1.2f), 30.0) * speed;
    q *= gauss_random(&cs_gauss) * 0.3f + 1.0f;
    if (walk_should_change(&cs_walk_pause, rot_make_floats(0.04f, 0.08f), 30.0))
        q /= random_rotation(rot_make_floats(1.4f, 1.8f));
    if (!(q > 0.25f)) q = 0.25f;
    float d = 4.0f / q;
    if (chest_stealer_smart_speed_setting[0x18] != 0) {
        float m = slot_distance(slot, cs_last_slot) * 0.5f;
        if (m > 1.0f) d *= m;
    }
    if (d < 1.0f) d = 1.0f;   // one click per tick at most
    if (d > 10.0f) d = 10.0f;
    return d;
}

static void cs_reset(JNIEnv *env) {
    if (cs_screen) (*env)->DeleteGlobalRef(env, cs_screen);
    cs_screen = 0;
    cs_phase = 0;
    cs_pending_slot = -1;
    cs_plan = cs_tried = 0;
    cs_failed = 0;
}

static void cs_start_close(void) {
    cs_phase = 3;
    cs_pending_slot = -1;
    cs_wait = wait_ticks(setting_float(chest_stealer_delay_before_close_setting));
}

struct CsToggleHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" char chest_stealer_module_descriptor[];
extern "C" void chest_stealer_on_toggle(CsToggleHook *hook) {
    if (hook->ev != chest_stealer_module_descriptor || hook->on)
        return;
    cs_reset(((Jvm *)hook->ctx->jvm)->env);
}

extern "C" void chest_stealer_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    JNIEnv *env = ((Jvm *)ctx->jvm)->env;
    void *scr = get_current_screen(ctx->jvm, ctx->minecraft);
    if (!scr || !java_jni_is_instance_of_gui_chest(ctx->jvm, scr)) {
        cs_reset(env);
        return;
    }
    void *chest = java_get_gui_chest_lower_chest_inventory(ctx->jvm, scr);
    if (!chest) {
        cs_reset(env);
        return;
    }
    if (chest_stealer_require_vanilla_title_value != 0) {
        // server-opened windows carry their own title; a world chest has none
        void *named = java_jni_is_instance_of_inventory_basic(ctx->jvm, chest);
        if (named) {
            void *title = java_get_inventory_basic(ctx->jvm, named);
            bool vanilla = java_string_equal_translation_key(ctx->jvm, title, str_container_chest)
                || java_string_equal_translation_key(ctx->jvm, title, str_container_chestdouble);
            if (title) (*env)->DeleteLocalRef(env, (jstring)title);
            if (!vanilla) {
                cs_reset(env);   // shop / menu GUI: leave it alone
                return;
            }
        }
    }
    // a different chest GUI (or a fresh one after the module was switched off mid-cycle) starts over
    bool same = cs_screen && (*env)->IsSameObject(env, cs_screen, (jobject)scr);
    if (!same) {
        cs_reset(env);
        cs_screen = (*env)->NewGlobalRef(env, (jobject)scr);
        cs_phase = 1;
        cs_last_slot = 13;   // cursor resting at the middle of the chest, not in a corner
        cs_wait = wait_ticks(chest_stealer_delay_after_open_value);
        return;
    }
    if (cs_phase == 0)
        return;   // the module closed this GUI; leave it alone until another one is opened
    // did the click sent last time move anything? a partial move still counts; no answer
    // within the window counts as refused
    if (cs_phase == 2 && cs_pending_slot >= 0) {
        void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, chest, cs_pending_slot);
        bool moved = !stack || java_get_item_stack_stack_size(ctx->jvm, stack) < cs_pending_size;
        if (stack) (*env)->DeleteLocalRef(env, stack);
        if (moved) {
            cs_pending_slot = -1;
            cs_failed = 0;
        } else if (cs_pending_wait > 0.0f) {
            cs_pending_wait -= 1.0f;
        } else {
            cs_pending_slot = -1;
            if (++cs_failed >= 2) {
                cs_start_close();
                return;
            }
        }
    }
    cs_wait -= 1.0f;
    if (cs_wait > 0.0f)
        return;
    if (cs_phase == 2 && cs_pending_slot >= 0)
        return;   // never send the next click before the server answered the last one
    for (;;) {
        if (cs_phase == 1) {
            cs_plan = 0;
            for (int i = 0; i < 54; i++) {
                void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, chest, i);
                if (stack) {
                    cs_plan |= 1ull << i;
                    (*env)->DeleteLocalRef(env, stack);
                }
            }
            cs_tried = 0;
            cs_phase = 2;
            if (!cs_plan) {
                cs_start_close();   // nothing to take: still honour the close delay
                return;
            }
            continue;
        }
        if (cs_phase == 2) {
            unsigned long long remaining = cs_plan & ~cs_tried;
            if (!remaining) {
                cs_start_close();
                return;
            }
            int slot = __builtin_ctzll(remaining);
            cs_tried |= 1ull << slot;
            void *stack = java_call_iinventory_get_stack_in_slot(ctx->jvm, chest, slot);
            if (!stack)
                continue;   // it went away since the plan was made
            cs_pending_size = java_get_item_stack_stack_size(ctx->jvm, stack);
            (*env)->DeleteLocalRef(env, stack);
            java_gui_container_handle_mouse_click(ctx->jvm, scr, slot, 0, 1);
            cs_last_slot = slot;
            if (!(cs_plan & ~cs_tried)) {
                cs_start_close();   // last planned stack: the close delay counts from this click
            } else {
                cs_pending_slot = slot;
                cs_pending_wait = cs_answer_window;
                cs_wait = click_interval(slot);
            }
            return;
        }
        keyboard_send_key(ctx->jvm, scr, 0, 1);
        // stay idle on this GUI (the global ref is kept on purpose): a server that ignores
        // Escape must not be looted and clicked again on the next tick
        cs_phase = 0;
        return;
    }
}
