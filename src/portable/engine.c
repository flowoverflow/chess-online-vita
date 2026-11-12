/* Chess Online: our own small chess engine. Original code; see engine.h. */
#include "engine.h"

#include <string.h>

#define INF 1000000
#define MATE 100000

static const int VAL[7] = {0, 100, 320, 330, 500, 900, 0};

typedef struct {
    int depth;      /* full-width depth */
    int quiesce;    /* capture search at the leaves */
    int noise;      /* +/- centipawns added to root choices */
    int time_ms;    /* soft limit for iterative deepening (0: none) */
} level_cfg;

/* 1: plays almost at random among sensible moves ... 8: deepest and steady */
static const level_cfg LEVELS[ENGINE_LEVELS] = {
    {1, 0, 350, 0}, {1, 0, 150, 0}, {2, 1, 90, 0}, {2, 1, 45, 0},
    {3, 1, 25, 1500}, {3, 1, 10, 2000}, {4, 1, 4, 2500}, {6, 1, 0, 3000},
};

typedef struct {
    uint64_t nodes, deadline;
    engine_clock_fn now;
    volatile int *stop;
    int aborted;
    int quiesce;
} ctx;

static int chebyshev_center(int sq)
{
    int f = CH_FILE(sq), r = CH_RANK(sq);
    int df = f < 4 ? 3 - f : f - 4, dr = r < 4 ? 3 - r : r - 4;
    return df > dr ? df : dr; /* 0 for the four centre squares .. 3 for the rim */
}

static int non_pawn_material(const chess_pos *p)
{
    int sq, m = 0;
    for (sq = 0; sq < 64; sq++) {
        int t = CH_TYPE(p->sq[sq]);
        if (t >= CH_KNIGHT && t <= CH_QUEEN) m += VAL[t];
    }
    return m;
}

static int piece_square(int type, int color, int sq, int endgame)
{
    int r = color == CH_WHITE ? CH_RANK(sq) : 7 - CH_RANK(sq); /* rank from the owner's side */
    int f = CH_FILE(sq), c = chebyshev_center(sq);
    switch (type) {
    case CH_PAWN: {
        int v = (r - 1) * (endgame ? 14 : 6);
        if ((f == 3 || f == 4) && (r == 3 || r == 4)) v += 12;
        if ((f == 3 || f == 4) && r == 1) v -= 8; /* get the centre pawns moving */
        return v;
    }
    case CH_KNIGHT: return 18 - 8 * c;
    case CH_BISHOP: return 9 - 4 * c + (r == 0 ? -6 : 0);
    case CH_ROOK: return (r == 6 ? 18 : 0) + (f == 3 || f == 4 ? 4 : 0);
    case CH_QUEEN: return 4 - 2 * c;
    case CH_KING:
        if (endgame) return 24 - 8 * c;
        if (r == 0 && (f <= 2 || f >= 6)) return 18; /* castled or tucked away */
        return r == 0 ? 0 : -14 * r;
    }
    return 0;
}

int engine_eval_white(const chess_pos *p)
{
    int sq, score = 0, endgame = non_pawn_material(p) <= 2600, bishops[2] = {0, 0};
    for (sq = 0; sq < 64; sq++) {
        int pc = p->sq[sq], t, c, v;
        if (!pc) continue;
        t = CH_TYPE(pc);
        c = CH_COLOR(pc);
        v = VAL[t] + piece_square(t, c, sq, endgame);
        if (t == CH_BISHOP) bishops[c]++;
        score += c == CH_WHITE ? v : -v;
    }
    if (bishops[0] >= 2) score += 25;
    if (bishops[1] >= 2) score -= 25;
    return score;
}

int engine_insufficient_material(const chess_pos *p)
{
    int sq, minors = 0;
    for (sq = 0; sq < 64; sq++) {
        int t = CH_TYPE(p->sq[sq]);
        if (!t || t == CH_KING) continue;
        if (t == CH_KNIGHT || t == CH_BISHOP) minors++;
        else return 0;
    }
    return minors <= 1;
}

static int eval_side(const chess_pos *p)
{
    int e = engine_eval_white(p);
    return p->side == CH_WHITE ? e : -e;
}

static int move_order_score(const chess_pos *p, chess_move m)
{
    int s = 0;
    if (m.flags & CH_F_CAPTURE) {
        int victim = (m.flags & CH_F_EP) ? CH_PAWN : CH_TYPE(p->sq[m.to]);
        s += 10000 + 10 * VAL[victim] - VAL[CH_TYPE(p->sq[m.from])] / 10;
    }
    if (m.promo) s += 9000 + VAL[m.promo];
    return s;
}

static void order(const chess_pos *p, chess_move *m, int n, const chess_move *first)
{
    int sc[CH_MAX_MOVES], i, j;
    for (i = 0; i < n; i++) {
        sc[i] = move_order_score(p, m[i]);
        if (first && m[i].from == first->from && m[i].to == first->to && m[i].promo == first->promo) sc[i] = 1 << 30;
    }
    for (i = 1; i < n; i++) { /* insertion sort, highest first */
        chess_move mv = m[i];
        int s = sc[i];
        for (j = i - 1; j >= 0 && sc[j] < s; j--) {
            m[j + 1] = m[j];
            sc[j + 1] = sc[j];
        }
        m[j + 1] = mv;
        sc[j + 1] = s;
    }
}

