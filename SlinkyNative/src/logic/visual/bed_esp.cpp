// Bed ESP - scans for beds in BedWars games, renders bed defense layers with team colors.
//   +0x48 tick: scan for beds, collect defense layers
//   +0x98 render: draw bed plates with defense HUD
#include "slinky/engine.h"

struct Vec3 { double x, y, z; };
struct Color3 { float r, g, b; };
struct Pair { float x, y; };
struct BedDefenseEntry {
    void *item_stack;        // +0x00 item stack or null
    void *sprite;            // +0x08 texture sprite or null
    int count;               // +0x10 block count
    int layer_index;         // +0x14
};
struct BedPlate {
    int cx, cz;              // chunk coordinates of bed head
    int bed_x, bed_y, bed_z; // head position
    int foot_x, foot_y, foot_z; // foot position
    Color3 team_color;       // team color
    unsigned long long entry_count, entry_cap;
    BedDefenseEntry *entries;
};
struct BedMesh {
    int cx, cz;
    unsigned long long a, b;
    struct Buf { void *p; unsigned long long n, cap; } lines, verts;
    unsigned short *idx;
};
struct TickEvt { char padding_00[8]; Ctx *ctx; };
struct RenderEvt { char padding_00[8]; Vec3 cam; Ctx *ctx; float partial; };

extern "C" char bed_esp_module_descriptor[];
extern "C" unsigned char bed_esp_module_enabled;
extern "C" float bed_esp_range_value;
extern "C" unsigned char bed_esp_show_bed_defense;
extern "C" unsigned char bed_esp_defence_hud;
extern "C" float bed_esp_opacity_value;
extern "C" float bed_esp_rounding_value;
extern "C" float bed_esp_scale_value;
extern "C" unsigned char bed_esp_auto_scale;
extern "C" unsigned char bed_esp_show_block_count;
extern "C" unsigned char bed_esp_exposed_outline;
extern "C" unsigned char bed_esp_outline_exposed_layers;
extern "C" unsigned bed_esp_bed_team_color_mode;

extern "C" unsigned long long bed_esp_plate_count, bed_esp_plate_cap;
extern "C" BedPlate *bed_esp_plates;

extern "C" unsigned long long bed_esp_scan_id_count, bed_esp_scan_id_cap;
extern "C" void *bed_esp_scan_id_entries;

extern "C" const double half_in_bed_esp;        // 0.5
extern "C" const float plate_height_offset;     // 0.6
extern "C" const float outline_expansion;       // 1.25
extern "C" const float auto_scale_reference;    // 8.0

extern "C" void (**ptr_to_pGlEnable)(unsigned);
extern "C" void (**ptr_to_pGlDisable)(unsigned);
extern "C" void (**ptr_to_pGlEnableClientState)(unsigned);
extern "C" void (**ptr_to_pGlDisableClientState)(unsigned);
extern "C" void (**ptr_to_pGlLineWidth)(float);
extern "C" void (**ptr_to_pGlPushMatrix)();
extern "C" void (**ptr_to_pGlPopMatrix)();
extern "C" void (**ptr_to_pGlTranslatef)(float, float, float);
extern "C" void (**ptr_to_pGlScalef)(float, float, float);
extern "C" void (**ptr_to_pGlRotatef)(float, float, float, float);
extern "C" void (**ptr_to_pGlColor3f)(float, float, float);
extern "C" void (**ptr_to_pGlColor4f)(float, float, float, float);
extern "C" void (**ptr_to_pGlVertexPointer)(int, unsigned, int, const void *);
extern "C" void (**ptr_to_pGlColorPointer)(int, unsigned, int, const void *);
extern "C" void (**ptr_to_pGlDrawElements)(unsigned, unsigned long long, unsigned, const void *);
extern "C" void (**ptr_to_pGlDepthMask)(unsigned);
extern "C" void (**ptr_to_pGlDepthFunc)(unsigned);
extern "C" void (**ptr_to_pGlDepthRange)(double, double);
extern "C" void (**ptr_to_pGlEnableClientState)(unsigned);
extern "C" void (**ptr_to_pGlDisableClientState)(unsigned);

