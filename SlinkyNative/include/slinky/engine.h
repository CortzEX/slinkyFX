// Engine-side types and helpers shared by the recoded module logic.
// Everything is named after its original RVA until its purpose is understood.
#pragma once
#include "slinky/api.h"

// Per-hook context handed to every module callback (rcx at +0x08 points here).
struct Ctx {
    void *jvm;              // +0x00  JNI/JVM handle passed as the first argument of every java_* call
    void *module_manager;   // +0x08  target of queue_module_toggle
    void *profile_list;     // +0x10
    void *minecraft;        // +0x18  Minecraft instance
    void *world;            // +0x20  client world
    void *player;           // +0x28  local player entity
    void *key_state_table;  // +0x30
    void *target;           // +0x38  current combat target record (entity, squared distance, ...)
    unsigned char in_game;  // +0x40  non-zero while a world is loaded
};

// Current combat target record (Ctx::target)
struct CombatTarget {
    void *padding_00;
    void *ent;              // +0x08  target entity
    double dist2;           // +0x10  squared distance to it
    unsigned char valid;    // +0x18
};

// Hook argument block (rcx of every descriptor callback).
struct Hook {
    char pad0[8];
    Ctx *ctx;           // +0x08
};

// Packet verdicts are a single shared dword (0 pass, 1 hold, 2 drop, 3 flush) that every enabled
// module writes, so a plain assignment makes the result depend on handler order: a module that only
// wants to hold could downgrade a keep-alive flush or a cancel written earlier. These merges keep the
// strongest intent regardless of which module runs first.
static inline void packet_hold(int *verdict) {
    if (*verdict == 0)
        *verdict = 1;
}
static inline void packet_flush(int *verdict) {
    if (*verdict != 2)
        *verdict = 3;
}
static inline void packet_drop(int *verdict) { *verdict = 2; }

#define FN(ret, name, args) extern "C" ret name args
