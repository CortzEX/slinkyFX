// Block ESP - per-chunk line meshes of the "allowed_blocks" found around the
// player, rebuilt on demand and drawn with GL client-side arrays.
//   +0x48 tick, +0x98 enable/disable, +0xa8 render, +0x188 block update, +0x1e8 shutdown
#include "slinky/engine.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Buf { void *p; unsigned long long n, cap; };
struct Mesh {                    // 0x50 bytes
    int cx, cz;                  // chunk coordinates
    unsigned long long a, b;     // +0x08
    Buf lines;                   // +0x18
    Buf verts;                   // +0x30 (16-byte vertices: xyz float + rgba)
    unsigned short *idx;         // +0x48
};
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };

extern "C" unsigned char block_esp_allowed_blocks_changed;     // allowed_blocks changed
extern "C" float block_esp_line_width_value;             // "line_width"
extern "C" float block_esp_block_render_limit_value;             // "block_render_limit"
extern "C" float block_esp_range_value;             // "range"
extern "C" unsigned char block_esp_should_limit_block_render;     // "should_limit_block_render"
extern "C" char block_esp_module_descriptor[];            // Block ESP module descriptor
struct MeshVec { unsigned long long size, cap; Mesh *data; };
extern "C" MeshVec block_esp_mesh_count_capacity;   // mesh count / capacity
extern "C" Mesh *block_esp_meshes;             // meshes
extern "C" unsigned char block_esp_rebuild_requested;     // rebuild requested
extern "C" unsigned long long id_table_count, id_table_capacity;
extern "C" void *id_table_entries;
extern "C" void *block_esp_block_scan_callback;             // block scan callback
extern "C" void (**ptr_to_pGlEnableClientState)(unsigned);                       // glEnableClientState
extern "C" void (**ptr_to_pGlDisableClientState)(unsigned);                       // glDisableClientState
extern "C" void (**ptr_to_pGlLineWidth)(float);                          // glLineWidth
extern "C" void (**ptr_to_pGlPushMatrix)();                               // glPushMatrix
extern "C" void (**ptr_to_pGlPopMatrix)();                               // glPopMatrix
extern "C" void (**ptr_to_pGlTranslatef)(float, float, float);            // glTranslatef
extern "C" void (**ptr_to_pGlVertexPointer)(int, unsigned, int, const void *);   // glVertexPointer
extern "C" void (**ptr_to_pGlColorPointer)(int, unsigned, int, const void *);   // glColorPointer
extern "C" void (**ptr_to_pGlDrawElements)(unsigned, unsigned long long, unsigned, const void *);  // glDrawElements
extern "C" const double block_esp_box_expand;     // 1/16
extern "C" const float factor_24_in_block_esp;      // limit scale
extern "C" const double half_in_block_esp;     // 0.5

FN(void, rebuild_block_esp_mesh, (Mesh *m));
FN(int, java_call_object_hash_code, (void *jvm, void *world));
FN(void, block_esp_queue_loaded_chunks, (Ctx *ctx));
FN(void, libc_free, (void *ptr));
FN(double, libc_hypot, (double x, double z));
FN(Vec3, vec3_sub, (Vec3 a, Vec3 b));
FN(void, equipment_event, (void *x));
FN(void, scan_16x16x16_chunk_section_searched, ());
FN(void, scan_16_bit_block_data, ());

static inline __attribute__((always_inline)) void clear_meshes() {
    for (unsigned long long i = 0; i < block_esp_mesh_count_capacity.size; i++) {
        Mesh *m = &block_esp_meshes[i];
        m->a = m->b = 0;
        libc_free(m->lines.p);
        m->lines.p = nullptr;
        m->lines.n = 0;
        m->lines.cap = 0;
        libc_free(m->verts.p);
        m->verts.p = nullptr;
        m->verts.n = 0;
        m->verts.cap = 0;
        libc_free(m->idx);
        m->idx = nullptr;
    }
    block_esp_mesh_count_capacity.size = block_esp_mesh_count_capacity.cap = 0;
    libc_free(block_esp_meshes);
    block_esp_meshes = nullptr;
}

