// 0x29000: silent rotation state, mouse step, Minecraft colour codes and GL vertex-array helpers.
// cflags: /Ob1 /clang:-fno-slp-vectorize
#include "slinky/engine.h"
#include "slinky/jcall.h"

typedef void (*GlPtrFn)(int size, int type, int stride, const void *p);   // glVertexPointer-style
typedef void (*GlEnableFn)(int cap);
extern "C" GlPtrFn *ptr_to_pGlVertexPointer;     // cell -> fn ptr (size,type,stride,ptr)
extern "C" GlEnableFn *ptr_to_pGlEnableClientState;  // cell -> glEnableClientState
extern "C" GlEnableFn *ptr_to_pGlDisableClientState;
extern "C" void (**ptr_to_pGlDrawElements)(int, int, int, const void *);
extern "C" char gl_array_xyz_float_data[], gl_array_color1_indices[], gl_array_color2_indices[], gl_array_xy_float_data[], gl_array_color3_indices[], gl_array_color4_indices[],
    gl_array_xy_float2_data[], gl_array_color5_indices[], gl_array_color6_indices[];

#define GL_VEC(name, tbl, n, cell, ptr) \
    extern "C" void name() { (*tbl)(n, 0x1406, 0, ptr); (*cell)(0x8074); }
#define GL_COL(name, n, size, ptr) \
    extern "C" void name() { auto f = *ptr_to_pGlDrawElements; asm("" : "+r"(f)); f(n, size, 0x1401, ptr); }

GL_VEC(gl_array_xyz_float, ptr_to_pGlVertexPointer, 3, ptr_to_pGlEnableClientState, gl_array_xyz_float_data)
GL_COL(gl_array_color1, 5, 0xe, gl_array_color1_indices)
GL_COL(gl_array_color2, 1, 0x18, gl_array_color2_indices)
GL_VEC(gl_array_xy_float, ptr_to_pGlVertexPointer, 2, ptr_to_pGlEnableClientState, gl_array_xy_float_data)
GL_COL(gl_array_color3, 5, 4, gl_array_color3_indices)
GL_COL(gl_array_color4, 2, 4, gl_array_color4_indices)
GL_VEC(gl_array_xy_float2, ptr_to_pGlVertexPointer, 2, ptr_to_pGlEnableClientState, gl_array_xy_float2_data)
GL_COL(gl_array_color5, 6, 4, gl_array_color5_indices)
GL_COL(gl_array_color6, 2, 4, gl_array_color6_indices)
extern "C" void disable_vertex_array_state() { (*ptr_to_pGlDisableClientState)(0x8074); }

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
FN(void *, java_new_vec3_init, (void *jvm, Vec3 v));
FN(void *, java_world_ray_trace_blocks, (void *jvm, void *object, void *other_object, void *d, bool e, bool f, bool g));
FN(void *, get_game_settings, (void *jvm, void *object));
FN(void *, java_get_game_settings_attack_key, (void *jvm, void *game_settings));
FN(int, get_event_key, (void *jvm, void *object));
FN(Vec3, vec3_make, (double x, double y, double z));

extern "C" bool set_vertex_pointer(void *jvm, void *object, Vec3 *p, Vec3 *q) {
    void *from = java_new_vec3_init(jvm, *p);
    void *s = java_new_vec3_init(jvm, *q);
    return java_world_ray_trace_blocks(jvm, object, from, s, false, true, true) == nullptr;
}

struct Ctx3 { void *jvm; char padding_08[0x10]; void *minecraft; char padding_20[0x10]; char *key_state_table; };
FN(double, time_now, ());
static double input_press_time[0x100];      // last down-edge (time_now()) per client key code
// Two ticks, not one. The gate that asks "is the player attacking" is sampled on a tick boundary, and a click
// from AHK or a mouse macro is down and up inside a single frame, so with a window the exact length of one tick
// whether the next tick still sees that click is decided by where inside the tick it landed - the assist then
// alternates between attacking and not, at any CPS. Two ticks covers the gap no matter where the click falls;
// a real release is still detected, because nothing renews the edge.
static const double input_press_latch = 0.10;

