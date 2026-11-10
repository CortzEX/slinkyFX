// Client start-up, hook control and shutdown.
//
// The original runs this from protector-virtualised code (see docs/init.md); the bodies could not be translated,
// so this file is NEW code written from what the rest of the client expects to find when it runs: every global the
// readable code reads but nothing readable writes is created here, in the order its dependencies imply, using the
// readable helpers (fn_*) the original left behind for exactly these steps. The JNI/JVMTI call sequences are those
// of the readable counterparts (install_class_hooks installs, remove_class_hooks removes). Anything inferred is marked "inferred".
// Licence checking is not part of this client.
#include "slinky/engine.h"
#include "slinky/jids.h"
#include "slinky/platform.h"
#include <stdarg.h>

// ---- state created here (the data_* globals of the image; all read elsewhere) ----
struct VmTable {   // JNIInvokeInterface
    void *reserved[3];
    int (*DestroyJavaVM)(void *vm);
    int (*AttachCurrentThread)(void *vm, void **env, void *args);
    int (*DetachCurrentThread)(void *vm);
    int (*GetEnv)(void *vm, void **env, int version);
};
struct KeyMap {   // key -> items map (see gui/e_e.cpp: key_map_add / key_map_remove / key_map_get / key_map_dispatch / key_map_destroy)
    void (*handler)(void *);   // fn(item, key, down), called by the input handler
    void (*release)(void *);   // called on an item when it is replaced or removed
    unsigned long long count, cap;
    void *data;
    unsigned char multi;       // 1: several items may share a key
    char padding_29[7];
};
struct LoaderRef { jobject loader; jmethodID loadClass; };
struct HClass { jclass cls; jmethodID ctor; };
extern "C" void *init_our_module_handle;               // our module handle
extern "C" unsigned char java_natives_frame_client_shutting_down_natives_check;       // client shutting down: the natives check this and go inert
extern "C" VmTable **init_javavm;           // JavaVM*
extern "C" JIds *java_natives_world_cached_jni_ids;               // cached JNI IDs
extern "C" char java_natives_world_module_manager[];              // module manager (0x310 bytes, in place)
extern "C" char *ptr_to_gui_module_descriptor;                    // GUI module block; its +0x14 is the "window open" flag
extern "C" unsigned char *java_natives_world_key_state_table_one_byte;      // key state table, one byte per client key code
extern "C" KeyMap *java_natives_frame_profile_key_binds_key_profile;             // profile key binds: key -> profile name
extern "C" KeyMap *java_natives_frame_module_key_binds_key_module;             // module key binds: key -> module
extern "C" void *init_profile_list;               // profile list
extern "C" HClass init_slinkychannelhandler_class_constructor;              // SlinkyChannelHandler class + constructor
extern "C" unsigned char classfile_hooks_1_while_hooks_are_installed;        // the Java hooks are armed (jni/hook_installer.cpp)
extern "C" bool (*input_input_handler_hooked_into_game)(void *event);   // input handler hooked into the game window (protected/input.cpp)
extern "C" const char str_slinky_in_profiles[], str_settings[], str_name[], str_profile_binds[], str_bind[];   // ".slinky", "settings", "name", "profile_binds", "bind"

