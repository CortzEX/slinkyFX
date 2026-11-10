// Free Look - descriptor +0x98 / +0x118: swap the camera perspective while
// the key is held and restore the previous one on release.
#include "slinky/engine.h"

struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};

extern "C" char free_look_module_descriptor[];                 // owner tag
extern "C" unsigned free_look_target_perspective_value;               // "target_perspective" current value
extern "C" unsigned char free_look_active;          // free-look active
extern "C" Rot free_look_saved_camera;                  // saved camera
extern "C" int free_look_saved_perspective;                    // saved perspective

struct LookState { char padding_00[8]; Ctx *ctx; void *tag; unsigned char held; };  // held @ +0x18

FN(bool, is_silent_rotation_active, ());
FN(Rot, get_silent_rotation, ());
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(int, java_get_game_settings_third_person_view, (void *jvm, void *game_settings));
FN(void, java_set_game_settings_third_person_view, (void *jvm, void *game_settings, int perspective));
FN(void, set_render_rotation, (Rot camera));

extern "C" void free_look_on_module_toggle(LookState *s) {
    if (s->tag != (void *)free_look_module_descriptor)
        return;
    Ctx *ctx = s->ctx;
    if (s->held == 1) {
        // the camera save reads the player, but the disable path only touches GameSettings and the
        // render rotation - it must still run when the module is switched off during unload, where
        // the world is already gone and the saved perspective would otherwise stay applied
        if (ctx->world == 0)
            return;
        free_look_active = 1;
        Rot cam;
        if (is_silent_rotation_active())
            cam = get_silent_rotation();
        else
            cam = get_entity_rotation(ctx->jvm, ctx->player);
        free_look_saved_camera = cam;
    }
    void *game_settings = get_game_settings(ctx->jvm, ctx->minecraft);
    void *jvm = ctx->jvm;
    if (s->held != 0) {
        free_look_saved_perspective = java_get_game_settings_third_person_view(jvm, game_settings);
        void *a2 = ctx->jvm;
        unsigned persp = free_look_target_perspective_value;
        java_set_game_settings_third_person_view(a2, game_settings, persp);
        return;
    }
    java_set_game_settings_third_person_view(jvm, game_settings, free_look_saved_perspective);
    if (free_look_active == 1) {
        set_render_rotation(free_look_saved_camera);
        free_look_active = 0;
    }
}

extern "C" void free_look_on_rotation_update(char *s) {
    s[8] = 1;
    __builtin_memcpy(s + 0xc, &free_look_saved_camera, 8);
}
