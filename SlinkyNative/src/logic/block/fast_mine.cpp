// Fast Mine - descriptor +0x48 / +0x168.
// +0x48:  cap the block-hit delay at "break_delay" and optionally count it down by one.
// +0x168: scale the mining progress so a block breaks in fewer ticks ("speed").
#include "slinky/engine.h"

extern "C" float fast_mine_break_delay_value;           // "break_delay"
extern "C" unsigned char fast_mine_decr_block_hit_delay_value;   // "decr_block_hit_delay"
extern "C" float fast_mine_speed;           // "speed"
extern "C" int fast_mine_last_block_hit_delay;             // last block-hit delay
extern "C" float fast_mine_last_progress_value;           // last progress value
extern "C" const float one_in_fast_mine_b;
extern "C" const double one_in_fast_mine;

FN(void *, java_get_minecraft_player_controller, (void *jvm, void *minecraft));
FN(float, java_get_player_controller_mp_cur_block_damage_mp, (void *jvm, void *blk));
FN(int, java_get_player_controller_mp_block_hit_delay, (void *jvm, void *blk));
FN(void, java_set_player_controller_mp_block_hit_delay, (void *jvm, void *blk, int value));
FN(void, java_set_player_controller_mp_cur_block_damage_mp, (void *jvm, void *blk, float value));
FN(float, libc_ceilf, (float v));

extern "C" void fast_mine_on_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    int delay = 0;
    void *blk = java_get_minecraft_player_controller(ctx->jvm, ctx->minecraft);
    if (blk) {
        fast_mine_last_progress_value = java_get_player_controller_mp_cur_block_damage_mp(ctx->jvm, blk);
        delay = java_get_player_controller_mp_block_hit_delay(ctx->jvm, blk);
        int v;
        if ((float)delay > fast_mine_break_delay_value)
            v = (int)fast_mine_break_delay_value;
        else if (delay > 0 && fast_mine_decr_block_hit_delay_value && delay == fast_mine_last_block_hit_delay)
            v = delay - 1;
        else
            goto done;
        java_set_player_controller_mp_block_hit_delay(ctx->jvm, blk, v);
    }
done:
    fast_mine_last_block_hit_delay = delay;
}

extern "C" void fast_mine_on_late_tick(Hook *hook) {
    Ctx *ctx = hook->ctx;
    void *blk = java_get_minecraft_player_controller(ctx->jvm, ctx->minecraft);
    if (!blk)
        return;
    float prog = java_get_player_controller_mp_cur_block_damage_mp(ctx->jvm, blk);
    if (prog == 0.0f || fast_mine_last_progress_value != 0.0f)
        return;
    double d = prog;
    long long hits = (long long)libc_ceilf(one_in_fast_mine_b / prog);
    double sp = fast_mine_speed;
    double q = one_in_fast_mine / sp / d;
    long long fast = (long long)libc_ceilf((float)q);
    unsigned int n = (unsigned int)((int)hits - (int)fast + 1);
    java_set_player_controller_mp_cur_block_damage_mp(ctx->jvm, blk, (float)(long long)n * prog);
}
