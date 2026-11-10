// No Item Release - descriptor +0x78 (outbound packet) / +0x98 (toggle).
// Tracks (no_item_release_release_lock_flag) whether the held item is one whose use must not be
// released; swords/bow/0x15a handling depends on the "mode" setting.
// The flag is cleared on disable so a re-enable can't release the item for a stale placement.
#include "slinky/engine.h"

extern "C" char no_item_release_module_descriptor[];      // No Item Release module descriptor
extern "C" unsigned no_item_release_mode;          // "mode" current value
extern "C" unsigned char no_item_release_release_lock_flag;     // release-lock flag

struct RelState { char padding_00[8]; void *w; void *x; char padding_18[0xc]; int state; };  // +0x24
struct ToggleHook { char padding_00[8]; void *ctx; void *ev; unsigned char on; };
FN(void *, java_jni_is_instance_of_c08_packet_player_block_placement, (void *w, void *candidate));
FN(void *, java_get_c08_packet_player_block_placement_stack, (void *w, void *c08_packet_player_block_placement));
FN(void *, java_call_item_stack_get_item, (void *w, void *item_stack));
FN(int, java_call_static_item_get_id_from_item, (void *w, void *item));
FN(void *, java_jni_is_instance_of_c07_packet_player_digging, (void *w, void *candidate));
FN(int, java_c07_packet_player_digging_status, (void *w, void *object));

extern "C" void no_item_release_on_packet_sent(RelState *s) {
    unsigned char lock = 0;
    void *c08_packet_player_block_placement = java_jni_is_instance_of_c08_packet_player_block_placement(s->w, s->x);
    void *jvm = s->w;
    if (c08_packet_player_block_placement) {
        void *stack = java_get_c08_packet_player_block_placement_stack(jvm, c08_packet_player_block_placement);
        if (stack) {
            void *item = java_call_item_stack_get_item(jvm, stack);
            if (item) {
                int item_id = java_call_static_item_get_id_from_item(jvm, item);
                bool compute;
                switch (item_id) {
                case 267: case 268: case 272: case 276: case 283:  // swords
                    compute = no_item_release_mode != 1;
                    break;
                case 261:  // bow
                case 346:
                    compute = false;
                    break;
                default:
                    compute = no_item_release_mode != 0;
                    break;
                }
                if (compute) {
                    int id2 = java_call_static_item_get_id_from_item(jvm, item);
                    lock = (id2 != 0x15a) & (id2 != 0x105);
                }
            }
        }
        no_item_release_release_lock_flag = lock;
        return;
    }
    c08_packet_player_block_placement = java_jni_is_instance_of_c07_packet_player_digging(jvm, s->x);
    if (!c08_packet_player_block_placement || no_item_release_release_lock_flag == 0)
        return;
    if (java_c07_packet_player_digging_status(s->w, c08_packet_player_block_placement) == 5)
        s->state = 2;
    no_item_release_release_lock_flag = 0;
}

extern "C" void no_item_release_on_toggle(ToggleHook *hook) {
    if (hook->ev != no_item_release_module_descriptor || hook->on != 0)
        return;
    no_item_release_release_lock_flag = 0;
}