extern "C" void block_esp_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->in_game == 1 && block_esp_allowed_blocks_changed) {
        for (unsigned long long i = 0; i < block_esp_mesh_count_capacity.size; i++)
            rebuild_block_esp_mesh(&block_esp_meshes[i]);
        block_esp_allowed_blocks_changed = 0;
    }
    if (!ctx->world) {
        clear_meshes();
        return;
    }
    java_call_object_hash_code(ctx->jvm, ctx->world);
    if (block_esp_rebuild_requested == 1) {
        clear_meshes();
        block_esp_queue_loaded_chunks(ctx);
        block_esp_rebuild_requested = 0;
    }
}

extern "C" void block_esp_on_toggle(EvHook *hook) {
    if (__builtin_expect(hook->ev == block_esp_module_descriptor, 1)) {
        if (hook->on) {
            Ctx *ctx = hook->ctx;
            block_esp_block_scan_callback = **(int **)ctx->jvm == 0 ? (void *)scan_16x16x16_chunk_section_searched : (void *)scan_16_bit_block_data;
            if (ctx->world) {
                block_esp_queue_loaded_chunks(ctx);
                return;
            }
        } else {
            clear_meshes();
        }
    }

}

extern "C" void block_esp_on_render_world(char *h) {
    if (!block_esp_mesh_count_capacity.size)
        return;
    void (**enable)(unsigned) = ptr_to_pGlEnableClientState;
    (*enable)(0x8074);
    (*enable)(0x8076);
    (*ptr_to_pGlLineWidth)(block_esp_line_width_value);
    unsigned long long lim = (unsigned long long)(block_esp_block_render_limit_value * factor_24_in_block_esp);
    unsigned long long limit = block_esp_should_limit_block_render ? lim : 0x1000;
    unsigned long long n = block_esp_mesh_count_capacity.size;
    if (n) {
        Vec3 *cam = (Vec3 *)(h + 8);
        double k = block_esp_box_expand;
        double cx = cam->x * k;
        double cz = k * cam->z;
        Mesh *arr = block_esp_meshes;
        float range = block_esp_range_value;
        void (**pop)() = ptr_to_pGlPopMatrix;
        double half = half_in_block_esp;
        for (unsigned long long i = 0; i < n; i++) {
            Mesh *m = &arr[i];
            double dx = cx - ((double)m->cx + half);
            double dz = cz - ((double)m->cz + half);
            if (libc_hypot(dx, dz) > (double)range)
                continue;
            (*ptr_to_pGlPushMatrix)();
            Vec3 o = {(double)(m->cx << 4), 0.0, (double)(m->cz << 4)};
            Vec3 r = vec3_sub(o, *cam);
            (*ptr_to_pGlTranslatef)((float)r.x, (float)r.y, (float)r.z);
            (*ptr_to_pGlVertexPointer)(3, 0x1406, 0x10, m->verts.p);
            (*ptr_to_pGlColorPointer)(3, 0x1401, 0x10, (char *)m->verts.p + 0xc);
            unsigned long long cnt = m->verts.n;
            (*ptr_to_pGlDrawElements)(1, limit < cnt ? limit : cnt, 0x1403, m->idx);
            (*pop)();
            arr = block_esp_meshes;
            range = block_esp_range_value;
            n = block_esp_mesh_count_capacity.size;
        }
    }
    void (**disable)(unsigned) = ptr_to_pGlDisableClientState;
    (*disable)(0x8074);
    (*disable)(0x8076);
}

extern "C" void block_esp_on_world_change(char *h) {
    equipment_event(h + 8);
}

extern "C" void block_esp_on_shutdown(Hook *hook) {
    id_table_count = id_table_capacity = 0;
    libc_free(id_table_entries);
    id_table_entries = nullptr;
    clear_meshes();
}
