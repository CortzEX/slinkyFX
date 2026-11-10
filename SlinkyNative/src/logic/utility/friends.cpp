// Friends - friend list kept in a hash table keyed by player UUID.
//   friends_on_middle_click_add(a, ui): middle-click on a player toggles it in the list (+ notification)
//   friends_on_tick(hook):  every tick tag friends in the entity list (and refresh last-seen)
//   friends_on_toggle(hook):  on disable untag everyone
//   friends_on_shutdown(hook):  free the table
#include "slinky/engine.h"
#include "slinky/jids.h"

struct Key { unsigned long long hi, lo, seen, padding_18;
#ifdef __linux__
    ~Key() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};          // 0x20 bytes
struct Entry { char padding_00[0x10]; long long seen; };
struct Notice { int kind; int padding_04; unsigned char added; char rest[0x3f]; };   // 0x48 bytes
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };
extern "C" unsigned char friends_middle_click_add_value;   // "middle_click_add"
extern "C" char friends_module_descriptor[];          // Friends module descriptor
extern "C" void *friend_set_friend_table;           // friend table
extern "C" char friend_set_table_meta[];          // table meta


FN(void *, get_minecraft, (void *jvm));
FN(void *, java_get_minecraft_object_mouse_over, (void *jvm, void *minecraft));
FN(void *, java_get_moving_object_position_entity_hit, (void *jvm, void *moving_object_position));
FN(void *, java_jni_is_instance_of_entity_other_player_mp, (void *jvm, void *candidate));
FN(void *, java_get_entity_uuid, (void *jvm, void *entity));
FN(unsigned long long, java_jni_call_long_method_uuid_get_most_significant_bits, (void *jvm, void *uuid));
FN(unsigned long long, java_jni_call_long_method_uuid_get_least_significant_bits, (void *jvm, void *uuid));
FN(long long, libc_time64, (void *t));
FN(Entry *, binary_search, (void *table, int keysz, void *meta, void *key, void *hash));
FN(void, sorted_insert, (void *table, int keysz, void *meta, void *key, void *hash));
FN(void, vector_erase_value, (void *table, int keysz, void *meta, void *entry));
FN(unsigned char, entity_flags_has, (void *jvm, void *entity, int mode));
FN(void, dispatch_event, (void *ui, Notice *n));
FN(void *, java_get_world_player_entities, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(void, entity_flags_set, (void *jvm, void *entity, int tag));
FN(void, entity_flags_clear, (void *jvm, void *entity, int tag));
FN(void, compare_friend_key, ());
FN(void, libc_free, (void *ptr));

extern "C" void friends_on_middle_click_add(void *jvm, void *mgr) {
    if (friends_middle_click_add_value == 1) {
        void *minecraft = java_get_minecraft_object_mouse_over(jvm, get_minecraft(jvm));
        if (minecraft) {
            void *moving_object_position = java_get_moving_object_position_entity_hit(jvm, minecraft);
            if (moving_object_position) {
                void *ent = java_jni_is_instance_of_entity_other_player_mp(jvm, moving_object_position);
                if (ent) {
                    void *uuid = java_get_entity_uuid(jvm, ent);
                    Key k;
                    k.hi = java_jni_call_long_method_uuid_get_most_significant_bits(jvm, uuid);
                    k.lo = java_jni_call_long_method_uuid_get_least_significant_bits(jvm, uuid);
                    k.seen = libc_time64(nullptr);
                    k.padding_18 = 0;
                    Notice n = {};
                    Entry *entry = binary_search(friend_set_friend_table, 0x20, friend_set_table_meta, &k, (void *)compare_friend_key);
                    if (entry) {
                        n.kind = 0x13;
                        n.added = 0;
                        vector_erase_value(friend_set_friend_table, 0x20, friend_set_table_meta, entry);
                    } else {
                        if (__builtin_expect(entity_flags_has(jvm, ent, 2), 0))
                            goto out;
                        n.kind = 0x13;
                        n.added = 1;
                        sorted_insert(&friend_set_friend_table, 0x20, friend_set_table_meta, &k, (void *)compare_friend_key);
                    }
                    dispatch_event(mgr, &n);
                }
            }
        }
    }
out:;
}

extern "C" void friends_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (!ctx->player)
        return;
    long long now = libc_time64(nullptr);
    void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
    for (int i = 0; i < java_call_list_size(ctx->jvm, list); i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        JNIEnv *obj = ((Jvm *)ctx->jvm)->env;
        auto pred = (*obj)->IsSameObject;
        if (pred(obj, ctx->player, element))
            continue;
        void *uuid = java_get_entity_uuid(ctx->jvm, element);
        alignas(16) Key k;
        k.hi = java_jni_call_long_method_uuid_get_most_significant_bits(ctx->jvm, uuid);
        k.lo = java_jni_call_long_method_uuid_get_least_significant_bits(ctx->jvm, uuid);
        k.seen = 0;
        k.padding_18 = 0;
        Entry *entry = binary_search(friend_set_friend_table, 0x20, friend_set_table_meta, &k, (void *)compare_friend_key);
        if (entry) {
            entity_flags_set(ctx->jvm, element, 4);
            entry->seen = now;
        } else {
            entity_flags_clear(ctx->jvm, element, 4);
        }
    }
}

extern "C" void friends_on_toggle(EvHook *hook) {
    if (__builtin_expect(hook->ev != friends_module_descriptor || hook->on, 0))
        return;
    Ctx *ctx = hook->ctx;
    if (!ctx->world)
        return;
    void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
    for (int i = 0; i < java_call_list_size(ctx->jvm, list); i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        JNIEnv *obj = ((Jvm *)ctx->jvm)->env;
        auto pred = (*obj)->IsSameObject;
        if (!pred(obj, ctx->player, element))
            entity_flags_clear(ctx->jvm, element, 4);
    }
}

extern "C" void friends_on_shutdown(Hook *hook) {
    libc_free(friend_set_friend_table);
    *(__int128 *)&friend_set_friend_table = 0;
}
