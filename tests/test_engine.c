/* Chess Online: host tests for our engine (engine.c). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "chess.h"
#include "engine.h"

static int fails, checks;
#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(cond)) {                                                                                                 \
            fails++;                                                                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static const char *pick_uci(const char *fen, int level, uint32_t seed, engine_info *info)
{
    static char buf[6];
    chess_pos p;
    chess_move m;
    chess_set_fen(&p, fen);
    if (engine_pick(&p, level, seed, now_ms, NULL, &m, info)) return "none";
    chess_move_uci(m, buf);
    return buf;
}

int main(void)
{
    engine_info info;
    int lv, seed;
    /* mate in one: the back-rank mate */
    for (lv = 3; lv <= 8; lv++) {
        const char *u = pick_uci("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", lv, 7, &info);
        CHECK(!strcmp(u, "a1a8"), "level %d finds Ra8#: %s", lv, u);
    }
    /* take a hanging queen */
    for (lv = 3; lv <= 8; lv++) {
        const char *u = pick_uci("rnb1kbnr/pppp1ppp/8/4p3/3qP3/2N5/PPPP1PPP/R1BQKBNR w KQkq - 0 1", lv, 3, &info);
        (void)u;
        u = pick_uci("4k3/8/8/3q4/8/8/8/3QK3 w - - 0 1", lv, 3, &info);
        CHECK(!strcmp(u, "d1d5"), "level %d takes the free queen: %s", lv, u);
    }
    /* don't hang the queen at the higher levels: Qxb7?? is met by ...Bxb7-like recaptures; here the queen on d1
       can take a defended pawn on d5 (defended by e6) and must not */
    for (lv = 5; lv <= 8; lv++) {
        const char *u = pick_uci("4k3/8/4p3/3p4/8/8/8/3QK3 w - - 0 1", lv, 11, &info);
        CHECK(strcmp(u, "d1d5"), "level %d avoids Qxd5?? (defended): %s", lv, u);
    }
    /* mate in two for level 8 (depth 4+): 1.Qh5+?? no; use a classic: K and Q vs K */
    {
        const char *u = pick_uci("7k/8/5K2/8/8/8/8/6Q1 w - - 0 1", 8, 1, &info);
        chess_pos p;
        chess_move m;
        chess_set_fen(&p, "7k/8/5K2/8/8/8/8/6Q1 w - - 0 1");
        CHECK(chess_parse_uci(&p, u, &m) == 0, "level 8 returns a legal move: %s", u);
        chess_make(&p, m);
        CHECK(chess_status(&p) != CH_STALEMATE, "level 8 does not stalemate: %s", u);
    }
    /* every level always returns a legal move, also from varied positions, within reasonable time */
    {
        const char *fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                              "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                              "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"};
        size_t f;
        for (f = 0; f < sizeof fens / sizeof *fens; f++)
            for (lv = 1; lv <= 8; lv++)
                for (seed = 1; seed <= 2; seed++) {
                    chess_pos p;
                    chess_move m, chk;
                    char u[6];
                    uint64_t t0 = now_ms();
                    chess_set_fen(&p, fens[f]);
                    CHECK(engine_pick(&p, lv, (uint32_t)seed, now_ms, NULL, &m, &info) == 0, "pick");
                    chess_move_uci(m, u);
                    CHECK(chess_parse_uci(&p, u, &chk) == 0, "level %d legal: %s", lv, u);
                    CHECK(now_ms() - t0 < 6000, "level %d took %llu ms", lv, (unsigned long long)(now_ms() - t0));
                    if (lv == 8 && seed == 1)
                        printf("level 8 on position %d: depth %d, %llu nodes, %llu ms\n", (int)f, info.depth_reached,
                               (unsigned long long)info.nodes, (unsigned long long)(now_ms() - t0));
                }
    }
    /* stop flag aborts at once and still answers */
    {
        chess_pos p;
        chess_move m;
        volatile int stop = 1;
        chess_startpos(&p);
        CHECK(engine_pick(&p, 8, 1, now_ms, &stop, &m, &info) == 0, "stopped search still answers");
    }
    /* no legal moves */
    {
        chess_pos p;
        chess_move m;
        chess_set_fen(&p, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        CHECK(engine_pick(&p, 4, 1, now_ms, NULL, &m, &info) == -1, "stalemate: no move");
    }
    /* insufficient material */
    {
        chess_pos p;
        chess_set_fen(&p, "8/8/4k3/8/8/2N5/8/4K3 w - - 0 1");
        CHECK(engine_insufficient_material(&p), "K+N v K");
        chess_set_fen(&p, "8/8/4k3/8/8/2R5/8/4K3 w - - 0 1");
        CHECK(!engine_insufficient_material(&p), "K+R v K is not");
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
