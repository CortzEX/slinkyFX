// Indicators - off-screen arrows pointing at incoming projectiles (10 arrow, 12 fireball, 14 pearl).
//   0x98 disable: release every cached global reference
//   0x12 tick: collect up to 10 projectiles with their distance labels
//   0xb8 render: rotate an arrow around the crosshair towards each projectile
#include "slinky/engine.h"
#include "slinky/jids.h"

struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
struct Indicator {                  // 0x38 bytes, array at 0x301eb0
    double x0, z0, x1, z1;          // current / previous position
    int kind;                       // 10, 12 or 14
    void *ref;                      // global ref to the distance label
    int width;                      // label width in pixels
    unsigned color;
};

extern "C" char indicators_module_descriptor[];                // Indicators module descriptor
extern "C" unsigned long long indicators_cached_count;    // cached count
extern "C" Indicator indicators_secondary_refs[10];

extern "C" void indicators_disable(EvHook *hook) {
    if (hook->on || hook->ev != indicators_module_descriptor)
        return;
    unsigned long long n = indicators_cached_count;
    if (n) {
        JNIEnv *env = ((Jvm *)hook->ctx->jvm)->env;
        for (unsigned long long i = 0; i < n; i++) {
            void *ref = indicators_secondary_refs[i].ref;
            if (!ref)
                continue;
            (*env)->DeleteGlobalRef(env, ref);
        }
    }
    indicators_cached_count = 0;
}

