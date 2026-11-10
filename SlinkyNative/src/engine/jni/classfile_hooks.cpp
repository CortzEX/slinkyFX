// JNI bridge 0x22000
// cflags: /Ob1 /clang:-fno-slp-vectorize
#include "slinky/engine.h"
#include "slinky/jcall.h"
struct Rot { float yaw, pitch;
#ifdef __linux__
    ~Rot() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
extern "C" jobject get_minecraft(Jvm *jvm) {
    return JCALL(jvm, GetStaticObjectField, ids->Minecraft, ids->Minecraft_theMinecraft);
}

extern "C" jobject get_local_player(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_thePlayer);
}

extern "C" jobject get_world(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_theWorld);
}

extern "C" jobject get_timer(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_timer);
}

extern "C" jobject get_game_settings(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_gameSettings);
}

extern "C" jobject get_minecraft_render_global(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderGlobal);
}

extern "C" jobject java_get_minecraft(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_entityRenderer);
}

extern "C" jobject java_get_minecraft_render_item(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderItem);
}

extern "C" jobject java_get_minecraft_render_engine(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderEngine);
}

extern "C" jobject java_get_minecraft_player_controller(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_playerController);
}

extern "C" jobject java_get_minecraft_object_mouse_over(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_objectMouseOver);
}

extern "C" void java_set_minecraft(Jvm *jvm, void *minecraft, void *value) {
    JCALL(jvm, SetObjectField, minecraft, ids->Minecraft_objectMouseOver, value);
}

// The ClickGUI hands the cursor over to itself by clearing Minecraft.inGameHasFocus, which is the flag
// vanilla gates its mouse-look on (see logic/visual/gui.cpp). The flag means "the game loop owns the mouse"
// to vanilla but "a world is loaded" to the modules, so while the menu is up they keep reading the value the
// flag had before it opened and nothing else in the engine changes behaviour.
extern "C" {
unsigned char gui_owns_cursor;         // the ClickGUI has the cursor and the focus flag is ours to hold
unsigned char gui_focus_before_open;   // the value it had when the menu opened
}

extern "C" bool is_in_game(Jvm *jvm, void *minecraft) {
    if (gui_owns_cursor) return gui_focus_before_open != 0;
    if (!minecraft) return false;
    return JCALLN(jvm, GetBooleanField, minecraft, ids->Minecraft_inGameHasFocus) != 0;
}

extern "C" void java_set_minecraft_in_game_focus(Jvm *jvm, void *minecraft, int value) {
    if (!minecraft) return;
    JCALL(jvm, SetBooleanField, minecraft, ids->Minecraft_inGameHasFocus, (jboolean)(value != 0));
}

// The screen the game is showing right now (NULL = none, so the world itself has the mouse). Written from the
// frame hook, which is the game's own thread and the only thread that ever sets it.
extern "C" void *java_get_minecraft_current_screen(Jvm *jvm, void *minecraft) {
    if (!minecraft) return 0;
    return JCALLN(jvm, GetObjectField, minecraft, (jfieldID)ids->Minecraft_currentScreen);
}

extern "C" void java_set_minecraft_current_screen(Jvm *jvm, void *minecraft, void *screen) {
    if (!minecraft) return;
    JCALL(jvm, SetObjectField, minecraft, (jfieldID)ids->Minecraft_currentScreen, screen);
}

extern "C" jint java_get_minecraft_left_click_counter(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_leftClickCounter);
}

extern "C" void java_set_minecraft_left_click_counter(Jvm *jvm, void *minecraft, int value) {
    JCALL(jvm, SetIntField, minecraft, ids->Minecraft_leftClickCounter, value);
}

extern "C" jobject get_render_view_entity(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetObjectField, minecraft, ids->Minecraft_renderViewEntity);
}

extern "C" jobject java_call_minecraft_get_net_handler(Jvm *jvm, void *minecraft) {
    return JCALLN(jvm, CallObjectMethod, minecraft, ids->Minecraft_getNetHandler);
}

extern "C" void java_call_minecraft_click_mouse(Jvm *jvm, void *minecraft) {
    JCALLN(jvm, CallVoidMethod, minecraft, ids->Minecraft_clickMouse);
}

extern "C" void java_call_minecraft_right_click_mouse(Jvm *jvm, void *minecraft) {
    JCALLN(jvm, CallVoidMethod, minecraft, ids->Minecraft_rightClickMouse);
}