// A hardware / AHK / mouse-macro click goes down and up inside one frame, so both window messages are
// already consumed before the next poll and key_state_table reads 0 on every tick. Remember the edge so
// gates that mean "the player is attacking" still see it. Recorded by on_input_event (java_natives_world).
extern "C" void note_input_press(int code) {
    if ((unsigned)code < 0x100) input_press_time[code] = time_now();
}
extern "C" unsigned char input_down_or_recent(char *keys, int code) {
    // Bound first: this indexes the fixed 0x100 table, so a caller holding an unrecorded code must not read past
    // it. Bit 0 is the press; bit 1 only marks a press the auto clicker posted, which is not the player holding it.
    if ((unsigned)code >= 0x100) return 0;
    if (keys[code] & 1) return 1;
    return time_now() - input_press_time[code] < input_press_latch;
}
// The slot the attack state lives in: right button (-99) is slot 2, everything else slot 1. The game's own attack
// entry point arms this slot (engine/base/java_natives_frame.cpp), so an attack bound to the keyboard, the middle
// button or the X buttons - and every click under Linux, where the input pump can only diff the state LWJGL
// sampled once per frame and a macro's press is gone before the next poll - still reaches the require-mouse-pressed
// gates instead of leaving them asleep.
extern "C" int attack_press_slot(void *jvm, void *minecraft) {
    void *settings = java_get_game_settings_attack_key(jvm, get_game_settings(jvm, minecraft));
    return get_event_key(jvm, settings) == -0x63 ? 2 : 1;
}
extern "C" bool vertex_pointer_current(Ctx3 *c) {
    return input_down_or_recent(c->key_state_table, attack_press_slot(c->jvm, c->minecraft));
}

// ---- rotation state (silent aim / mouse emulation)
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
FN(Rot, rot_add, (Rot a, Rot b));
FN(Rot, rot_make, (float yaw, float pitch));
FN(float, wrap_angle, (float a));                  // wrap to [-180, 180)
FN(void *, get_game_settings, (void *j, void *x));
FN(float, java_get_game_settings_mouse_sensitivity, (void *jvm, void *settings)); // mouse sensitivity
FN(void, pack_flags, (void *jvm, bool on));
FN(Rot, get_entity_rotation, (void *jvm, void *entity));
FN(Rot, get_entity_prev_rotation, (void *jvm, void *entity));
FN(void, set_entity_rotation, (void *jvm, void *entity, Rot rotation));
FN(void, set_entity_prev_rotation, (void *jvm, void *entity, Rot rotation));
extern "C" unsigned rotation_priority_peak;
extern "C" Rot silent_rotation_offset, saved_entity_rotation, saved_entity_prev_rotation, silent_rotation_current;
extern "C" int silent_rotation_age_ticks;
extern "C" float mouse_sensitivity_step;
extern "C" unsigned char rotation_reset_requested, rotation_reset_done, silent_rotation_active_flag, render_rotation_dirty;
extern "C" Rot render_rotation;
extern "C" const float factor_0_6_in_silent_rotation_and_chat_colors, offset_0_2, factor_8_in_silent_rotation_and_chat_colors, factor_0_15_in_silent_rotation_and_chat_colors;