static int out_of_time(ctx *c)
{
    if (c->aborted) return 1;
    if ((++c->nodes & 1023) == 0) {
        if (c->stop && *c->stop) c->aborted = 1;
        else if (c->deadline && c->now && c->now() >= c->deadline) c->aborted = 1;
    }
    return c->aborted;
}

static int qsearch(ctx *c, const chess_pos *p, int alpha, int beta, int ply)
{
    chess_move m[CH_MAX_MOVES];
    int n, i, stand;
    if (out_of_time(c)) return 0;
    stand = eval_side(p);
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    if (ply > 24) return stand;
    n = chess_legal_moves(p, m);
    if (!n) return chess_in_check(p, p->side) ? -MATE + ply : 0;
    order(p, m, n, NULL);
    for (i = 0; i < n; i++) {
        chess_pos q;
        int s;
        if (!(m[i].flags & CH_F_CAPTURE) && !m[i].promo) break; /* ordered: the rest are quiet */
        q = *p;
        chess_make(&q, m[i]);
        s = -qsearch(c, &q, -beta, -alpha, ply + 1);
        if (c->aborted) return 0;
        if (s >= beta) return s;
        if (s > alpha) alpha = s;
    }
    return alpha;
}

static int search(ctx *c, const chess_pos *p, int depth, int alpha, int beta, int ply)
{
    chess_move m[CH_MAX_MOVES];
    int n, i, best = -INF;
    if (out_of_time(c)) return 0;
    n = chess_legal_moves(p, m);
    if (!n) return chess_in_check(p, p->side) ? -MATE + ply : 0;
    if (p->halfmove >= 100 || engine_insufficient_material(p)) return 0;
    if (depth <= 0) return c->quiesce ? qsearch(c, p, alpha, beta, ply) : eval_side(p);
    order(p, m, n, NULL);
    for (i = 0; i < n; i++) {
        chess_pos q = *p;
        int s;
        chess_make(&q, m[i]);
        s = -search(c, &q, depth - 1, -beta, -alpha, ply + 1);
        if (c->aborted) return 0;
        if (s > best) best = s;
        if (s > alpha) alpha = s;
        if (alpha >= beta) break;
    }
    return best;
}

static uint32_t rng(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

int engine_pick(const chess_pos *p, int level, uint32_t seed, engine_clock_fn now_ms, volatile int *stop,
                chess_move *out, engine_info *info)
{
    chess_move m[CH_MAX_MOVES], best_move, root_moves[CH_MAX_MOVES];
    int n, i, d, root_scores[CH_MAX_MOVES];
    const level_cfg *L;
    ctx c;
    uint32_t s = seed ? seed : 0x9E3779B9u;
    engine_info dummy;
    if (!info) info = &dummy;
    memset(info, 0, sizeof *info);
    if (level < 1) level = 1;
    if (level > ENGINE_LEVELS) level = ENGINE_LEVELS;
    L = &LEVELS[level - 1];
    n = chess_legal_moves(p, m);
    if (!n) return -1;
    memset(&c, 0, sizeof c);
    c.now = now_ms;
    c.stop = stop;
    c.quiesce = L->quiesce;
    if (L->time_ms && now_ms) c.deadline = now_ms() + (uint64_t)L->time_ms;
    best_move = m[0];
    for (i = 0; i < n; i++) root_scores[i] = 0;
    for (d = 1; d <= L->depth; d++) {
        int scores[CH_MAX_MOVES], alpha = -INF;
        order(p, m, n, d > 1 ? &best_move : NULL);
        for (i = 0; i < n; i++) {
            chess_pos q = *p;
            chess_make(&q, m[i]);
            /* full window at the root so every move gets a comparable score for the noise */
            scores[i] = -search(&c, &q, d - 1, -INF, INF, 1);
            if (c.aborted) break;
            if (scores[i] > alpha) alpha = scores[i];
        }
        if (c.aborted) {
            info->timed_out = 1;
            break;
        }
        /* keep this finished iteration's moves with their scores: the next one reorders m */
        for (i = 0; i < n; i++) {
            root_scores[i] = scores[i];
            root_moves[i] = m[i];
        }
        info->depth_reached = d;
        {
            int bi = 0;
            for (i = 1; i < n; i++)
                if (root_scores[i] > root_scores[bi]) bi = i;
            best_move = root_moves[bi];
        }
        if (alpha >= MATE - 100) break; /* a forced mate was found */
        if (c.deadline && now_ms && now_ms() + 10 >= c.deadline) break;
    }
    if (!info->depth_reached) { /* not even depth 1: take the ordered first move */
        *out = m[0];
        info->nodes = c.nodes;
        return 0;
    }
    /* choose: best score plus level noise (never throwing away a found mate) */
    {
        int bi = -1, bv = -INF * 2;
        for (i = 0; i < n; i++) {
            int v = root_scores[i];
            if (L->noise && v < MATE - 100 && v > -MATE + 100)
                v += (int)(rng(&s) % (uint32_t)(2 * L->noise + 1)) - L->noise;
            if (v > bv) {
                bv = v;
                bi = i;
            }
        }
        *out = root_moves[bi];
        info->score = root_scores[bi];
    }
    info->nodes = c.nodes;
    return 0;
}
