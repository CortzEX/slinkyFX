// Pointers - arrows around the crosshair towards players outside the view.
//   0x3df10 tick: collect up to 100 players in range but out of view, sorted
//   0x3da80 render: one coloured arrow per player
#include "slinky/engine.h"
#include "slinky/jids.h"

typedef int v4i __attribute__((vector_size(16), aligned(16)));

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Color3 { float r, g, b;
#ifdef __linux__
    ~Color3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Pair { float x, y;
#ifdef __linux__
    ~Pair() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Pointer {                    // 0x38 bytes at 0x3008b0
    double x, z, px, pz;            // current / previous position
    Color3 color;
    float prev_dist, dist;
    int padding_34;
};
struct TickEvt { char padding_00[8]; Ctx *ctx; };
struct RenderEvt { char padding_00[8]; double cam_x; char padding_10[8]; double cam_z; Ctx *ctx; void *screen; char padding_30[0x10]; float partial; };
extern "C" unsigned long long pointer_count;     // count
extern "C" Pointer pointer_entries[100];
extern "C" float pointers_range;                  // range
extern "C" float pointers_skip_when_within_this_many;                  // skip when within this many degrees of the view
extern "C" unsigned char pointers_hide_friendlies;          // also skip invisible
extern "C" int pointers_color_mode;                    // colour mode: 0 distance gradient, 1 team, 2 friend
extern "C" Color3 pointers_friend_other_colour, pointers_friend_other_colour_second;    // friend / other colour
extern "C" int pointers_style;                    // 3d tilt
extern "C" float pointers_gradient_distance_range, pointers_gradient_distance_range_second;     // gradient distance range
extern "C" float pointers_size;                  // size
extern "C" float pointers_radius;                  // radius
extern "C" unsigned char pointers_pulse_radius_with_distance;          // pulse radius with distance
extern "C" float pointers_near_colour, pointers_near_colour_second, pointers_near_colour_third;   // near colour
extern "C" float pointers_far_colour, pointers_far_colour_second, pointers_far_colour_third;   // far colour
extern "C" const double half_turn_degrees_in_pointers_a, pi_in_pointers_a, offset_neg_90_in_pointers_a, half_turn_degrees_in_pointers, pi_in_pointers, offset_neg_90_in_pointers;
extern "C" const float scale_0_5_in_pointers, threshold_30_in_pointers, offset_90_in_pointers, one_in_pointers, range_1eneg_05, factor_10_in_pointers;
extern "C" const v4i pointers_abs_mask;             // abs mask
extern "C" const v4i pointers_sign_mask;             // sign mask
extern "C" void (**ptr_to_pGlPushMatrix)();                                 // glPushMatrix
extern "C" void (**ptr_to_pGlPopMatrix)();                                 // glPopMatrix
extern "C" void (**ptr_to_pGlTranslatef)(float, float, float);              // glTranslatef
extern "C" void (**ptr_to_pGlScalef)(float, float, float);              // glScalef
extern "C" void (**ptr_to_pGlRotatef)(float, float, float, float);       // glRotatef
extern "C" void (**ptr_to_pGlLineWidth)(float);                            // glLineWidth
extern "C" void (**ptr_to_pGlColor3f)(float, float, float);              // glColor3f
extern "C" void (**ptr_to_pGlEnable)(unsigned);                         // glEnable

FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(Vec3, java_get_entity_prev_pos_x, (void *jvm, void *entity));
FN(void *, java_get_minecraft_render_manager, (void *jvm, void *minecraft));
FN(float, java_get_render_manager_player_view_x, (void *jvm, void *minecraft));
FN(float, java_get_render_manager_player_view_y, (void *jvm, void *minecraft));
FN(void *, java_get_world_player_entities, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(unsigned char, java_get_entity_is_dead, (void *jvm, void *entity));
FN(int, entity_flags_get, (void *jvm, void *entity));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(double, vec3_distance, (Vec3 a, Vec3 b));
FN(double, libc_atan2, (double y, double x));
FN(float, wrap_angle, (float deg));
FN(float, lerp_unclamped_float, (float a, float b, float t));
FN(float, limit_rotation_delta, (float lo, float hi, float v));
FN(float, libc_sqrtf, (float x));                  // sqrtf (negative input path)
FN(int, entity_name_colour, (void *jvm, void *entity));
FN(Color3, minecraft_color_code_to_rgb, (int team));
FN(void, libc_qsort, (void *base, unsigned long long n, unsigned long long size, int (*cmp)(const void *, const void *)));
FN(int, compare_by_hurt_time_desc, (const void *left, const void *right));
FN(Pair, rot_scale, (Pair screen, float f));
FN(void, gl_array_xy_float2, ());
FN(void, gl_array_color5, ());
FN(void, gl_array_color6, ());
FN(void, disable_vertex_array_state, ());

typedef unsigned char (*IsSameObject)(void *env, void *a, void *b);

static inline __attribute__((always_inline)) float vabs(float x) {
    x = __builtin_fabsf(x);
    return x;
}
static inline __attribute__((always_inline)) float neg(float x) {
    x = -x;
    return x;
}

extern "C" void pointers_tick(TickEvt *h) {
    Ctx *ctx = h->ctx;
    pointer_count = 0;
    // event 0x12 dispatches with the world it was called on, and the render player can still be missing while
    // a fresh world is already installed; a NULL jobject passed to a JNI field read faults
    if (!ctx->world || !ctx->player)
        return;
    Vec3 position = java_get_entity_pos_x(ctx->jvm, ctx->player);
    Vec3 me_prev = java_get_entity_prev_pos_x(ctx->jvm, ctx->player);
    float yaw = java_get_render_manager_player_view_y(ctx->jvm, java_get_minecraft_render_manager(ctx->jvm, ctx->minecraft));
    void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
    int skip = pointers_hide_friendlies * 5 + 2;
    for (int i = 0; i < java_call_list_size(ctx->jvm, list); i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        JNIEnv *env = ((Jvm *)ctx->jvm)->env;
        if ((*env)->IsSameObject(env, ctx->player, element))
            continue;
        if (java_get_entity_is_dead(ctx->jvm, element))
            continue;
        int flags = entity_flags_get(ctx->jvm, element);
        if (flags & skip)
            continue;
        Vec3 pos = java_get_entity_pos_x(ctx->jvm, element);
        double d2 = vec3_distance_sq(pos, position);
        if (d2 > (double)(pointers_range * pointers_range))
            continue;
        double ang = libc_atan2(pos.z - position.z, pos.x - position.x);
        float off = vabs(wrap_angle((float)(ang * half_turn_degrees_in_pointers_a / pi_in_pointers_a + offset_neg_90_in_pointers_a) - yaw));
        if (pointers_skip_when_within_this_many > off)
            continue;                           // already on screen
        Vec3 prev = java_get_entity_prev_pos_x(ctx->jvm, element);
        Pointer *p = &pointer_entries[pointer_count++];
        p->x = pos.x;
        p->z = pos.z;
        p->px = prev.x;
        p->pz = prev.z;
        float f2 = (float)d2;

        float dist;
        if (f2 >= 0.0f)
            dist = __builtin_sqrtf(f2);
        else
            dist = libc_sqrtf(f2);
        p->dist = dist;
        p->prev_dist = (float)vec3_distance(me_prev, prev);
        if (pointers_color_mode == 2)
            p->color = (flags & 5) ? pointers_friend_other_colour : pointers_friend_other_colour_second;
        else if (pointers_color_mode == 1)
            p->color = minecraft_color_code_to_rgb(entity_name_colour(ctx->jvm, element));
        if (pointer_count == 100)
            break;
    }
    libc_qsort(pointer_entries, pointer_count, 0x38, compare_by_hurt_time_desc);
}

extern "C" void pointers_render(RenderEvt *h) {
    if (!pointer_count)
        return;
    Ctx *ctx = h->ctx;
    float partial = h->partial;
    void *minecraft = java_get_minecraft_render_manager(ctx->jvm, ctx->minecraft);
    float fov = java_get_render_manager_player_view_x(ctx->jvm, minecraft);
    float yaw = java_get_render_manager_player_view_y(ctx->jvm, minecraft);
    gl_array_xy_float2();
    Pair scr;
    __builtin_memcpy(&scr, &h->screen, sizeof scr);   // the slot holds the two floats packed, not a pointer
    Pair mid = rot_scale(scr, scale_0_5_in_pointers);
    (*ptr_to_pGlPushMatrix)();
    const float one = one_in_pointers;
    if (pointers_style) {
        float m = threshold_30_in_pointers > fov ? threshold_30_in_pointers : fov;
        (*ptr_to_pGlTranslatef)(mid.x, mid.y, 0.0f);
        (*ptr_to_pGlRotatef)(offset_90_in_pointers - m, one, 0.0f, 0.0f);
    } else {
        (*ptr_to_pGlTranslatef)(mid.x, mid.y, 0.0f);
    }
    (*ptr_to_pGlLineWidth)(one);
    (*ptr_to_pGlEnable)(0xb20);
    float range = pointers_gradient_distance_range_second - pointers_gradient_distance_range;
    if (range == 0.0f)
        range = range_1eneg_05;
    for (unsigned long long i = 0; i < pointer_count; i++) {
        Pointer *p = &pointer_entries[i];
        double x = lerp_unclamped_float((float)p->px, (float)p->x, partial);
        double z = lerp_unclamped_float((float)p->pz, (float)p->z, partial);
        double ang = libc_atan2(z - h->cam_z, x - h->cam_x);
        float a = wrap_angle((float)(ang * half_turn_degrees_in_pointers / pi_in_pointers + offset_neg_90_in_pointers) - yaw);
        (*ptr_to_pGlPushMatrix)();
        (*ptr_to_pGlRotatef)(a, 0.0f, 0.0f, one);
        float s = pointers_size * factor_10_in_pointers;
        (*ptr_to_pGlScalef)(s, s, s);
        float d = lerp_unclamped_float(p->prev_dist, p->dist, partial);
        float k = limit_rotation_delta(0.0f, one, (d - pointers_gradient_distance_range) / range);
        float r = pointers_radius;
        if (pointers_pulse_radius_with_distance)
            r *= k + one;
        (*ptr_to_pGlTranslatef)(0.0f, neg(r), 0.0f);
        if (pointers_color_mode) {
            (*ptr_to_pGlColor3f)(p->color.r, p->color.g, p->color.b);
        } else {
            float cr = lerp_unclamped_float(pointers_near_colour, pointers_far_colour, k);
            float cg = lerp_unclamped_float(pointers_near_colour_second, pointers_far_colour_second, k);
            float cb = lerp_unclamped_float(pointers_near_colour_third, pointers_far_colour_third, k);
            (*ptr_to_pGlColor3f)(cr, cg, cb);
        }
        gl_array_color5();
        (*ptr_to_pGlColor3f)(0.0f, 0.0f, 0.0f);
        gl_array_color6();
        (*ptr_to_pGlPopMatrix)();
    }
    (*ptr_to_pGlPopMatrix)();
    disable_vertex_array_state();
}
