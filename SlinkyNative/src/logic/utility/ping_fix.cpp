// Ping Fix - descriptor +0x68 / +0x78: when the lookup succeeds, force the
// packet state field to 3.
#include "slinky/engine.h"

struct PingState {
    char padding_00[8];
    void *jvm;
    void *packet;
    char padding_18[0xc];
    int state;  // +0x24
};

FN(void *, java_jni_is_instance_of_s00_packet_keep_alive, (void *jvm, void *candidate));
FN(void *, java_jni_is_instance_of_c00_packet_keep_alive, (void *jvm, void *candidate));

extern "C" void ping_fix_drop_incoming_keepalive(PingState *s) {
    if (java_jni_is_instance_of_s00_packet_keep_alive(s->jvm, s->packet))
        s->state = 3;
}

extern "C" void ping_fix_drop_outgoing_keepalive(PingState *s) {
    if (java_jni_is_instance_of_c00_packet_keep_alive(s->jvm, s->packet))
        s->state = 3;
}