// ---- hand-written
FN(jobject, java_get_render_manager_instance, (Jvm *jvm));

extern "C" jobject get_current_screen(Jvm *jvm, jobject object) {
    jobject x = JCALLN(jvm, GetObjectField, object, (jfieldID)ids->Minecraft_currentScreen);
    return JCALLN(jvm, IsInstanceOf, x, (jclass)ids->GuiContainer) ? x : nullptr;
}
extern "C" jobject java_get_minecraft_render_manager(Jvm *jvm, jobject minecraft) {
    if (jvm->ids->variant != 0)
        return JCALL(jvm, GetObjectField, minecraft, (jfieldID)ids->Minecraft_renderManager);
    return java_get_render_manager_instance(jvm);
}

// ---- ClassFileLoadHook: rewrite the classes listed in the hook table, injecting "SlinkyHooks13"
struct MethodKey { char b[32]; };
struct MethodPat { long long a, b; };
struct HookTab { char padding_00[0x18]; MethodPat *pats; unsigned long long n; };
struct HookCls { jobject cls; void *data; int len; char padding_14[0x14]; };   // 0x28 bytes each

extern "C" volatile int classfile_hooks_first_error;          // first error (sticky)
extern "C" volatile long classfile_hooks_sites_rewritten = 0; // instructions replaced by the class currently being rewritten (diagnostics)
extern "C" unsigned char classfile_hooks_1_while_hooks_are_installed;         // 1 while the hooks are installed
extern "C" void slinky_logf(const char *fmt, ...);
static unsigned char g_disarming;             // 1 while remove_class_hooks restores the original bytecode
extern "C" HookCls hooked_class_minecraft, hooked_class_entity_renderer, hooked_class_render_manager, hooked_class_renderer_living_entity, hooked_class_render_player,
    hooked_class_net_handler_play_client, hooked_class_world_client, hooked_class_entity, hooked_class_entity_living_base;

// The names, in internal form ("net/minecraft/client/world/WorldClient"), of the nine classes above, taken once
// at install.
//
// The ClassFileLoadHook below identified a class only by IsSameObject against the `redefined` argument, and that
// argument is NULL for an ordinary class load - the JVM sets it only when it replays the hook while
// retransforming a class it already has. So every class the game loaded AFTER the install went in unpatched:
// WorldClient and RenderPlayer, which on Forge are loaded when the player joins a world, and those two carry a
// large part of what the modules sit on. The client looked alive in the menu and did nothing in the world.
// Matching on `name` covers both kinds of load, and `redefined` stays as the fallback for a load whose name the
// JVM does not give (null-package and some proxied loaders).
#define HOOK_NAME_SLOTS 9
#define HOOK_NAME_LEN 256
static char hook_class_names[HOOK_NAME_SLOTS][HOOK_NAME_LEN];

extern "C" void class_hooks_capture_names(JNIEnv *env) {
    jclass classClass = (*env)->FindClass(env, "java/lang/Class");
    if (!classClass) { (*env)->ExceptionClear(env); return; }
    jmethodID getName = (*env)->GetMethodID(env, classClass, "getName", "()Ljava/lang/String;");
    if (!getName) { (*env)->ExceptionClear(env); return; }
    HookCls *slots = &hooked_class_minecraft;   // the nine are one contiguous array, see the retransform loop
    for (int i = 0; i < HOOK_NAME_SLOTS; i++) {
        hook_class_names[i][0] = 0;
        if (!slots[i].cls) continue;
        jobject nameObj = (*env)->CallObjectMethod(env, slots[i].cls, getName);
        if (!nameObj) { (*env)->ExceptionClear(env); continue; }
        const char *n = (*env)->GetStringUTFChars(env, (jstring)nameObj, nullptr);
        if (n) {
            unsigned long k = 0;
            for (; n[k] && k < HOOK_NAME_LEN - 1; k++) hook_class_names[i][k] = (n[k] == '.') ? '/' : n[k];
            hook_class_names[i][k] = 0;
            (*env)->ReleaseStringUTFChars(env, (jstring)nameObj, n);
        } else {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, nameObj);
    }
}