FN(void, libc_free, (void *ptr));
FN(void *, libc_calloc, (unsigned long long n, unsigned long long size));
FN(void *, libc_realloc, (void *ptr, unsigned long long size));
FN(int, java_call_object_hash_code, (void *jvm, void *world));
FN(void *, get_block_state_id, (void *jvm, void *block_state));
FN(int, get_block_id, (void *jvm, void *block_state_id));
FN(void *, java_get_minecraft_render_manager, (void *jvm, void *minecraft));
FN(float, java_get_render_manager_player_view_y, (void *jvm, void *minecraft));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(int, java_get_moving_object_position_type_ordinal, (void *jvm, void *mop));
FN(void *, java_call_entity_get_command_sender_name, (void *jvm, void *entity));
FN(void *, java_jni_is_instance_of_entity_living_base, (void *jvm, void *entity));
FN(void *, java_call_entity_living_base_get_team, (void *jvm, void *entity_living_base));
FN(void *, java_jni_is_instance_of_score_player_team, (void *jvm, void *team));
FN(jstring, java_get_score_player_team, (void *jvm, void *score_player_team));
FN(int, entity_name_colour, (void *jvm, void *entity));
FN(Color3, minecraft_color_code_to_rgb, (int team));
FN(void, bed_esp_queue_loaded_chunks, (Ctx *ctx));
FN(void, scan_bed_blocks, ());
FN(void, bed_esp_rebuild_defense, (BedPlate *plate));
FN(unsigned char, project_point, (void *view, Vec3 p, Pair *out));
FN(void, bed_esp_draw_rounded_rect, (float x0, float y0, float x1, float y1, int color, float rounding));
FN(void, bed_esp_draw_stroked_rounded_rect, (float x0, float y0, float x1, float y1, int color, float rounding, float width));
FN(void, bed_esp_fill_rounded_rect, (float x0, float y0, float x1, float y1, int color, float rounding));
FN(void, bed_esp_draw_item_texture, (void *buffer, void *item_stack, float x, float y, float scale));
FN(void, bed_esp_draw_sprite, (void *buffer, void *sprite, float x, float y, float scale));
FN(void, bed_esp_get_font_width, (const char *font_name, const char *text, float *out_width));
FN(void, bed_esp_get_font_height, (const char *font_name, float *out_height));
FN(void, bed_esp_draw_text, (void *buffer, const char *font_name, const char *text, float x, float y, float height, int color, bool shadow, float max_width));

static inline __attribute__((always_inline)) void clear_plates() {
    for (unsigned long long i = 0; i < bed_esp_plate_count; i++) {
        BedPlate *p = &bed_esp_plates[i];
        libc_free(p->entries);
        p->entries = nullptr;
        p->entry_count = p->entry_cap = 0;
    }
    libc_free(bed_esp_plates);
    bed_esp_plates = nullptr;
    bed_esp_plate_count = bed_esp_plate_cap = 0;
}

extern "C" void bed_esp_on_tick(TickEvt *h) {
    Ctx *ctx = h->ctx;
    if (!bed_esp_module_enabled)
        return;
    if (ctx->in_game == 1) {
        java_call_object_hash_code(ctx->jvm, ctx->world);
        bed_esp_queue_loaded_chunks(ctx);
    }
    if (!ctx->world) {
        clear_plates();
        return;
    }
}

extern "C" void bed_esp_on_toggle(void *hook) {
    struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
    EvHook *evh = (EvHook *)hook;
    if (evh->ev != bed_esp_module_descriptor)
        return;
    if (evh->on) {
        Ctx *ctx = evh->ctx;
        // Initialize bed scanner
        if (ctx->world) {
            bed_esp_queue_loaded_chunks(ctx);
        }
    } else {
        clear_plates();
        libc_free(bed_esp_scan_id_entries);
        bed_esp_scan_id_entries = nullptr;
        bed_esp_scan_id_count = bed_esp_scan_id_cap = 0;
    }
}

