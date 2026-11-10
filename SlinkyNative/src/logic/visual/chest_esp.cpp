// Chest ESP - descriptor +0x48 (collect containers in range, max 100) and
// +0xa8 (render a box per container with the per-type colour).
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Found { Vec3 pos; int id; int padding_1c; };          // 0x20 bytes

extern "C" float chest_esp_range_value;            // "range"
extern "C" unsigned char chest_esp_render_chest;    // "render_chest"
extern "C" unsigned char chest_esp_render_ender_chest;    // "render_ender_chest"
extern "C" unsigned char chest_esp_render_trapped_chest;    // "render_trapped_chest"
extern "C" unsigned char chest_esp_render_beacon;    // "render_beacon"
extern "C" float chest_esp_line_width_value;            // "line_width"
extern "C" float chest_esp_color_ender_chest_value[];          // "color_ender_chest"
extern "C" float chest_esp_color_trapped_chest_value[];          // "color_trapped_chest"
extern "C" float chest_esp_color_beacon_value[];          // "color_beacon"
extern "C" float chest_esp_color_chest_value[];          // "color_chest"
extern "C" unsigned char chest_esp_filled_value;    // "filled"
extern "C" unsigned long long chest_esp_found_count;   // found count
extern "C" Found chest_esp_chests[100];
extern "C" void (**ptr_to_pGlLineWidth)(float);                    // glLineWidth
extern "C" void (**ptr_to_pGlColor3f)(float, float, float);      // glColor3f
extern "C" void (**ptr_to_pGlPopMatrix)();                         // glPopMatrix
extern "C" void (**ptr_to_pGlPushMatrix)();                         // glPushMatrix
extern "C" void (**ptr_to_pGlTranslatef)(float, float, float);      // glTranslatef
extern "C" void (**ptr_to_pGlScalef)(float, float, float);      // glScalef
extern "C" void (**ptr_to_pGlColor4f)(float, float, float, float);  // glColor4f
extern "C" const double chest_esp_0_5_0_5[2];  // {0.5, 0.5}
extern "C" const float half_in_chest_esp;      // box scale
extern "C" const float chest_esp_box_alpha;      // fill alpha

FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(void *, java_get_world_loaded_tile_entity_list, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(void *, java_get_tile_entity_block_type, (void *jvm, void *te));
FN(int, get_block_id, (void *jvm, void *blk));
FN(Vec3, java_get_block_pos_as_vec3, (void *jvm, void *te));
FN(double, vec3_distance_sq, (Vec3 a, Vec3 b));
FN(void, gl_array_xyz_float, (void *h));
FN(Vec3, vec3_sub, (Vec3 a, Vec3 b));
FN(Vec3, vec3_add, (Vec3 a, Vec3 b));
FN(void, gl_array_color1, ());
FN(void, gl_array_color2, ());
FN(void, disable_vertex_array_state, ());

extern "C" void chest_esp_on_tick(Hook *hook) {
    chest_esp_found_count = 0;
    Ctx *ctx = hook->ctx;
    // event 0 dispatches without checking the player, and the position read faults on a NULL jobject
    if (!ctx->world || !ctx->player)
        return;
    Vec3 eye = java_get_entity_pos_x(ctx->jvm, ctx->player);
    void *list = java_get_world_loaded_tile_entity_list(ctx->jvm, ctx->world);
    for (int i = 0; i < java_call_list_size(ctx->jvm, list); i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        void *blk = java_get_tile_entity_block_type(ctx->jvm, element);
        if (!blk)
            continue;
        int block_id = get_block_id(ctx->jvm, blk);
        if (block_id > 0x89) {
            if (block_id == 0x8a) {
                if (chest_esp_render_beacon != 1) continue;
            } else if (block_id == 0x92) {
                if (!chest_esp_render_trapped_chest) continue;
            } else {
                continue;
            }
        } else if (block_id == 0x36) {
            if (!chest_esp_render_chest) continue;
        } else if (block_id == 0x82) {
            if (!chest_esp_render_ender_chest) continue;
        } else {
            continue;
        }
        Vec3 block_pos_a = java_get_block_pos_as_vec3(ctx->jvm, element);
        if (vec3_distance_sq(block_pos_a, eye) > (double)(chest_esp_range_value * chest_esp_range_value))
            continue;
        unsigned long long n = chest_esp_found_count;
        chest_esp_found_count = n + 1;
        chest_esp_chests[n].pos = block_pos_a;
        chest_esp_chests[n].id = block_id;
        if (n + 1 == 100)
            return;
    }
}

extern "C" void chest_esp_on_render_world(char *h) {
    gl_array_xyz_float(h);
    (*ptr_to_pGlLineWidth)(chest_esp_line_width_value);
    if (chest_esp_found_count) {
        Vec3 *cam = (Vec3 *)(h + 8);
        Found *f = chest_esp_chests;
        for (unsigned long long i = 0; i < chest_esp_found_count; i++, f++) {
            float *col = chest_esp_color_ender_chest_value;
            if (f->id == 0x82)
                ;
            else if (f->id == 0x8a)
                col = chest_esp_color_beacon_value;
            else if (f->id == 0x92)
                col = chest_esp_color_trapped_chest_value;
            else
                col = chest_esp_color_chest_value;
            Vec3 ctr = vec3_add(vec3_sub(f->pos, *cam), Vec3{chest_esp_0_5_0_5[0], chest_esp_0_5_0_5[1], 0.5});
            (*ptr_to_pGlPushMatrix)();
            (*ptr_to_pGlTranslatef)((float)ctr.x, (float)ctr.y, (float)ctr.z);
            (*ptr_to_pGlScalef)(half_in_chest_esp, half_in_chest_esp, half_in_chest_esp);
            if (chest_esp_filled_value == 1) {
                (*ptr_to_pGlColor4f)(col[0], col[1], col[2], chest_esp_box_alpha);
                gl_array_color1();
            }
            (*ptr_to_pGlColor3f)(col[0], col[1], col[2]);
            gl_array_color2();
            (*ptr_to_pGlPopMatrix)();
        }
    }
    disable_vertex_array_state();
}