// ---- readable engine functions used below ----
FN(void *, libc_calloc, (unsigned long long n, unsigned long long size));   // calloc
FN(void, libc_free, (void *ptr));                                             // free
FN(int, libc_vsnprintf, (char *buf, unsigned long long n, const char *fmt, va_list ap));   // vsnprintf
FN(int, get_java_vm, (void **vm));                                              // JNI_GetCreatedJavaVMs
FN(int, init_jni_ids, (JIds *ids, JNIEnv *env, LoaderRef *out));                // JNI bootstrap: loader, mapping, all IDs
FN(void, release_jni_ids, (JIds *ids, JNIEnv *env));                               // release the cached class refs
FN(int, create_module_manager, (void *mgr, void (*setter)(void *mod, int key)));         // module manager from the static module list
FN(void, save_module_states, (void *mgr));                                               // snapshot each module's current settings as its defaults
FN(void, destroy_module_manager, (void *mgr));                                            // ... and its destructor
FN(unsigned char, queue_module_toggle, (void *module_manager, void *module, unsigned char enabled));      // queue a module enable/disable
FN(void, set_module_bind, (void *mod, int key));                                    // set a module's key bind
FN(unsigned char, on_profile_bind, (const char *name, int key, bool down));        // profile key pressed
FN(unsigned char, on_module_bind, (char *mod, int key, bool down));               // module key pressed
FN(void, bind_profile_to_key, (char *profile, int key));                                // bind a profile to a key
FN(int, load_profiles, (void *profiles));                                         // (re)read the profile folder
FN(void, key_map_destroy, (void *map));                                             // destroy a key map
FN(void, destroy_profiles, (void *profiles));                                        // free the profile list
FN(void *, find_profile, (void *profiles, const char *name));                    // profile entry by name
FN(int, init_chat_commands, ());                                                       // built-in chat commands
FN(void, destroy_chat_commands, ());                                                      // ... and their free
FN(int, init_glfw_entrypoints, ());                                                       // GLFW entry points (0x3e8: no glfw)
FN(int, install_packet_channel, (Jvm *jvm, jobject loader, HClass *out, void *onPacket));    // define SlinkyChannelHandler
FN(void, release_packet_channel, (JNIEnv *env, jclass *cls));                              // ... and drop it
FN(int, on_packet_local, (JNIEnv *env, jobject, void *pkt, unsigned char out, long long time, int id)) __asm__("on_packet");
FN(bool, on_input_event, (void *event));                                           // the client's input handler
FN(int, install_class_hooks, (Jvm *jvm, void **jvmti));                                  // install the class hooks
FN(int, remove_class_hooks, (JNIEnv *env, void **jvmti));                             // remove them again
FN(void, clear_saved_string, ());                                                      // free the current profile name
FN(void, free_block_search_cache, ());                                                     // free the block-search mesh cache
FN(int, settings_path, (char **path));                                           // %USERPROFILE%
FN(int, settings_path_append, (char **path, const char *suffix));                       // append "\suffix"
FN(int, read_text_file, (char **path, char **data));                              // read a whole file as text
FN(void, free_path, (char **path));
FN(unsigned char, load_module_settings, (void *root, void *mods, void *mod));            // apply the saved settings of one module
FN(void *, cjson_parse, (const char *text));                                   // cJSON_Parse
FN(void *, cjson_get_object_item, (const void *o, const char *name));                    // cJSON_GetObjectItemCaseSensitive
FN(char *, cjson_get_string_value, (const void *o));                                      // cJSON_GetStringValue
FN(void, cjson_delete, (void *o));                                              // cJSON_Delete

// ---- random numbers ----
// xoshiro128+ state used by math/e_a.cpp. The image leaves it zeroed and its virtualised start-up (not
// recovered) seeded it. With an all-zero state rng_next() always returns 0, so walk_randomize's
// `while (epsilon > |a - b|)` and gauss_random's `while (s >= 1)` rejection loops never terminate: the game
// freezes the first time a module uses a random walk (Aim Assist, Clutch, ...). Seed it from the TSC.
extern "C" unsigned init_table[4];
static void seed_rng() {
    unsigned long long x = (unsigned long long)__builtin_ia32_rdtsc();
    x ^= reinterpret_cast<unsigned long long>(&init_table) << 17;
    unsigned long long acc = 0;
    for (int i = 0; i < 4; i++) {
        x += 0x9e3779b97f4a7c15ULL;                    // splitmix64
        unsigned long long z = x;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        init_table[i] = (unsigned)z;
        acc |= z;
    }
    if (!acc) init_table[0] = 0x9e3779b9;             // xoshiro must not be all zero
}

// ---- logging ----
extern "C" void slinky_logf(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    libc_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    slinky_log_write(line);
}

