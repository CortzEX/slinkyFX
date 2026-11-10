// Anti Bot - tag entities that look like server-side bots (tag 2) so other
// modules can ignore them.  Per-player info is kept in a table keyed by UUID.
//   +0x48 tick:  re-evaluate every player
//   +0x98 event: on disable clear the table and untag everyone
#include "slinky/engine.h"
#include "slinky/jids.h"

struct Vec3 { double x, y, z;
#ifdef __linux__
    ~Vec3() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Key { unsigned long long hi, lo, padding_10, padding_18;
#ifdef __linux__
    ~Key() {}   // a struct with a destructor goes by address on SysV, which is how MS already passes it
#endif
};
struct Info {
    unsigned long long hi, lo;
    double moved;            // +0x10 accumulated horizontal movement^2
    unsigned char aged;      // +0x18 passed "min_age"
    unsigned char visible;   // +0x19 seen with armour while invisible / old enough
    unsigned char name_ok;   // +0x1a name has a valid character
};
struct EvHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };

extern "C" float anti_bot_min_age_value;            // "min_age"
extern "C" unsigned char anti_bot_always_stationary_value;    // "always_stationary"
extern "C" int anti_bot_invalid_uuid_value;              // "invalid_uuid"
extern "C" unsigned char anti_bot_sleeping_value;    // "sleeping"
extern "C" unsigned char anti_bot_always_invis_value;    // "always_invis"
extern "C" unsigned char anti_bot_invalid_username_value;    // "invalid_username"
extern "C" char anti_bot_module_descriptor[];           // Anti Bot module descriptor
extern "C" int anti_bot_world_id_table_belongs;              // world id the table belongs to
extern "C" void *anti_bot_info_table[1];         // info table
extern "C" char anti_bot_table_meta[];           // table meta
extern "C" const double threshold_1_in_anti_bot;    // "stationary" threshold


