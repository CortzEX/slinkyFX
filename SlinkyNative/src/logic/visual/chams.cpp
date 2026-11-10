// Chams - entity render hook wrapper.  Draws the entity with GL polygon offset
// (so it shows through geometry) unless it is filtered out (friendlies when
// "hide_friendlies" is set).  Forwards all render arguments to the original 0x274f0.
#include "slinky/engine.h"
#define H0(c) __builtin_expect(!!(c), 0)
#define H1(c) (c)

extern "C" unsigned char chams_module_enabled;            // module enabled
extern "C" unsigned char chams_hide_friendlies_value;            // "hide_friendlies"
extern "C" void (**ptr_to_pGlEnable)(unsigned);       // glEnable
extern "C" void (**ptr_to_pGlDisable)(unsigned);       // glDisable
extern "C" void (**ptr_to_pGlPolygonOffset)(float, float);   // glPolygonOffset
extern "C" volatile float one_in_chams;             // 1.0f
extern "C" volatile float chams_polygon_offset_factor;             // -3000000.0f

FN(unsigned char, entity_flags_has, (void *jvm, void *entity, int mode));
FN(void, java_jni_call_nonvirtual_renderer_living_entity_do_render, (void *jvm, void *object, void *other_object, double x, double d0, double d1, float f0, float f1));
extern "C" void slinky_logf(const char *fmt, ...);

extern "C" void chams_render_living_entity(void *jvm, void *entity, void *other_object, double x, double d0, double d1, float f0, float f1) {
    static int logged;
    if (!logged) {
        logged = 1;
        slinky_logf("chams: first render wrapper enabled=%d hide_friendlies=%d", (int)chams_module_enabled, (int)chams_hide_friendlies_value);
    }
    if (__builtin_expect(chams_module_enabled != 1, 0) || entity_flags_has(jvm, other_object, chams_hide_friendlies_value * 5 + 2)) {
        java_jni_call_nonvirtual_renderer_living_entity_do_render(jvm, entity, other_object, x, d0, d1, f0, f1);
           // keeps clang from turning this call into a tail call (the original is not)
        return;
    }
    (*ptr_to_pGlEnable)(0x8037);
    auto po = *ptr_to_pGlPolygonOffset;
    float f = one_in_chams;
    float u = chams_polygon_offset_factor;
    po(f, u);
    java_jni_call_nonvirtual_renderer_living_entity_do_render(jvm, entity, other_object, x, d0, d1, f0, f1);
    (*ptr_to_pGlDisable)(0x8037);
}