// ---- JVM / JVMTI access ----
enum { JNI_VERSION_8 = 0x00010008, JVMTI_VERSION_1_2 = 0x30010200 };
static JNIEnv *attach(bool *attached) {
    VmTable **vm = init_javavm;
    void *env = 0;
    *attached = false;
    if (!vm || !*vm) return 0;
    if ((*vm)->GetEnv(vm, &env, JNI_VERSION_8) == 0) return (JNIEnv *)env;   // already attached (the game's thread)
    if ((*vm)->AttachCurrentThread(vm, &env, 0) != 0) return 0;
    *attached = true;
    return (JNIEnv *)env;
}
static void detach(bool attached) {
    if (attached && init_javavm && *init_javavm) (*init_javavm)->DetachCurrentThread(init_javavm);
}
// turn the class hooks on (1) or off (0); 0 or the error code of the step that failed
// GetEnv(..., JVMTI_VERSION_1_2) makes the VM create a NEW JVMTI environment on every call
// (JvmtiExport::get_jvmti_interface -> JvmtiEnv::create_a_jvmti). A fresh environment has neither the
// capabilities the install added nor the event callbacks nor the enabled ClassFileLoadHook event, so a
// disarm on a re-fetched environment does nothing and RetransformClasses fails with
// JVMTI_ERROR_MUST_POSSESS_CAPABILITY (99). The original keeps one environment; reuse the cached one.
static void **g_jvmti;
static int hooks_set(JNIEnv *env, bool enable) {
    void **jvmti;
    int err;
    if (!init_javavm || !*init_javavm) return 0x7d3;
    if (g_jvmti) {
        jvmti = g_jvmti;
    } else {
        err = (*init_javavm)->GetEnv(init_javavm, (void **)&jvmti, JVMTI_VERSION_1_2);
        if (err) return 12000 + err;
    }
    Jvm jvm = {java_natives_world_cached_jni_ids, env};
    err = enable ? install_class_hooks(&jvm, jvmti) : remove_class_hooks(env, jvmti);
    // Cache the environment whenever an install fetched it, not only when the install reported success. The
    // capabilities, the callbacks and the enabled load hook all belong to this one environment, and a
    // re-fetched one answers nothing: a disarm that ran on a fresh environment failed with
    // MUST_POSSESS_CAPABILITY while the hooks were still in the game, which is the state the unload cannot escape
    // from. Whatever the install returned, this is the only environment that can undo it.
    if (enable) g_jvmti = jvmti;
    return err;
}
// set_java_hooks(env, enable): enable = 0 comes from slinky_disarm, the unload step of the engine thread.
// Returns the error so the unload can be aborted while every global is still valid: if the Java
// hooks cannot be removed, tearing the client down and unloading the DLL while the game keeps
// calling into it is a guaranteed use-after-free (see the serialize_settings crash on unload).
extern "C" int set_java_hooks(JNIEnv *env, int enable) {
    int err = hooks_set(env, enable != 0);
    if (err) slinky_logf(enable ? "Initialization failed: %u" : "Disabling hooks failed: %u", err);
    return err;
}

// ---- the saved global settings: ~/.slinky/settings (written by save_settings) ----
// inferred: the counterpart of serialize_settings (save). Module settings of the global modules (flags & 0x10) go through the
// per-module loader load_module_settings; "profile_binds" is a list of {name, bind} that rebinds the profiles.
struct Json { Json *next, *prev, *child; int type; char *valuestring; int valueint; double valuedouble; char *string; };
struct Mods { struct Mod { char pad0[0x14]; unsigned char enabled; char pad1[3]; unsigned flags; } **data; unsigned long long count; };
static void load_settings() {
    char *path;
    if (settings_path(&path)) return;
    char *text = 0;
    if (!settings_path_append(&path, str_slinky_in_profiles) && !settings_path_append(&path, str_settings) && !read_text_file(&path, &text) && text) {
        Json *root = (Json *)cjson_parse(text);
        if (root) {
            Mods *mods = (Mods *)java_natives_world_module_manager;
            for (unsigned long long i = 0; i < mods->count; i++)
                if (mods->data[i]->flags & 0x10) load_module_settings(root, mods, mods->data[i]);
            Json *binds = (Json *)cjson_get_object_item(root, str_profile_binds);
            for (Json *b = binds ? binds->child : 0; b; b = b->next) {
                const char *name = cjson_get_string_value(cjson_get_object_item(b, str_name));
                Json *key = (Json *)cjson_get_object_item(b, str_bind);
                char *profile = name ? (char *)find_profile(init_profile_list, name) : 0;
                if (profile && key) bind_profile_to_key(profile, (int)key->valuedouble);
            }
            cjson_delete(root);
        }
        libc_free(text);
    }
    free_path(&path);
}