extern "C" const char str_slinkyhooks13[];         // "SlinkyHooks13"
FN(bool, compare_member_name, (MethodKey *left, MethodPat *right));   // spelled as classfile_patch_helpers.cpp defines it: MS hands big structs by address, SysV would pass them in registers
struct HookTable; struct CP; struct RBuf22; struct WBuf22;
FN(int, rewrite_instruction, (HookTable *t, CP *constant_pool, RBuf22 *in, WBuf22 *out));   // instruction patcher (below)
FN(int, rewrite_class_file, (const unsigned char *data, int len, unsigned char **out, int *outLen, const char *hookCls,
                void *hookData, int hookLen, void *match, HookCls *h));
FN(void, libc_free, (void *ptr));                  // free
FN(void *, libc_memcpy_alt, (void *dest, const void *src, long long n));   // memcpy

struct JvmtiEnv_ { void *fn[64]; };
typedef int (*JvmtiAllocate)(JvmtiEnv_ **, long long, unsigned char **);

// method matcher passed to the class rewriter: (-1,-1) entries match everything
extern "C" int match_method(HookTab *t, MethodKey *k, void **outFn, MethodPat **outPat) {
    static int pat_logs;
    for (unsigned long long i = 0; i < t->n; i++) {
        MethodPat *p = &t->pats[i * 2];
        if (p->a == -1 && p->b == -1) {
            *outFn = (void *)rewrite_instruction;
            *outPat = p;
            break;
        }
        unsigned long long na = (unsigned long long)p->a, da = (unsigned long long)p->b;
        if (pat_logs < 12) { pat_logs++; slinky_logf("patcmp: i=%llu name=%p desc=%p", i, p->a, p->b); }
        if (na < 0x10000 || da < 0x10000) {
            slinky_logf("patcmp: bad pattern name=%llx desc=%llx (skipped)", na, da);
            continue;
        }
        if (compare_member_name(k, p)) {
            *outFn = (void *)rewrite_instruction;
            *outPat = p;
            break;
        }
    }
    return 0;
}

extern "C" void class_file_load_hook(JvmtiEnv_ **jvmti, JNIEnv *env, jclass redefined, jobject loader, const char *name,
                         jobject pd, int len, const unsigned char *data, int *newLen, unsigned char **newData) {
    if (classfile_hooks_first_error != 0) return;
    // remove_class_hooks sets g_disarming before it retransforms: during that call the JVM may replay this hook
    // for the classes being restored, and re-patching them would silently undo the disarm. Handing the
    // bytes back untouched makes the retransform a true restore. (The install also runs with the hook
    // active and g_disarming clear, so it still patches.)
    if (g_disarming) return;
    HookCls *pctx = nullptr;
    HookCls *slots = &hooked_class_minecraft;   // the nine, contiguous
    if (redefined) {
#define TRY(x) if ((*env)->IsSameObject(env, x.cls, redefined)) { pctx = &x; goto found; }
        TRY(hooked_class_minecraft) TRY(hooked_class_entity_renderer) TRY(hooked_class_render_manager) TRY(hooked_class_renderer_living_entity) TRY(hooked_class_render_player)
        TRY(hooked_class_net_handler_play_client) TRY(hooked_class_world_client) TRY(hooked_class_entity) TRY(hooked_class_entity_living_base)
#undef TRY
    } else if (name) {
        for (int i = 0; i < HOOK_NAME_SLOTS; i++) {
            if (!slots[i].cls || !hook_class_names[i][0]) continue;
            const char *a = name, *b = hook_class_names[i];
            while (*a && *a == *b) { a++; b++; }
            if (*a == 0 && *b == 0) { pctx = &slots[i]; goto found; }
        }
    }
    return;
found:
    slinky_logf("classhook-seen: %s", name ? name : "?");
    unsigned char *buf = nullptr;
    int outLen;
    __atomic_store_n(&classfile_hooks_sites_rewritten, 0, __ATOMIC_RELAXED);
    int err = rewrite_class_file(data, len, &buf, &outLen, str_slinkyhooks13, pctx->data, pctx->len, (void *)match_method, pctx);
    long sites = __atomic_load_n(&classfile_hooks_sites_rewritten, __ATOMIC_RELAXED);
    if (err == 0)
        slinky_logf("classhook: %s sites=%ld %d->%d", name ? name : "?", sites, len, outLen);
    else
        slinky_logf("classhook: %s sites=%ld err=%d", name ? name : "?", sites, err);
    if (err == 0) {
        unsigned char *mem = nullptr;
        JvmtiAllocate alloc = (JvmtiAllocate)(*jvmti)->fn[45];
        int alloc_error = alloc(jvmti, outLen, &mem);
        if (alloc_error != 0 || !mem) {
            err = (alloc_error ? alloc_error : 100) + 12000;   // JVMTI_ERROR_NULL_POINTER
            libc_free(buf);
            __sync_val_compare_and_swap(&classfile_hooks_first_error, 0, err);
            return;
        }
        libc_memcpy_alt(mem, buf, outLen);
        *newData = mem;
        *newLen = outLen;
        libc_free(buf);
        return;
    }
    __sync_val_compare_and_swap(&classfile_hooks_first_error, 0, err);
}

