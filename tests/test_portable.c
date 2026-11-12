/* Chess Online: host tests for the portable code (rules, JSON). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess.h"
#include "json.h"

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

static void perft_case(const char *fen, int depth, unsigned long long want)
{
    chess_pos p;
    unsigned long long got;
    CHECK(chess_set_fen(&p, fen) == 0, "fen parses: %s", fen);
    got = (unsigned long long)chess_perft(&p, depth);
    CHECK(got == want, "perft(%d) %s = %llu, want %llu", depth, fen, got, want);
}

static void test_perft(void)
{
    /* standard published perft results (chessprogramming.org "Perft Results") */
    const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char *kiwi = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    const char *pos3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    const char *pos4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    const char *pos5 = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    perft_case(start, 1, 20);
    perft_case(start, 2, 400);
    perft_case(start, 3, 8902);
    perft_case(start, 4, 197281);
    perft_case(kiwi, 1, 48);
    perft_case(kiwi, 2, 2039);
    perft_case(kiwi, 3, 97862);
    perft_case(pos3, 1, 14);
    perft_case(pos3, 2, 191);
    perft_case(pos3, 3, 2812);
    perft_case(pos3, 4, 43238);
    perft_case(pos4, 1, 6);
    perft_case(pos4, 2, 264);
    perft_case(pos4, 3, 9467);
    perft_case(pos5, 1, 44);
    perft_case(pos5, 2, 1486);
    perft_case(pos5, 3, 62379);
}