extern "C" bool update_rotation_priority_peak(unsigned v) {
    unsigned old = rotation_priority_peak;
    if (v >= old) rotation_priority_peak = v;
    return v >= old;
}
extern "C" void apply_silent_rotation(Rot rotation) {
    silent_rotation_offset = rot_add(silent_rotation_offset, rotation);
    silent_rotation_age_ticks = 0;
}
extern "C" Rot get_silent_rotation_offset() { return silent_rotation_offset; }
// per-tick: recompute the mouse step from the sensitivity (f = s*0.6+0.2; step = f^3*8*0.15)
extern "C" void update_mouse_sensitivity_step(void *jvm, void *object) {
    float f = java_get_game_settings_mouse_sensitivity(jvm, get_game_settings(jvm, object)) * factor_0_6_in_silent_rotation_and_chat_colors + offset_0_2;
    mouse_sensitivity_step = f * f * f * factor_8_in_silent_rotation_and_chat_colors * factor_0_15_in_silent_rotation_and_chat_colors;
    if (silent_rotation_age_ticks++ > 0) silent_rotation_offset = rot_make(0.0f, 0.0f);
    rotation_priority_peak = 0;
}
extern "C" void request_rotation_reset_if_flagged(void **j) {
    if (rotation_reset_requested == 1) {
        pack_flags(*j, true);
        rotation_reset_done = 1;
    }
}
extern "C" void set_silent_rotation_active(void *jvm, void *object, bool save) {
    Rot *a, *b;
    if (save) {
        saved_entity_rotation = get_entity_rotation(jvm, object);
        saved_entity_prev_rotation = get_entity_prev_rotation(jvm, object);
        a = b = &silent_rotation_current;
    } else {
        b = &saved_entity_prev_rotation;
        a = &saved_entity_rotation;
    }
    set_entity_rotation(jvm, object, *a);
    set_entity_prev_rotation(jvm, object, *b);
}
extern "C" unsigned char is_silent_rotation_active() { return silent_rotation_active_flag; }
extern "C" Rot get_silent_rotation() { return silent_rotation_current; }
extern "C" void set_render_rotation(Rot rotation) {
    float d = wrap_angle(rotation.yaw - silent_rotation_current.yaw);
    render_rotation_dirty = 1;
    silent_rotation_current.yaw = d + silent_rotation_current.yaw;
    silent_rotation_current.pitch = rotation.pitch;
}
extern "C" Rot get_render_rotation() { return render_rotation; }

