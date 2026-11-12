/* Chess Online: our own small chess engine for "Play vs computer" (original code, portable C99, host-tested).
 * Negamax alpha-beta with iterative deepening, capture-only quiescence, simple move ordering and a material +
 * piece-square evaluation. Eight levels: lower levels search less and add noise to their choice. */
#ifndef CO_ENGINE_H
#define CO_ENGINE_H

#include <stdint.h>

#include "chess.h"

#define ENGINE_LEVELS 8

typedef uint64_t (*engine_clock_fn)(void);

typedef struct {
    int depth_reached;
    int score;        /* centipawns for the side to move */
    uint64_t nodes;
    int timed_out;
} engine_info;

/* Picks a move for the side to move. level 1..8; seed makes the noise repeatable; now_ms may be NULL (no time
 * limit beyond the level's depth). Returns 0, or -1 when there is no legal move. *stop, when not NULL, aborts. */
int engine_pick(const chess_pos *p, int level, uint32_t seed, engine_clock_fn now_ms, volatile int *stop,
                chess_move *out, engine_info *info);

/* Static evaluation, centipawns from White's point of view. */
int engine_eval_white(const chess_pos *p);

/* Draw by material: kings only, or a king and one minor piece against a king. */
int engine_insufficient_material(const chess_pos *p);

#endif