// ---- start-up ----
static void *calloc1(unsigned long long size) { return libc_calloc(1, size); }
static KeyMap *new_key_map(void (*handler)(void *), void (*release)(void *), bool multi) {
    KeyMap *m = (KeyMap *)calloc1(sizeof(KeyMap));
    if (!m) return 0;
    m->handler = handler;
    m->release = release;
    m->multi = multi;
    return m;
}
enum { ERR_NO_MEMORY = 0x3ed, ERR_NO_ENV = 0x7d3 };
static int setup(JNIEnv *env) {
    int err;
    seed_rng();
    // 1. the JNI ID table: game class loader, mapping detection (1.7.10 / 1.8.9, obf / MCP / SRG), all IDs
    JIds *ids = (JIds *)calloc1(sizeof(JIds));
    if (!ids) return ERR_NO_MEMORY;
    java_natives_world_cached_jni_ids = ids;
    LoaderRef secure;   // SecureClassLoader child of the game loader (the game loader itself is ids + 0x10)
    if ((err = init_jni_ids(ids, env, &secure))) return err;
    Jvm jvm = {ids, env};
    jobject game_loader = (jobject)((char *)ids + 0x10);

    // 2. state the frame code and the input handler read
    if ((err = create_module_manager(java_natives_world_module_manager, set_module_bind))) return err;           // module manager, key-bind setter = set_module_bind
    // Freeze the static setting values as the module defaults before any saved settings are loaded: the
    // built-in "Default" profile restores the `saved` slots, and a profile that omits a setting falls back
    // to them. Without this snapshot those slots stay zero (the image's .data only holds the live values),
    // so loading Default would zero every setting instead of restoring the original configuration.
    save_module_states(java_natives_world_module_manager);
    if (!(java_natives_world_key_state_table_one_byte = (unsigned char *)calloc1(0x100))) return ERR_NO_MEMORY;
    if (!(java_natives_frame_profile_key_binds_key_profile = new_key_map((void (*)(void *))on_profile_bind, libc_free, false))) return ERR_NO_MEMORY;
    if (!(java_natives_frame_module_key_binds_key_module = new_key_map((void (*)(void *))on_module_bind, 0, true))) return ERR_NO_MEMORY;
    if (!(init_profile_list = calloc1(0x20))) return ERR_NO_MEMORY;
    *(void **)((char *)init_profile_list + 0x18) = (void *)bind_profile_to_key;          // profile bind button setter
    // A profile folder that cannot be read makes the start-up degraded, not failed: the client then runs on the
    // built-in Default profile, exactly the way it already does for a fresh install with an empty folder. Aborting
    // here used to turn a ~/.slinky owned by root (an earlier sudo run) into "Initialization failed: 1013" and
    // FINAL STATUS: FAILED, with nothing telling anyone which folder was at fault.
    if ((err = load_profiles(init_profile_list))) {
        slinky_logf("Profiles in .slinky/profiles inside your user folder could not be read (code %u);"
                    " continuing with the built-in profile.", err);
        err = 0;
    }
    if ((err = init_chat_commands())) return err;                                 // chat commands
    init_glfw_entrypoints();                                                         // GLFW entry points (absent on LWJGL 2: not an error)
    load_settings();                                                   // global settings, profile binds, modules' own state
    // Modules with flags bit 1 (Notifications, Auto Rod, Block In, Weapons) have no toggle and are
    // never read from the settings file; they are on for the whole session. queue_module_toggle only queues the
    // enable, apply_module_toggles applies it on the first frame.
    // The GUI module carries the same bit, but its +0x14 byte is read everywhere as "the ClickGUI window
    // is open", so forcing it on throws the menu open on the first frame. It opens on its key bind instead,
    // and the Notifications module (also flagged, still forced on) prints the "Press ... to open GUI" hint.
    Mods *mods = (Mods *)java_natives_world_module_manager;
    for (unsigned long long i = 0; i < mods->count; i++)
        if ((mods->data[i]->flags & 0x2) && (void *)mods->data[i] != (void *)ptr_to_gui_module_descriptor && mods->data[i]->enabled != 1)
            queue_module_toggle(java_natives_world_module_manager, mods->data[i], 1);
    input_input_handler_hooked_into_game = on_input_event;                                             // window hook (installed by the frame code, hook_game_window)

    // 3. Java side: the netty packet tap, then the hooks that make the game call into the client
    if ((err = install_packet_channel(&jvm, game_loader, &init_slinkychannelhandler_class_constructor, (void *)on_packet_local))) return err;
    return hooks_set(env, true);
}

// Entry of the engine thread (platform/win/entry.cpp). Returns 0 or the error that stopped it (reported; the
// caller then unloads). The thread stays ATTACHED to the JVM when the start-up succeeds and later runs
// slinky_disarm on itself: the JVMTI environment that install_class_hooks fetches belongs to the thread that
// fetched it, and a thread that detaches (or exits) takes its environments - the capabilities, the event
// callbacks and the enabled ClassFileLoadHook with them - down with it. Detaching here instead would leave the
// cached g_jvmti unusable, so no unload could ever take the hooks out again.
extern "C" int slinky_setup(void *self) {
    init_our_module_handle = self;
    int err = get_java_vm((void **)&init_javavm);
    if (err) return err;
    bool attached;
    JNIEnv *env = attach(&attached);
    if (!env) return ERR_NO_ENV;
    err = setup(env);
    if (err) detach(attached);   // nothing is armed, so this thread may leave the JVM before the DLL goes
    if (err) slinky_logf("Initialization failed: %u", err);
    return err;
}