extern "C" void bed_esp_on_render(RenderEvt *h) {
    if (!bed_esp_module_enabled || !bed_esp_plate_count)
        return;
    if (!bed_esp_show_bed_defense && !bed_esp_defence_hud)
        return;

    void (**enable)(unsigned) = ptr_to_pGlEnable;
    (*enable)(0xbe2);  // GL_BLEND
    (*enable)(0x809);  // GL_ALPHA_TEST
    (*ptr_to_pGlDepthMask)(0);
    (*ptr_to_pGlDepthFunc)(0x203);  // GL_LEQUAL
    (*ptr_to_pGlLineWidth)(1.0f);

    Ctx *ctx = h->ctx;
    float yaw = java_get_render_manager_player_view_y(ctx->jvm, java_get_minecraft_render_manager(ctx->jvm, ctx->minecraft));
    yaw = -yaw;
    float partial = h->partial;
    Vec3 cam = h->cam;
    float range = bed_esp_range_value;
    double range2 = (double)range * (double)range;

    // Projected 2D rendering for defence HUD
    void *view = &cam;

    for (unsigned long long i = 0; i < bed_esp_plate_count; i++) {
        BedPlate *plate = &bed_esp_plates[i];
        double dx = plate->bed_x + half_in_bed_esp - cam.x;
        double dy = plate->bed_y + half_in_bed_esp - cam.y;
        double dz = plate->bed_z + half_in_bed_esp - cam.z;
        if (dx * dx + dy * dy + dz * dz > range2)
            continue;

        if (bed_esp_exposed_outline) {
            // Render exposed layer outlines (air blocks around bed)
            Color3 col = plate->team_color;
            if (bed_esp_bed_team_color_mode == 0) {
                // Use team color
            } else {
                // Use exposed color setting
                int argb = *(int *)bed_esp_exposed_color_setting; // this would come from setting
                col.r = ((argb >> 16) & 0xFF) / 255.0f;
                col.g = ((argb >> 8) & 0xFF) / 255.0f;
                col.b = (argb & 0xFF) / 255.0f;
            }
            int outline_argb = ((int)(col.r * 255) << 16) | ((int)(col.g * 255) << 8) | (int)(col.b * 255) | (0xFF << 24);

            (*ptr_to_pGlPushMatrix)();
            (*ptr_to_pGlTranslatef)((float)dx, (float)dy, (float)dz);
            (*ptr_to_pGlRotatef)(yaw, 0.0f, 1.0f, 0.0f);
            // Render bed outline here
            (*ptr_to_pGlPopMatrix)();
        }

        if (bed_esp_defence_hud && plate->entry_count > 0) {
            // Render defence HUD - bed plate with layers
            double cx = plate->bed_x + half_in_bed_esp - cam.x;
            double cy = plate->bed_y + plate_height_offset - cam.y;
            double cz = plate->bed_z + half_in_bed_esp - cam.z;

            Pair proj;
            if (!project_point(view, {cx, cy, cz}, &proj))
                continue;

            float base_scale = bed_esp_scale_value * 0.02f;
            float scale = base_scale;
            if (bed_esp_auto_scale) {
                float dist = (float)sqrt(cx * cx + cy * cy + cz * cz);
                float ref_dist = fmaxf(1.0f, dist);
                scale = base_scale * (ref_dist / auto_scale_reference);
                scale = fmaxf(base_scale, scale);
            }
            scale = fminf(scale, 16.0f);

            float rounding = bed_esp_rounding_value * scale;
            int opacity = (int)bed_esp_opacity_value * 2.55f;
            int bg_color = (opacity << 24);

            int entry_count = (int)plate->entry_count;
            float total_height = entry_count * 18.0f * scale - 2.0f;
            float x0 = proj.x - total_height / 2.0f;
            float y0 = proj.y - 11.0f * scale;
            float x1 = proj.x + total_height / 2.0f + 3.0f * scale;
            float y1 = proj.y + 11.0f * scale;

            bed_esp_fill_rounded_rect(x0 - 3.0f * scale, y0, x1, y1, bg_color, rounding);

            if (bed_esp_exposed_outline && bed_esp_outline_exposed_layers) {
                int outline_argb = 0xFFFFFFFF; // white outline
                bed_esp_draw_stroked_rounded_rect(x0 - 3.0f * scale, y0, x1, y1, outline_argb, rounding, 1.25f * scale);
            }

            for (int j = 0; j < entry_count; j++) {
                BedDefenseEntry *entry = &plate->entries[j];
                float icon_x = x0 + j * 18.0f * scale;
                float icon_y = proj.y - 8.0f * scale;
                float icon_size = 16.0f * scale;

                if (entry->item_stack) {
                    bed_esp_draw_item_texture(nullptr, entry->item_stack, icon_x, icon_y, icon_size);
                } else if (entry->sprite) {
                    bed_esp_draw_sprite(nullptr, entry->sprite, icon_x, icon_y, icon_size);
                }

                if (bed_esp_show_block_count && entry->count > 1) {
                    char buf[16];
                    sprintf(buf, "%d", entry->count);
                    float font_h = 0;
                    float font_w = 0;
                    bed_esp_get_font_height("Modern", &font_h);
                    bed_esp_get_font_width("Modern", buf, &font_w);
                    bed_esp_draw_text(nullptr, "Modern", buf,
                        icon_x + 17.0f * scale - font_w * scale,
                        icon_y + 9.0f * scale,
                        font_h * scale, 0xFFFFFFFF, true, font_w * scale);
                }
            }
        }
    }

    (*ptr_to_pGlDepthMask)(1);
    (*ptr_to_pGlDisable)(0x809);
    (*ptr_to_pGlDisable)(0xbe2);
}

extern "C" void bed_esp_on_shutdown(void *hook) {
    clear_plates();
    libc_free(bed_esp_scan_id_entries);
    bed_esp_scan_id_entries = nullptr;
    bed_esp_scan_id_count = bed_esp_scan_id_cap = 0;
}