typedef int v4i __attribute__((vector_size(16), aligned(16)));
typedef float v4f __attribute__((vector_size(16), aligned(16)));
extern "C" const v4i indicators_sign_mask;      // sign mask
static inline __attribute__((always_inline)) float neg(float x) {
    v4f v = {x, 0, 0, 0};
    return ((v4f)((v4i)v ^ indicators_sign_mask))[0];
}
struct Pair { float x, y;
#ifdef __linux__
    ~Pair() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct TickEvt { char padding_00[8]; Ctx *ctx; };
struct RenderEvt { char padding_00[8]; double cam_x; char padding_10[8]; double cam_z; Ctx *ctx; void *screen; char padding_30[0x10]; float partial; unsigned char shader; };
extern "C" Indicator indicators_secondary_refs[10];
extern "C" char indicators_label_buffer[10];                       // label buffer
extern "C" unsigned char indicators_show_kind_10_12_14, indicators_show_kind_10_12_14_second, indicators_show_kind_10_12_14_third;   // show kind 10 / 12 / 14
extern "C" unsigned char indicators_only_approaching_items;                  // only approaching items
extern "C" float indicators_arrow_scale_radius, indicators_arrow_scale_radius_second;             // arrow scale / radius
extern "C" const double factor_20_in_indicators, factor_0_5_in_indicators, half_turn_degrees_in_indicators, pi_in_indicators, offset_neg_90_in_indicators, threshold_15, threshold_30_in_indicators;
extern "C" const float scale_0_5_in_indicators, gl_colour_component_1_in_indicators, indicator_vertical_translate, five_in_indicators;
extern "C" const float indicators_colour_per_kind_10[], indicators_colour_per_kind_10_second[], indicators_colour_per_kind_10_third[];   // colour per kind - 10
S(fmt_float_0dp_m);   // "%.0fm"
extern "C" void (**ptr_to_pGlPushMatrix)();                                 // glPushMatrix
extern "C" void (**ptr_to_pGlPopMatrix)();                                 // glPopMatrix
extern "C" void (**ptr_to_pGlTranslatef)(float, float, float);              // glTranslatef
extern "C" void (**ptr_to_pGlScalef)(float, float, float);              // glScalef
extern "C" void (**ptr_to_pGlRotatef)(float, float, float, float);       // glRotatef
extern "C" void (**ptr_to_pGlLineWidth)(float);                            // glLineWidth
extern "C" void (**ptr_to_pGlEnable)(unsigned);                         // glEnable
extern "C" void (**ptr_to_pGlDisable)(unsigned);                         // glDisable
extern "C" void (**ptr_to_pGlColor4f)(float, float, float, float);       // glColor4f

FN(void *, java_get_minecraft_render_manager, (void *jvm, void *minecraft));
FN(void *, java_get_render_manager_text_renderer, (void *jvm, void *render_manager));
FN(float, java_get_render_manager_player_view_y, (void *jvm, void *render_manager));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(Vec3, java_get_entity_prev_pos_x, (void *jvm, void *entity));
FN(void *, java_get_world_loaded_entity_list, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(int, java_call_static_entity_list_get_entity_id, (void *jvm, void *entity_list));
FN(unsigned char, java_get_entity_arrow_in_ground, (void *jvm, void *entity_arrow));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(int, java_call_font_renderer_get_string_width, (void *jvm, void *font, void *str));
FN(void, java_font_draw_string, (void *jvm, void *font, void *str, float x, float y, unsigned color, unsigned char shadow));
FN(void, gl_bind_texture, (void *jvm));
FN(void, gl_clear_color, (void *jvm));
FN(void, gl_set_color, (void *jvm, float r, float g, float b, float al));
FN(void, gl_array_xy_float2, ());
FN(void, gl_array_color5, ());
FN(void, gl_array_color6, ());
FN(void, disable_vertex_array_state, ());
FN(Pair, rot_scale, (Pair screen, float f));
FN(double, time_now, ());
FN(double, libc_sin, (double x));
FN(float, lerp_unclamped_float, (float a, float b, float t));
FN(double, libc_atan2, (double y, double x));
FN(float, wrap_angle, (float deg));
FN(double, libc_sqrt, (double x));   // sqrt (negative input path)

typedef void *(*NewGlobalRef)(void *env, void *o);

extern "C" void indicators_tick(TickEvt *h) {
    Ctx *ctx = h->ctx;
    if (indicators_cached_count) {
        JNIEnv *env = ((Jvm *)ctx->jvm)->env;
        for (unsigned long long i = 0; i < indicators_cached_count; i++)
            if (indicators_secondary_refs[i].ref)
                (*env)->DeleteGlobalRef(env, indicators_secondary_refs[i].ref);
    }
    indicators_cached_count = 0;
    // event 0x12 dispatches with the world it was called on, and the render player can still be missing while
    // a fresh world is already installed; a NULL jobject passed to a JNI field read faults
    if (!ctx->world || !ctx->player)
        return;
    void *font = java_get_render_manager_text_renderer(ctx->jvm, java_get_minecraft_render_manager(ctx->jvm, ctx->minecraft));
    if (!font)
        return;
    Vec3 position = java_get_entity_pos_x(ctx->jvm, ctx->player);
    void *list = java_get_world_loaded_entity_list(ctx->jvm, ctx->world);
    for (int i = 0; i < java_call_list_size(ctx->jvm, list); i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        int kind = java_call_static_entity_list_get_entity_id(ctx->jvm, element);
        if (kind == 14) {
            if (!indicators_show_kind_10_12_14_third)
                continue;
        } else if (kind == 12) {
            if (!indicators_show_kind_10_12_14_second)
                continue;
        } else if (kind == 10) {
            if (!indicators_show_kind_10_12_14 || java_get_entity_arrow_in_ground(ctx->jvm, element))
                continue;
        } else {
            continue;
        }
        Vec3 cur = java_get_entity_pos_x(ctx->jvm, element);
        Vec3 prev = java_get_entity_prev_pos_x(ctx->jvm, element);
        double d2 = vec3_distance_sq(cur, position);
        if (indicators_only_approaching_items == 1 && d2 > vec3_distance_sq(prev, position))
            continue;                           // moving away
        double dist;
        if (d2 >= 0.0)
            dist = __builtin_sqrt(d2);
        else
            dist = libc_sqrt(d2);
        Indicator *ind = &indicators_secondary_refs[indicators_cached_count++];
        ind->x0 = cur.x;
        ind->z0 = cur.z;
        ind->x1 = prev.x;
        ind->z1 = prev.z;
        ind->kind = kind;
        JNIEnv *env = ((Jvm *)ctx->jvm)->env;
        log_fmt(indicators_label_buffer, 10, fmt_float_0dp_m, dist);
        void *str = (*env)->NewStringUTF(env, indicators_label_buffer);
        if (!str) {
            ind->ref = nullptr;
        } else {
            ind->ref = (*env)->NewGlobalRef(env, str);
            (*env)->DeleteLocalRef(env, str);
            if (ind->ref) {
                ind->width = java_call_font_renderer_get_string_width(ctx->jvm, font, ind->ref);
                unsigned col = dist > threshold_15 ? 0xffffff00u : 0xffff0000u;
                ind->color = dist > threshold_30_in_indicators ? 0xffffffffu : col;
            }
        }
        if (indicators_cached_count == 10)
            return;
    }
}

extern "C" void indicators_render(RenderEvt *h) {
    if (!indicators_cached_count)
        return;
    Ctx *ctx = h->ctx;
    float partial = h->partial;
    void *minecraft = java_get_minecraft_render_manager(ctx->jvm, ctx->minecraft);
    void *font = java_get_render_manager_text_renderer(ctx->jvm, minecraft);
    float yaw = java_get_render_manager_player_view_y(ctx->jvm, minecraft);
    gl_array_xy_float2();
    Pair scr;
    __builtin_memcpy(&scr, &h->screen, sizeof scr);   // the slot holds the two floats packed, not a pointer
    Pair mid = rot_scale(scr, scale_0_5_in_indicators);
    (*ptr_to_pGlPushMatrix)();
    (*ptr_to_pGlTranslatef)(mid.x, mid.y, 0.0f);
    (*ptr_to_pGlLineWidth)(gl_colour_component_1_in_indicators);
    (*ptr_to_pGlEnable)(0xb20);
    double wave = libc_sin(time_now() * factor_20_in_indicators);
    float pulse = (float)(wave * factor_0_5_in_indicators + factor_0_5_in_indicators);
    for (unsigned long long i = 0; i < indicators_cached_count; i++) {
        Indicator *ind = &indicators_secondary_refs[i];
        double x = lerp_unclamped_float((float)ind->x1, (float)ind->x0, partial);
        double z = lerp_unclamped_float((float)ind->z1, (float)ind->z0, partial);
        double ang = libc_atan2(z - h->cam_z, x - h->cam_x);
        float a = wrap_angle((float)(ang * half_turn_degrees_in_indicators / pi_in_indicators + offset_neg_90_in_indicators) - yaw);
        (*ptr_to_pGlPushMatrix)();
        (*ptr_to_pGlRotatef)(a, 0.0f, 0.0f, gl_colour_component_1_in_indicators);
        float scale = indicators_arrow_scale_radius;
        (*ptr_to_pGlTranslatef)(0.0f, neg(indicators_arrow_scale_radius_second), 0.0f);
        (*ptr_to_pGlScalef)(scale, scale, scale);
        if (ind->ref) {
            (*ptr_to_pGlPushMatrix)();
            (*ptr_to_pGlRotatef)(neg(a), 0.0f, 0.0f, gl_colour_component_1_in_indicators);
            if (h->shader == 1) {
                gl_bind_texture(ctx->jvm);
                gl_set_color(ctx->jvm, gl_colour_component_1_in_indicators, gl_colour_component_1_in_indicators, gl_colour_component_1_in_indicators, 1.0f);
            } else {
                (*ptr_to_pGlEnable)(0xde1);
            }
            java_font_draw_string(ctx->jvm, font, ind->ref, (float)(-ind->width * factor_0_5_in_indicators), -5.0f, ind->color, 1);
            if (h->shader == 1)
                gl_clear_color(ctx->jvm);
            else
                (*ptr_to_pGlDisable)(0xde1);
            (*ptr_to_pGlPopMatrix)();
        }
        (*ptr_to_pGlTranslatef)(0.0f, indicator_vertical_translate, 0.0f);
        (*ptr_to_pGlScalef)(five_in_indicators, five_in_indicators, five_in_indicators);
        int k = ind->kind - 10;
        (*ptr_to_pGlColor4f)(indicators_colour_per_kind_10[k], indicators_colour_per_kind_10_second[k], indicators_colour_per_kind_10_third[k], pulse);
        gl_array_color5();
        (*ptr_to_pGlColor4f)(0.0f, 0.0f, 0.0f, pulse);
        gl_array_color6();
        (*ptr_to_pGlPopMatrix)();
    }
    (*ptr_to_pGlPopMatrix)();
    disable_vertex_array_state();
}