// Minecraft formatting colour code ('0'-'9', 'a'-'e', anything else white) -> RGB
struct F2 { float a, b;
#ifdef __linux__
    ~F2() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Col { F2 rg; float b;
#ifdef __linux__
    ~Col() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
extern "C" const F2 color_rg_1_1, color_rg_0p33_0p33, color_rg_0p67_0p67, color_rg_1_0p33, color_rg_0p33_1, color_rg_0_0p67, color_rg_1_0p67;
extern "C" const float minecraft_colour_dark_component, one_in_silent_rotation_and_chat_colors, minecraft_colour_bright_component;   // 0.33, 1.0, 0.67
extern "C" Col minecraft_color_code_to_rgb(unsigned short ch) {
    Col c;
    F2 z = {0.0f, 0.0f};
    switch (ch) {
    case '0': c.rg = z; c.b = 0.0f; break;
    case '1': c.rg = z; c.b = minecraft_colour_bright_component; break;
    case '2': c.rg = color_rg_0_0p67; c.b = 0.0f; break;
    case '3': c.rg = color_rg_0_0p67; c.b = minecraft_colour_bright_component; break;
    case '4': c.rg.a = minecraft_colour_bright_component; c.rg.b = 0.0f; c.b = 0.0f; break;
    case '5': c.rg.a = minecraft_colour_bright_component; c.rg.b = 0.0f; c.b = minecraft_colour_bright_component; break;
    case '6': c.rg = color_rg_1_0p67; c.b = 0.0f; break;
    case '7': c.rg = color_rg_0p67_0p67; c.b = minecraft_colour_bright_component; break;
    case '8': c.rg = color_rg_0p33_0p33; c.b = minecraft_colour_dark_component; break;
    case '9': c.rg = color_rg_0p33_0p33; c.b = one_in_silent_rotation_and_chat_colors; break;
    case 'a': c.rg = color_rg_0p33_1; c.b = minecraft_colour_dark_component; break;
    case 'b': c.rg = color_rg_0p33_1; c.b = one_in_silent_rotation_and_chat_colors; break;
    case 'c': c.rg = color_rg_1_0p33; c.b = minecraft_colour_dark_component; break;
    case 'd': c.rg = color_rg_1_0p33; c.b = one_in_silent_rotation_and_chat_colors; break;
    case 'e': c.rg = color_rg_1_1; c.b = minecraft_colour_dark_component; break;
    default: c.rg = color_rg_1_1; c.b = one_in_silent_rotation_and_chat_colors; break;
    }
    return c;
}

// ---- chat output
FN(void *, get_minecraft, (void *jvm));
FN(void *, get_local_player, (void *jvm, void *minecraft));
FN(int, libc_vsnprintf, (char *buf, unsigned long long n, const char *fmt, va_list ap));   // vsnprintf
FN(void *, java_new_chat_component_text_init, (void *jvm, void *str));     // new ChatComponentText
FN(void, java_call_entity_player_sp_add_chat_message, (void *jvm, void *entity_player_sp, void *msg));
extern "C" char log_message_buffer[0x100];
extern "C" void jni_throw_format(Jvm *jvm, const char *fmt, ...) {
    void *minecraft = get_minecraft(jvm);
    void *player = get_local_player(jvm, minecraft);
    if (!player) return;
    __builtin_va_list va;
    __builtin_va_start(va, fmt);
    libc_vsnprintf(log_message_buffer, 0x100, fmt, va);
    JNIEnv *env = jvm->env;
    jstring chat_component_text = (*env)->NewStringUTF(env, log_message_buffer);
    if (!chat_component_text) return;
    void *t = java_new_chat_component_text_init(jvm, chat_component_text);
    if (!t) return;
    java_call_entity_player_sp_add_chat_message(jvm, player, t);
}

// ---- movement helpers
FN(float, libc_sinf, (float x));   // sinf
FN(float, libc_cosf, (float x));   // cosf
extern "C" const float movement_input_friction, threshold_0_0001, one_in_silent_rotation_and_chat_colors_a, threshold_1_in_silent_rotation_and_chat_colors_b;
extern "C" const double pi_in_silent_rotation_and_chat_colors, half_turn_degrees_in_silent_rotation_and_chat_colors;
// horizontal motion for (forward, strafe) input at the given yaw (EntityLivingBase.moveFlying)
extern "C" Vec3 horizontal_motion(Rot rotation, float fwd, float strafe, float speed) {
    fwd *= movement_input_friction;
    strafe *= movement_input_friction;
    float m = strafe * strafe + fwd * fwd;
    if (threshold_0_0001 > m) {
        Vec3 z = vec3_make(0.0, 0.0, 0.0);
        return z;
    }
    float d = __builtin_sqrtf(m);
    float k = speed * one_in_silent_rotation_and_chat_colors_a / (threshold_1_in_silent_rotation_and_chat_colors_b > d ? threshold_1_in_silent_rotation_and_chat_colors_b : d);
    strafe *= k;
    fwd *= k;
    float a = (float)((double)rotation.yaw * pi_in_silent_rotation_and_chat_colors / half_turn_degrees_in_silent_rotation_and_chat_colors);
    float sn = libc_sinf(a);
    float cs = libc_cosf(a);
    Vec3 v;
    float t = fwd * sn;

    v.x = (double)(strafe * cs - t);
    v.y = 0.0;
    v.z = (double)(fwd * cs + strafe * sn);
    return v;
}

// ---- world -> screen projection with the captured GL matrices
struct View { char padding_00[0x20]; float w, h; const float *mv, *proj; };
extern "C" const float one_in_silent_rotation_and_chat_colors_c, offset_0_5_in_silent_rotation_and_chat_colors;   // 1.0, 0.5
extern "C" bool project_point_ex(View *v, Vec3 p, F2 *out, float *depth) {
    float x = (float)p.x, y = (float)p.y, z = (float)p.z;
    const float *m = v->mv, *proj = v->proj;
    float e0 = m[0] * x + m[4] * y + m[8] * z + m[12];
    float e1 = m[1] * x + m[5] * y + m[9] * z + m[13];
    float e2 = m[2] * x + m[6] * y + m[10] * z + m[14];
    float e3 = m[3] * x + m[7] * y + m[11] * z + m[15];
    float w = proj[3] * e0 + proj[7] * e1 + proj[11] * e2 + proj[15] * e3;
    if (w == 0.0f) return false;
    float k = one_in_silent_rotation_and_chat_colors_c / w * offset_0_5_in_silent_rotation_and_chat_colors;
    float zz = (proj[2] * e0 + proj[6] * e1 + proj[10] * e2 + proj[14] * e3) * k + offset_0_5_in_silent_rotation_and_chat_colors;
    if (0.0f >= zz || zz >= one_in_silent_rotation_and_chat_colors_c) return false;
    float yy = (proj[1] * e0 + proj[5] * e1 + proj[9] * e2 + proj[13] * e3) * k + offset_0_5_in_silent_rotation_and_chat_colors;
    float xx = (proj[0] * e0 + proj[4] * e1 + proj[8] * e2 + proj[12] * e3) * k + offset_0_5_in_silent_rotation_and_chat_colors;
    out->a = xx * v->w;
    out->b = v->h - yy * v->h;
    if (depth) *depth = k;
    return true;
}
extern "C" bool project_point(View *v, Vec3 *p, F2 *out) { return project_point_ex(v, *p, out, nullptr); }

// ---- apply the pending mouse delta in whole sensitivity steps
FN(float, libc_truncf, (float v));                 // truncf
FN(void, java_entity_player_sp_render_arm_pitch, (void *jvm, void *object, Rot rotation));
FN(void, java_entity_player_sp_prev_render_arm_pitch, (void *jvm, void *object, Rot rotation));
typedef int v4i __attribute__((vector_size(16), aligned(16)));
typedef float v4f __attribute__((vector_size(16), aligned(16)));
extern "C" const v4i gl_vertex_state_sign_mask;           // sign mask
extern "C" void set_rotation_active(void *jvm, void *object, bool render) {
    float step = mouse_sensitivity_step;
    if (step == 0.0f) return;
    float neg = ((v4f)((v4i)(v4f){step} ^ gl_vertex_state_sign_mask))[0];
    float dx = 0.0f, dy = 0.0f;
    float yaw = silent_rotation_offset.yaw;
    if (neg >= yaw || yaw >= step) dx = libc_truncf(yaw / step) * step;
    silent_rotation_offset.yaw = yaw - dx;
    float pitch = silent_rotation_offset.pitch;
    if (neg >= pitch || pitch >= step) dy = libc_truncf(pitch / step) * step;
    silent_rotation_offset.pitch = pitch - dy;
    if (dx == 0.0f && dy == 0.0f) return;
    if (!object) return;   // the rotation accessors below bail on null; the arm-pitch ones are raw JNI writes
    Rot rotation = rot_add(get_entity_rotation(jvm, object), Rot{dx, dy});
    set_entity_rotation(jvm, object, rotation);
    set_entity_prev_rotation(jvm, object, rotation);
    if (!render) return;
    java_entity_player_sp_prev_render_arm_pitch(jvm, object, rotation);
    java_entity_player_sp_render_arm_pitch(jvm, object, rotation);
}

// ---- movement fix: remap the pressed WASD keys to the server-side yaw
FN(bool, is_in_game, (void *jvm, void *minecraft));
FN(void *, java_get_game_settings_forward_key, (void *jvm, void *game_settings));      // keyBindForward
FN(void *, java_get_game_settings_back_key, (void *jvm, void *game_settings));      // keyBindBack
FN(void *, java_get_game_settings_left_key, (void *jvm, void *game_settings));      // keyBindLeft
FN(void *, java_get_game_settings_right_key, (void *jvm, void *game_settings));      // keyBindRight
FN(bool, is_key_down, (void *jvm, void *kb));        // pressed
FN(void, java_set_key_binding_pressed, (void *jvm, void *kb, bool pressed));
FN(float, libc_roundf, (float v));                 // roundf
extern "C" unsigned char gl_vertex_state_captured_key_bits_fwd_back;        // captured key bits: fwd, back, left, right
extern "C" const float divisor_45;         // 45
extern "C" const unsigned short gl_vertex_state_octant_bytes[8];   // octant -> (fwd, strafe) bytes
extern "C" void pack_flags(void *j, bool apply) {
    void *minecraft = get_minecraft(j);
    if (!is_in_game(j, minecraft)) return;
    void *game_settings = get_game_settings(j, minecraft);
    void *game_settings_key_bindings = java_get_game_settings_forward_key(j, game_settings), *key_binding = java_get_game_settings_back_key(j, game_settings), *kl = java_get_game_settings_left_key(j, game_settings), *kr = java_get_game_settings_right_key(j, game_settings);
    if (!apply) {
        java_set_key_binding_pressed(j, game_settings_key_bindings, gl_vertex_state_captured_key_bits_fwd_back & 1);
        java_set_key_binding_pressed(j, key_binding, (gl_vertex_state_captured_key_bits_fwd_back >> 1) & 1);
        java_set_key_binding_pressed(j, kl, (gl_vertex_state_captured_key_bits_fwd_back >> 2) & 1);
        java_set_key_binding_pressed(j, kr, (gl_vertex_state_captured_key_bits_fwd_back >> 3) & 1);
        return;
    }
    unsigned char a = is_key_down(j, game_settings_key_bindings);
    unsigned char b = is_key_down(j, key_binding);
    unsigned char bits = (unsigned char)(b + b) | a;
    unsigned char c = is_key_down(j, kl);
    bits = (unsigned char)(c << 2) | bits;
    unsigned char d = is_key_down(j, kr);
    gl_vertex_state_captured_key_bits_fwd_back = (unsigned char)(d << 3) | bits;
    void *player = get_local_player(j, minecraft);
    float delta = wrap_angle(get_entity_rotation(j, player).yaw - render_rotation.yaw);
    unsigned f = gl_vertex_state_captured_key_bits_fwd_back;
    signed char fwd = (signed char)((f & 1) + ((signed char)(f << 6) >> 7));
    signed char str = (signed char)(((signed char)(f << 4) >> 7) + ((f >> 2) & 1));
    unsigned short mv;
    if (fwd == 0 && str == 0) {
        mv = 0;
    } else {
        int dir;
        if (fwd == 0) dir = str == 1 ? 2 : (str == -1 ? 6 : 0);
        else if (fwd == -1) dir = 5 - (unsigned char)(str + 1);
        else if (str == 0) dir = 0;
        else dir = str == 1 ? 1 : (str == -1 ? 7 : 0);
        mv = gl_vertex_state_octant_bytes[((int)libc_roundf(delta / divisor_45) + dir) & 7];
    }
    unsigned hi = mv >> 8;
    java_set_key_binding_pressed(j, game_settings_key_bindings, (mv & 0xff) == 1);
    java_set_key_binding_pressed(j, key_binding, (mv & 0xff) == 0xff);
    java_set_key_binding_pressed(j, kl, hi == 1);
    java_set_key_binding_pressed(j, kr, hi == 0xff);
}

// ---- silent-rotation controller: start (save real rotation) / stop (restore smoothly)
extern "C" unsigned char use_target_rotation;
extern "C" void apply_player_rotation(Ctx *ctx, bool on, unsigned char moveFix, unsigned char useTarget, Rot target) {
    if (rotation_reset_done == 1) {
        pack_flags(ctx->jvm, false);
        rotation_reset_done = 0;
        rotation_reset_requested = 0;
    }
    if (on) {
        if (!(silent_rotation_active_flag & 1) && ctx->player) {
            silent_rotation_active_flag = 1;
            silent_rotation_current = get_entity_rotation(ctx->jvm, ctx->player);
        }
        use_target_rotation = useTarget;
        render_rotation = target;
        rotation_reset_requested = moveFix;
        return;
    }
    if (!(silent_rotation_active_flag & 1)) return;
    silent_rotation_active_flag = 0;
    Rot *src = use_target_rotation ? &render_rotation : &silent_rotation_current;
    float yaw = src->yaw, pitch = src->pitch;
    float cur = get_entity_rotation(ctx->jvm, ctx->player).yaw;
    Rot rotation = {wrap_angle(yaw - cur) + cur, pitch};
    set_entity_rotation(ctx->jvm, ctx->player, rotation);
    set_entity_prev_rotation(ctx->jvm, ctx->player, rotation);
    java_entity_player_sp_prev_render_arm_pitch(ctx->jvm, ctx->player, rotation);
    java_entity_player_sp_render_arm_pitch(ctx->jvm, ctx->player, rotation);
}

// wrap an angle into range when it lies outside (-limit, limit); limit = mouse_sensitivity_step
FN(float, libc_truncf, (float x));   // floorf
extern "C" void clamp_angle(float *angle) {
    float v = *angle, limit = mouse_sensitivity_step;
    float wrap = 0.0f;
    if (v <= -limit || v >= limit) wrap = libc_truncf(v / limit) * limit;
    *angle = v - wrap;
}