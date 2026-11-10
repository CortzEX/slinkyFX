// Teams - descriptors +0x48 (per tick) / +0x98 (event) / +0x1e8 (shutdown).
// +0x48: work out the local player's team colour code, then sweep the entity
//        list and tag every entity whose team colour matches / is allowed.
// +0x98: on disable, release every tagged entity.
// +0x1e8: free the colour table.
#include "slinky/engine.h"
#include "slinky/jids.h"

extern "C" unsigned char teams_match_self_color_value;   // "match_self_color"
extern "C" void *teams_colour_table;           // colour table
extern "C" char key_colour_vector_header[];

struct Vtbl { void *slot[0x18]; };      // slot[0x18] (+0xc0): is-team-mate predicate

FN(void *, java_call_minecraft_get_net_handler, (void *jvm, void *minecraft));
FN(void *, java_get_net_handler_play_client, (void *jvm, void *net_handler_play_client));
FN(void *, java_get_entity_uuid, (void *jvm, void *entity));
FN(void *, java_call_map_get, (void *jvm, void *object, void *key));
FN(void *, java_get_network_player_info_display_name, (void *jvm, void *network_player_info));
FN(void *, java_call_ichat_component_get_formatted_text, (void *jvm, void *object));
FN(unsigned short, name_prefix_code, (void *jvm, void *object));
FN(void *, java_call_network_player_info_get_player_team, (void *jvm, void *network_player_info));
FN(unsigned short, name_last_code, (void *jvm, void *object));
FN(unsigned short, entity_name_colour, (void *jvm, void *entity));
FN(void *, java_get_world_player_entities, (void *jvm, void *world));
FN(int, java_call_list_size, (void *jvm, void *list));
FN(void *, java_call_list_get, (void *jvm, void *list, int i));
FN(void, entity_flags_set, (void *jvm, void *entity, int on));
FN(void, entity_flags_clear, (void *jvm, void *entity, int on));
FN(void *, binary_search, (void *table, int n, void *key, void *val, void *cb));
FN(void, compare_signed_chars, ());
FN(void, libc_free, (void *ptr));

extern "C" void teams_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    if (ctx->player == nullptr)
        return;
    unsigned short team;
    if (**(int **)ctx->jvm) {
        void *minecraft_get_net_handler = java_call_minecraft_get_net_handler(ctx->jvm, ctx->minecraft);
        void *net_handler_play_client = java_get_net_handler_play_client(ctx->jvm, minecraft_get_net_handler);
        void *pid = java_get_entity_uuid(ctx->jvm, ctx->player);
        void *map_get = java_call_map_get(ctx->jvm, net_handler_play_client, pid);
        if (!map_get) {
            team = entity_name_colour(ctx->jvm, ctx->player);
        } else {
            void *network_player_info = java_get_network_player_info_display_name(ctx->jvm, map_get);
            if (network_player_info) {
                unsigned short v = name_prefix_code(ctx->jvm, java_call_ichat_component_get_formatted_text(ctx->jvm, network_player_info));
                team = v ? v : 0x72;
            } else {
                void *jvm = java_call_network_player_info_get_player_team(ctx->jvm, map_get);
                team = jvm ? name_last_code(ctx->jvm, jvm) : entity_name_colour(ctx->jvm, ctx->player);
            }
        }
    } else {
        team = entity_name_colour(ctx->jvm, ctx->player);
    }
    void *list = java_get_world_player_entities(ctx->jvm, ctx->world);
    for (int i = 0; i < java_call_list_size(ctx->jvm, list); i++) {
        void *element = java_call_list_get(ctx->jvm, list, i);
        JNIEnv *obj = ((Jvm *)ctx->jvm)->env;
        auto pred = (*obj)->IsSameObject;
        if (pred(obj, ctx->player, element))
            continue;
        unsigned short col = entity_name_colour(ctx->jvm, element);
        if (teams_match_self_color_value == 1 && col == team) {
            entity_flags_set(ctx->jvm, element, 1);
            continue;
        }
        signed char b = (signed char)col;
        char key = (char)((b >= 0x61 ? 0xa9 : 0xd0) + b);
        if (binary_search(teams_colour_table, 1, key_colour_vector_header, &key, (void *)compare_signed_chars))
            entity_flags_set(ctx->jvm, element, 1);
        else
            entity_flags_clear(ctx->jvm, element, 1);
    }
}

struct EventHook { char padding_00[8]; Ctx *ctx; void *ev; unsigned char on; };   // +0x10 / +0x18
extern "C" char teams_module_descriptor[];

extern "C" void teams_on_toggle(EventHook *hook) {
    if (__builtin_expect(hook->ev != teams_module_descriptor || hook->on, 0))
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
            entity_flags_clear(ctx->jvm, element, 1);
    }
}

extern "C" void teams_on_shutdown(Hook *hook) {
    libc_free(teams_colour_table);
    *(__int128 *)&teams_colour_table = 0;
}