// Take the Java hooks out again; only valid on the engine thread (see slinky_setup).
// 0 = the game no longer calls into us, either because nothing was armed or because the disarm worked.
extern "C" int slinky_disarm(void) {
    if (!classfile_hooks_1_while_hooks_are_installed) return 0;
    bool attached;
    JNIEnv *env = attach(&attached);
    if (!env) return ERR_NO_ENV;
    int err = set_java_hooks(env, 0);
    if (!err) g_jvmti = 0;
    return err;
}

// The class restore, one step per frame of the game. RetransformClasses takes the whole VM into a global
// safepoint and throws away the compiled code of the classes it touches, so all nine in one call stop the game
// dead for the length of it - measured on a live client at ~260 ms of frozen window in the middle of the
// unload. One class per call puts each stop under a frame, and the game paints its own vanilla frame in
// between, so the client is gone the moment it is clicked off and nothing ever looks hung.
// 0 = the game has its original code back, 1 = call again next frame, anything else = the error that stopped
// the pass (the client stays resident and allocated, exactly as in the one-shot failure above).
static signed char g_disarm_step = -1;
FN(int, remove_class_hooks_step, (JNIEnv *env, void **jvmti, int step, int *next));
extern "C" int slinky_disarm_step(void) {
    if (!classfile_hooks_1_while_hooks_are_installed && g_disarm_step < 0) return 0;
    if (!init_javavm || !*init_javavm) return 0x7d3;
    bool attached;
    JNIEnv *env = attach(&attached);
    if (!env) return ERR_NO_ENV;
    // the one environment the install used, kept for the whole pass (see hooks_set on why a re-fetched
    // environment has neither the capabilities nor the event callbacks)
    if (!g_jvmti && (*init_javavm)->GetEnv(init_javavm, (void **)&g_jvmti, JVMTI_VERSION_1_2)) return ERR_NO_ENV;
    int next = -1;
    int err = remove_class_hooks_step(env, g_jvmti, g_disarm_step, &next);
    g_disarm_step = err ? -1 : (signed char)next;
    if (g_disarm_step < 0) g_jvmti = 0;
    if (err) slinky_logf("Disabling hooks failed: %u", err);
    return err ? err : (g_disarm_step >= 0);
}

// 1 while a stepped pass is half done: the classes it already handed back are vanilla, the rest of them still
// call into this DLL, and only this thread holds the JVMTI environment that can finish the job. The engine
// thread watches this to take the pass over if the game stops painting (platform/win/entry.cpp).
extern "C" int slinky_disarm_in_flight(void) { return g_disarm_step >= 0; }

// the engine thread leaves the JVM as the last step before it exits
extern "C" void slinky_detach(void) {
    if (init_javavm && *init_javavm) (*init_javavm)->DetachCurrentThread(init_javavm);
}

// ---- shutdown: everything slinky_setup built, once the hooks are gone (runs on the engine thread) ----
extern "C" void slinky_teardown(void) {
    // from here the packet tap and every Java native are inert: they check java_natives_frame_client_shutting_down_natives_check first, and the
    // frees below must not race one (this covers the start-up failure path too, where hook_update_display never ran)
    java_natives_frame_client_shutting_down_natives_check = 1;
    bool attached;
    JNIEnv *env = attach(&attached);
    if (env) {
        if (init_slinkychannelhandler_class_constructor.cls) release_packet_channel(env, &init_slinkychannelhandler_class_constructor.cls);
        if (java_natives_world_cached_jni_ids) release_jni_ids(java_natives_world_cached_jni_ids, env);
    }
    detach(attached);
    destroy_module_manager(java_natives_world_module_manager);
    key_map_destroy(java_natives_frame_profile_key_binds_key_profile);
    key_map_destroy(java_natives_frame_module_key_binds_key_module);
    destroy_profiles(init_profile_list);
    destroy_chat_commands();
    clear_saved_string();
    free_block_search_cache();
    libc_free(java_natives_world_key_state_table_one_byte);
    libc_free(java_natives_world_cached_jni_ids);
    java_natives_frame_profile_key_binds_key_profile = java_natives_frame_module_key_binds_key_module = 0;
    init_profile_list = 0;
    java_natives_world_key_state_table_one_byte = 0;
    java_natives_world_cached_jni_ids = 0;
}