FN(int, java_call_object_hash_code, (void *jvm, void *world));
FN(void, libc_free, (void *ptr));
FN(void *, java_get_world_player_entities, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(void *, java_get_entity_uuid, (void *jvm, void *entity));
FN(unsigned long long, java_jni_call_long_method_uuid_get_most_significant_bits, (void *jvm, void *uuid));
FN(unsigned long long, java_jni_call_long_method_uuid_get_least_significant_bits, (void *jvm, void *uuid));
FN(Info *, vector_find_insert, (void *table, int keysz, void *meta, void *key, void *hash));
FN(void, compare_u128, ());
FN(Vec3, java_get_entity_prev_pos_x, (void *jvm, void *entity));
FN(Vec3, java_get_entity_pos_x, (void *jvm, void *entity));
FN(unsigned char, java_get_entity_player_sleeping, (void *jvm, void *entity_player));
FN(int, java_get_entity_ticks_existed, (void *jvm, void *entity));
FN(unsigned char, java_call_entity_player_is_invisible_to_player, (void *jvm, void *entity_player, void *viewer));
FN(void *, java_get_entity_player_inventory, (void *jvm, void *entity_player));
FN(void *, java_call_iinventory_get_stack_in_slot, (void *jvm, void *inv, int slot));
FN(void *, java_call_item_stack_get_item, (void *jvm, void *stack));
FN(int, java_call_static_item_get_id_from_item, (void *jvm, void *item));
FN(void *, java_call_entity_get_command_sender_name, (void *jvm, void *entity));
FN(void, entity_flags_set, (void *jvm, void *entity, int tag));
FN(void, entity_flags_clear, (void *jvm, void *entity, int tag));

static inline bool has_item(void *jvm, void *inv, int slot) {
    void *stack = java_call_iinventory_get_stack_in_slot(jvm, inv, slot);
    if (!stack)
        return false;
    void *item = java_call_item_stack_get_item(jvm, stack);
    return item && java_call_static_item_get_id_from_item(jvm, item) != 0;
}

extern "C" void anti_bot_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    int wid = ctx->world ? java_call_object_hash_code(ctx->jvm, ctx->world) : 0;
    if (wid != anti_bot_world_id_table_belongs) {
        libc_free(anti_bot_info_table[0]);
        anti_bot_info_table[0] = nullptr;
        __builtin_memset(anti_bot_table_meta, 0, 8);   // cap + count
        anti_bot_world_id_table_belongs = wid;
    }
    if (!ctx->world)
        return;
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
        k.padding_10 = k.padding_18 = 0;
        Info *inf = vector_find_insert(anti_bot_info_table, 0x20, anti_bot_table_meta, &k, (void *)compare_u128);
        Vec3 now = java_get_entity_prev_pos_x(ctx->jvm, element);
        Vec3 last = java_get_entity_pos_x(ctx->jvm, element);
        double dx = now.x - last.x, dz = now.z - last.z;
        double moved = dx * dx + dz * dz + inf->moved;
        inf->moved = moved;
        if (anti_bot_always_stationary_value == 1 && threshold_1_in_anti_bot > moved)
            goto bot;
        if (anti_bot_invalid_uuid_value != 0) {
            unsigned char still = threshold_1_in_anti_bot > moved;
            unsigned char v2 = anti_bot_invalid_uuid_value == 2;
            if (((unsigned)inf->hi & 0xf000) == 0x2000 && (v2 | still))
                goto bot;
        }
        if (anti_bot_sleeping_value == 1 && java_get_entity_player_sleeping(ctx->jvm, element))
            goto bot;
        {
            int age = java_get_entity_ticks_existed(ctx->jvm, element);
            if (anti_bot_always_invis_value == 1 && !inf->visible) {
                void *jvm = ctx->jvm;
                if (java_call_entity_player_is_invisible_to_player(jvm, element, ctx->player)) {
                    void *inv = java_get_entity_player_inventory(jvm, element);
                    if (!has_item(jvm, inv, 0x24) && !has_item(jvm, inv, 0x25) &&
                        !has_item(jvm, inv, 0x26) && !has_item(jvm, inv, 0x27))
                        goto bot;
                }
                if (age >= 21)
                    inf->visible = 1;
            }
            float min_age = anti_bot_min_age_value;
            if (min_age > 0.0f && !inf->aged) {
                if (min_age > (float)age)
                    goto bot;
                inf->aged = 1;
            }
        }
        if (anti_bot_invalid_username_value == 1 && !inf->name_ok) {
            JNIEnv *env = ((Jvm *)ctx->jvm)->env;
            void *name = java_call_entity_get_command_sender_name(ctx->jvm, element);
            int len = (*env)->GetStringLength(env, name);
            unsigned short *ch = (unsigned short *)(*env)->GetStringCritical(env, name, nullptr);
            for (int j = 0; j < len; j++) {
                unsigned d = ch[j];
                if (d == 0xa7) {   // colour code: skip the code character too
                    j++;
                    continue;
                }
                if ((unsigned short)((d & 0xffdf) - 0x41) < 0x1a || d == 0x5f || (unsigned short)(d - 0x30) <= 9) {
                    inf->name_ok = 1;
                    break;
                }
            }
            (*env)->ReleaseStringCritical(env, name, ch);
            if (!inf->name_ok)
                goto bot;
        }
        entity_flags_clear(ctx->jvm, element, 2);
        continue;
bot:
        entity_flags_set(ctx->jvm, element, 2);
    }
}

extern "C" void anti_bot_on_toggle(EvHook *hook) {
    if (__builtin_expect(hook->ev != anti_bot_module_descriptor || hook->on, 0))
        return;
    void *tbl = anti_bot_info_table[0];
    asm("" : "+r"(tbl));
    libc_free(tbl);
    anti_bot_info_table[0] = nullptr;
    __builtin_memset(anti_bot_table_meta, 0, 8);   // cap + count
    Ctx *ctx = hook->ctx;
    if (!ctx->world)
        return;
    void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
    for (int i = 0; i < java_call_list_size(ctx->jvm, list); i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        JNIEnv *obj = ((Jvm *)ctx->jvm)->env;
        auto pred = (*obj)->IsSameObject;
        if (!pred(obj, ctx->player, element))
            entity_flags_clear(ctx->jvm, element, 2);
    }
}
