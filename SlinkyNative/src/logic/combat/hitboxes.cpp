// Hitboxes - descriptor +0xc8: publish the hitbox expansion each tick.
#include "slinky/engine.h"

extern "C" float hitboxes_expand;  // "expand" current value

struct HitboxState {
    char padding_00[0x10];
    bool active;   // +0x10
    char padding_11[0xf];
    double expand; // +0x20
};

extern "C" void hitboxes_on_reach_query(HitboxState *s) {
    s->active = true;
    s->expand = (double)hitboxes_expand;
}