static void test_fen(void)
{
    const char *fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
                          "8/8/1R2q1pk/3p1p1p/3Pr2P/4PQP1/6K1/1R6 b - - 2 40"};
    char out[100];
    chess_pos p;
    size_t i;
    for (i = 0; i < sizeof fens / sizeof *fens; i++) {
        CHECK(chess_set_fen(&p, fens[i]) == 0, "fen %s", fens[i]);
        chess_get_fen(&p, out, sizeof out);
        CHECK(!strcmp(out, fens[i]), "round trip %s -> %s", fens[i], out);
    }
    CHECK(chess_set_fen(&p, "startpos") == 0, "startpos");
    CHECK(chess_set_fen(&p, "8/8/8/8/8/8/8/8 w - - 0 1") != 0, "no kings rejected");
    CHECK(chess_set_fen(&p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1") != 0, "7 ranks rejected");
    CHECK(chess_set_fen(&p, "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") != 0, "9 files rejected");
}

static void play(chess_pos *p, const char *uci, const char *want_san)
{
    chess_move m;
    char san[16], back[6];
    int ok = chess_parse_uci(p, uci, &m) == 0;
    CHECK(ok, "uci %s legal", uci);
    if (!ok) return;
    chess_move_san(p, m, san, sizeof san);
    CHECK(!strcmp(san, want_san), "san of %s = %s, want %s", uci, san, want_san);
    chess_move_uci(m, back);
    if (strlen(uci) == 4 && strcmp(uci, "e1h1") && strcmp(uci, "e8h8") && strcmp(uci, "e1a1") && strcmp(uci, "e8a8"))
        CHECK(!strcmp(back, uci), "uci round trip %s -> %s", uci, back);
    chess_make(p, m);
}

static void test_san_uci(void)
{
    chess_pos p;
    chess_move m;
    char out[100];
    chess_startpos(&p);
    play(&p, "e2e4", "e4");
    play(&p, "e7e5", "e5");
    play(&p, "g1f3", "Nf3");
    play(&p, "b8c6", "Nc6");
    play(&p, "f1c4", "Bc4");
    play(&p, "g8f6", "Nf6");
    play(&p, "e1h1", "O-O"); /* Lichess king-to-rook castling */
    play(&p, "f8c5", "Bc5");
    play(&p, "d2d4", "d4");
    play(&p, "e5d4", "exd4");
    chess_get_fen(&p, out, sizeof out);
    CHECK(!strcmp(out, "r1bqk2r/pppp1ppp/2n2n2/2b5/2BpP3/5N2/PPP2PPP/RNBQ1RK1 w kq - 0 6"), "fen after moves: %s", out);
    CHECK(chess_parse_uci(&p, "e1g1", &m) != 0, "castling twice refused");

    /* disambiguation, en passant, promotion and mate */
    chess_set_fen(&p, "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    play(&p, "a1d1", "Rd1"); /* the h1 rook is blocked by the king */
    chess_set_fen(&p, "4k3/8/8/8/8/8/8/R4RK1 w - - 0 1");
    play(&p, "a1d1", "Rad1");
    chess_set_fen(&p, "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    play(&p, "e1g1", "O-O");
    chess_set_fen(&p, "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    play(&p, "e1c1", "O-O-O");
    chess_set_fen(&p, "k7/8/8/8/1N3N2/8/8/K7 w - - 0 1");
    play(&p, "b4d5", "Nbd5");
    chess_set_fen(&p, "k7/8/8/1N6/8/1N6/8/K7 w - - 0 1");
    play(&p, "b5d4", "N5d4");
    chess_set_fen(&p, "k7/8/8/3pP3/8/8/8/K7 w - d6 0 2");
    play(&p, "e5d6", "exd6");
    CHECK(p.sq[CH_SQ(3, 4)] == 0, "en passant removed the pawn");
    chess_set_fen(&p, "k7/4P3/8/8/8/8/8/K7 w - - 0 1");
    play(&p, "e7e8q", "e8=Q+");
    chess_set_fen(&p, "k7/4P3/8/8/8/8/8/K7 w - - 0 1");
    play(&p, "e7e8n", "e8=N");
    chess_set_fen(&p, "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    play(&p, "a1a8", "Ra8#");
    CHECK(chess_status(&p) == CH_CHECKMATE, "back-rank mate");
    chess_set_fen(&p, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(chess_status(&p) == CH_STALEMATE, "stalemate");
    chess_set_fen(&p, "k7/8/8/8/8/8/8/K6r w - - 0 1");
    CHECK(chess_parse_uci(&p, "a1b1", &m) != 0, "king may not walk into check");
    CHECK(chess_parse_uci(&p, "a1a2", &m) == 0, "king may step out of check");
    CHECK(chess_parse_uci(&p, "zz", &m) != 0 && chess_parse_uci(&p, "e7e8x", &m) != 0, "bad uci refused");
}

static void test_json(void)
{
    const char *t = "{\"a\":1,\"b\":[true,false,null,\"x\\n\\u00e9\\ud83d\\ude00\"],\"c\":{\"d\":-2.5e1,\"e\":\"\"}}";
    jval *r = json_parse(t, strlen(t));
    const jval *b;
    CHECK(r != NULL, "json parses");
    if (!r) return;
    CHECK(json_num(r, "a", 0) == 1, "a");
    CHECK(json_num(r, "c.d", 0) == -25, "c.d");
    CHECK(json_str(r, "c.e", NULL) && !strcmp(json_str(r, "c.e", NULL), ""), "c.e empty");
    b = json_get(r, "b");
    CHECK(b && b->t == J_ARR && b->child && b->child->t == J_TRUE, "array");
    CHECK(b && b->child->next->next->next && !strcmp(b->child->next->next->next->str, "x\n\xc3\xa9\xf0\x9f\x98\x80"),
          "escapes and surrogate pairs");
    CHECK(json_str(r, "zz.y", "def") && !strcmp(json_str(r, "zz.y", "def"), "def"), "missing path default");
    json_free(r);
    CHECK(json_parse("{\"a\":}", 6) == NULL, "bad json refused");
    CHECK(json_parse("[1,2", 4) == NULL, "truncated refused");
    CHECK(json_parse("{} x", 4) == NULL, "trailing junk refused");
    {
        /* dense arrays must fit the pool */
        char big[4001];
        int i;
        jval *a;
        big[0] = '[';
        for (i = 0; i < 1999; i++) {
            big[1 + 2 * i] = '0';
            big[2 + 2 * i] = ',';
        }
        big[3998] = '0';
        big[3999] = ']';
        big[4000] = 0;
        a = json_parse(big, 4000);
        CHECK(a != NULL, "dense array parses");
        json_free(a);
    }
}

int main(void)
{
    test_perft();
    test_fen();
    test_san_uci();
    test_json();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