// ---- JNI bridge functions found by the gap scan (no caller outside the virtualised init) ----
extern "C" jint java_get_minecraft_right_click_delay_timer(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_rightClickDelayTimer);
}

extern "C" jobject java_call_minecraft_add_scheduled_task(Jvm *jvm, void *minecraft, void *value) {
    return JCALLN(jvm, CallObjectMethod, minecraft, ids->Minecraft_addScheduledTask, value);
}

// ---- JVMTI: switch the class hooks off again (undo of the install) ----
// Disables the ClassFileLoadHook event, hands the hooked classes back to the game one at a time so the JVM's
// stop-the-world is spread over frames, then drops the transformer's callbacks, natives and capabilities and
// finally its global ref. Errors are JVMTI error + 12000.
struct JvmtiTable { void *fn[160]; };
typedef int (*JvmtiSetEventMode)(JvmtiTable **, int mode, int event, void *thread, ...);
typedef int (*JvmtiRetransform)(JvmtiTable **, int count, jclass *classes);
typedef int (*JvmtiSetCallbacks)(JvmtiTable **, const void *callbacks, int size);
typedef int (*JvmtiRelinquish)(JvmtiTable **, const void *caps);
#define JVMTI(env, idx, T) ((T)(*(env))->fn[idx])
extern "C" unsigned char classfile_hooks_1_while_hooks_are_installed;       // 1 while the hooks are installed
extern "C" jobject classfile_hooks_global_ref_transformer_class;              // global ref of the transformer class
extern "C" const char ptr_to_hook_installer_class_file_slinkyhooks13[];        // JVMTI capabilities that were added

// The class behind position i of the restore pass. Minecraft is position 8, not 0: its rewritten Display.update
// call site is what keeps calling hook_update_display, so it has to be the last one handed back or the unload
// stops half way with no frame left to continue from.
static jclass disarm_class(int i) {
    switch (i) {
        case 0: return (jclass)hooked_class_entity_renderer.cls;
        case 1: return (jclass)hooked_class_render_manager.cls;
        case 2: return (jclass)hooked_class_renderer_living_entity.cls;
        case 3: return (jclass)hooked_class_render_player.cls;
        case 4: return (jclass)hooked_class_net_handler_play_client.cls;
        case 5: return (jclass)hooked_class_world_client.cls;
        case 6: return (jclass)hooked_class_entity.cls;
        case 7: return (jclass)hooked_class_entity_living_base.cls;
        default: return (jclass)hooked_class_minecraft.cls;
    }
}

// One restore step. step = -1 starts the pass, 0..8 hands classes back, 9 releases the JVMTI side; *next is
// the step to call back with, or -1 once the game is vanilla again.
// The caller (engine/protected/init.cpp slinky_disarm_step) drives this once per frame; see the note there
// about why one call for all nine classes is a stall the player sees.
extern "C" double time_now();          // engine/protected/files.cpp, the clock the frame code uses too
static int g_disarm_batch = 1;         // classes per RetransformClasses call, tuned by the stops themselves
// The fixed part of a stop (bringing every thread of the game to a safepoint) is paid once per call, so one
// class per call is the gentlest split but not the cheapest. The pass starts at one class and takes one more
// per call as long as the measured stop stays under half the budget, and gives one back as soon as a stop
// goes over it: whatever the machine turns out to be, it converges within a couple of frames and a stop only
// exceeds the budget when a single class on its own already does.
static const int kDisarmStopBudgetMs = 8;
static int disarm_classes_back(int step, JvmtiTable **jvmti, int *errp) {
    jclass batch[9];
    int n = 0;
    int i = step;
    for (; i < 9 && n < g_disarm_batch; i++) {
        jclass one = disarm_class(i);
        // A class the game never loaded holds nothing of ours; the install skipped it the same way, and a
        // skipped position costs nothing so it never stops the batch from filling up.
        if (one) batch[n++] = one;
    }
    if (!n) return i;
    double start = time_now();
    int err = JVMTI(jvmti, 151, JvmtiRetransform)(jvmti, n, batch);
    int took = (int)((time_now() - start) * 1000.0 + 0.5);
    slinky_logf("disarm: %d class%s back in %d ms (err %d, next batch %d)", n, n == 1 ? "" : "es", took, err, g_disarm_batch);
    if (took * 2 < kDisarmStopBudgetMs && g_disarm_batch < 4) g_disarm_batch++;
    else if (took > kDisarmStopBudgetMs && g_disarm_batch > 1) g_disarm_batch--;
    if (err) {
        *errp = err + 0x2ee0;
        return -1;
    }
    return i;
}

extern "C" int remove_class_hooks_step(JNIEnv *env, JvmtiTable **jvmti, int step, int *next) {
    int err = 0;
    *next = -1;
    if (step < 0) {
        // Not armed = the hooks were never installed or another pass already consumed the flag (a failed
        // pass left the game's patched calls live). Reporting success here made the unload sequence tear
        // everything down while the JVM was still dispatching natives into this DLL: hs_err pid6708
        // (java_set_render_manager_player_view_y via setRenderY) and pid2260 (serialize_settings via a second
        // hook_update_display pass) look exactly like that.
        if (!__sync_bool_compare_and_swap(&classfile_hooks_1_while_hooks_are_installed, 1, 0)) {
            // A failed install may still have defined SlinkyHooks13 and kept its global ref.
            if (env && classfile_hooks_global_ref_transformer_class) {
                (*env)->DeleteGlobalRef(env, classfile_hooks_global_ref_transformer_class);
                classfile_hooks_global_ref_transformer_class = 0;
            }
            return 12000 + 49 /* JVMTI_ERROR_NOT_AVAILABLE */;
        }
        g_disarming = 1;
        g_disarm_batch = 1;   // every pass starts gentle and relearns the machine it is on
        err = JVMTI(jvmti, 1, JvmtiSetEventMode)(jvmti, 0 /* JVMTI_DISABLE */, 0x36 /* CLASS_FILE_LOAD_HOOK */, 0);
        if (err) { err += 0x2ee0; goto failed; }
        *next = 0;
        return 0;
    }
    if (step < 9) {
        int after = disarm_classes_back(step, jvmti, &err);
        if (after < 0) goto failed;
        if (after < 9) {
            *next = after;
            return 0;   // the game paints this frame, the next one hands the rest back
        }
        // Minecraft - the class that holds the frame hook itself - is back, so the game no longer calls us and
        // nothing may be left for a next frame: release the JVMTI side in this one.
    }
    // Nothing of ours may stay reachable from the JVM once the DLL is gone: the transformer's callbacks are
    // a copy the VM holds, and its native entry points are function pointers into this module.
    {
        char no_callbacks[0x138] = {};
        err = JVMTI(jvmti, 121, JvmtiSetCallbacks)(jvmti, no_callbacks, sizeof no_callbacks);
        if (err) { err += 0x2ee0; goto failed; }
    }
    if (env && classfile_hooks_global_ref_transformer_class &&
        (*env)->UnregisterNatives(env, classfile_hooks_global_ref_transformer_class) != 0)
        (*env)->ExceptionClear(env);
    {
        int rel = JVMTI(jvmti, 142, JvmtiRelinquish)(jvmti, ptr_to_hook_installer_class_file_slinkyhooks13);
        if (rel) err = rel + 0x2ee0;
    }
    if (classfile_hooks_global_ref_transformer_class) {
        (*env)->DeleteGlobalRef(env, classfile_hooks_global_ref_transformer_class);
        classfile_hooks_global_ref_transformer_class = 0;
    }
    g_disarming = 0;
    return err;
failed:
    // The game still calls into this DLL, so everything stays allocated and resident: re-arm the flag and let
    // the next click start a whole pass. Retransform is idempotent, so classes already restored survive it.
    g_disarming = 0;
    __sync_bool_compare_and_swap(&classfile_hooks_1_while_hooks_are_installed, 0, 1);
    slinky_logf("disarm: err=%d at step %d", err, step);
    return err;
}

// The whole pass in one call: the start-up failure path has no frames left to spread it over.
extern "C" int remove_class_hooks(JNIEnv *env, JvmtiTable **jvmti) {
    int step = -1;
    for (;;) {
        int next;
        int err = remove_class_hooks_step(env, jvmti, step, &next);
        if (err || next < 0) return err;
        step = next;
    }
}

// ---- class-file patcher: rewrites a call/field instruction that matches a hook entry into `invokestatic hook` ----
// Instruction callback of the class transformer (rewrite_span/copy_methods_table_constant_pool `pick`). `t` lists the hooks; each has the
// owner class, member name + descriptor to catch, the kind of instruction and where its new Methodref index is.
#include "slinky/classfile.h"
struct RBuf22 { const unsigned char *p, *end; };
struct WBuf22 { unsigned char *p, *end; };
struct HookEnt { const char *cls; const char *name, *desc; int kind; int padding_1c; unsigned short *ref; };   // 0x28
struct HookTable { char padding_00[0x10]; HookEnt *e; unsigned long long n; };
struct MemberName { const char *name, *desc; };
FN(int, classfile_read_u1, (RBuf22 *r, unsigned char *out));
FN(int, classfile_read_u2, (RBuf22 *r, unsigned short *out));
FN(int, classfile_write_u1, (WBuf22 *w, unsigned char b));
FN(int, classfile_write_u2, (WBuf22 *w, unsigned short v));
FN(int, constant_pool_member_ref, (CP *constant_pool, unsigned short idx, MemberRef *out));
FN(int, constant_pool_member_ref2, (CP *constant_pool, unsigned short idx, MemberRef *out));
extern "C" int compare_utf8_local(const Utf8 *left, const char *right) __asm__("compare_utf8");
extern "C" bool compare_member_name_local(const NameType *left, MemberName *right) __asm__("compare_member_name");
extern "C" int rewrite_instruction(HookTable *t, CP *cp, RBuf22 *in, WBuf22 *out) {
    unsigned char op;
    int error_code = classfile_read_u1(in, &op);
    if (error_code) return error_code;
    int kind;
    bool field = true;
    switch (op) {
    case 0xb2: kind = 4; break;               // getstatic
    case 0xb3: kind = 6; break;               // putstatic
    case 0xb4: kind = 3; break;               // getfield
    case 0xb5: kind = 5; break;               // putfield
    case 0xb6: case 0xb7: kind = 1; field = false; break;   // invokevirtual / invokespecial
    case 0xb8: kind = 2; field = false; break;               // invokestatic
    default: return 0;
    }
    unsigned short idx;
    error_code = classfile_read_u2(in, &idx);
    if (error_code) return error_code;
    MemberRef ref;
    error_code = field ? constant_pool_member_ref(cp, idx, &ref) : constant_pool_member_ref2(cp, idx, &ref);
    if (error_code) return error_code;
    for (unsigned long long i = 0; i < t->n; i++) {
        HookEnt *h = &t->e[i];
        if (h->kind != kind) continue;
        if (compare_utf8_local(&ref.cls, h->cls) != 0) continue;
        unsigned long long np = (unsigned long long)h->name, dp = (unsigned long long)h->desc;
        static int site_logs;
        if (site_logs < 30) { site_logs++; slinky_logf("sitecmp: cls=%s kind=%d name=%p desc=%p", h->cls, h->kind, h->name, h->desc); }
        if (np < 0x10000 || dp < 0x10000) {
            slinky_logf("sitecmp: bad site name=%llx desc=%llx (skipped)", np, dp);
            continue;
        }
        MemberName mn = {h->name, h->desc};
        if (!compare_member_name_local(&ref.nt, &mn)) continue;
        error_code = classfile_write_u1(out, 0xb8);   // invokestatic
        if (error_code) return error_code;
        error_code = classfile_write_u2(out, *h->ref);
        if (error_code == 0) __atomic_add_fetch(&classfile_hooks_sites_rewritten, 1, __ATOMIC_RELAXED);
        return error_code;
    }
    return 0;
}

// ---- functions the first gap scan missed (padding / tail-jump neighbours) ----
extern "C" void java_set_minecraft_right_click_delay_timer(Jvm *jvm, void *minecraft, int value) {
    JCALL(jvm, SetIntField, minecraft, ids->Minecraft_rightClickDelayTimer, value);
}

extern "C" jint java_get_minecraft_display_width(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_displayWidth);
}

extern "C" jint java_get_minecraft_display_height(Jvm *jvm, void *minecraft) {
    return JCALL(jvm, GetIntField, minecraft, ids->Minecraft_displayHeight);
}