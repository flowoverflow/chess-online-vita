/* Chess Online: host tests for the guest-mode protocol layer (cmg.c) and the SAN helpers it uses.
 * Message shapes follow the live PC check of 2026-11-10 (evidence/chessonline/cmg-check-*.log). */
#include <stdio.h>
#include <string.h>

#include "chess.h"
#include "cmg.h"

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

static cmg_msg M;
static cmg_game G;

static int parse(const char *s)
{
    return cmg_parse(s, strlen(s), &M);
}

static void test_san(void)
{
    chess_pos p;
    chess_move m;
    const char *line[] = {"e4", "d5", "exd5", "c6", "dxc6", "Nf6", "cxb7", "Nbd7", "bxa8=Q"};
    char fen[100];
    size_t i;
    chess_startpos(&p);
    for (i = 0; i < sizeof line / sizeof *line; i++) {
        CHECK(chess_parse_san(&p, line[i], &m) == 0, "san %s", line[i]);
        chess_make(&p, m);
    }
    chess_get_fen(&p, fen, sizeof fen);
    CHECK(chess_same_fen(fen, "Q1bqkb1r/p2npppp/5n2/8/8/8/PPPP1PPP/RNBQKBNR b KQk - 0 5"), "replayed fen %s", fen);
    CHECK(chess_parse_san(&p, "Nxe4", &m) != 0, "illegal san refused");
    chess_set_fen(&p, "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(chess_parse_san(&p, "O-O", &m) == 0 && m.to == 6, "O-O");
    CHECK(chess_parse_san(&p, "0-0-0", &m) == 0 && m.to == 2, "0-0-0 with zeros");
    CHECK(chess_same_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
                         "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"),
          "en passant written or omitted compares equal");
    CHECK(!chess_same_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
                          "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1"),
          "side to move differs");
}

static void test_lobby(void)
{
    CHECK(parse("{\"type\":\"open_games\",\"games\":[{\"id\":\"g1\",\"minutes\":5,\"increment\":8,\"level\":\"beginner\","
                "\"color\":\"random\",\"createdAt\":1794332800000},{\"id\":42,\"minutes\":10,\"increment\":0,"
                "\"level\":\"advanced\",\"color\":\"white\",\"createdAt\":1}]}") == 0,
          "open_games parses");
    CHECK(M.type == CMG_M_OPEN_GAMES && M.ngames == 2 && !strcmp(M.games[0].id, "g1") && M.games[0].increment == 8 &&
              !strcmp(M.games[1].id, "42") && !strcmp(M.games[1].level, "advanced"),
          "open_games fields");
    CHECK(parse("{\"type\":\"open_games\",\"games\":[]}") == 0 && M.ngames == 0, "empty lobby is fine");
    CHECK(parse("{\"type\":\"stats\",\"players\":2206,\"activeGames\":570,\"openGameSessions\":3}") == 0 &&
              M.type == CMG_M_STATS && M.players == 2206 && M.active_games == 570,
          "stats");
    CHECK(parse("{\"type\":\"private_created\",\"code\":\"E4NWVK\"}") == 0 && !strcmp(M.code, "E4NWVK"), "private code");
    CHECK(parse("{\"type\":\"open_created\",\"gameId\":\"77\"}") == 0 && M.type == CMG_M_OPEN_CREATED &&
              !strcmp(M.game_id, "77"),
          "open_created");
    CHECK(parse("{\"type\":\"error\",\"message\":\"Invalid code.\"}") == 0 && M.type == CMG_M_ERROR &&
              !strcmp(M.message, "Invalid code."),
          "error message");
    CHECK(parse("{\"type\":\"something_new\",\"x\":1}") == 0 && M.type == CMG_M_UNKNOWN, "unknown type tolerated");
    CHECK(parse("not json") == -1 && parse("[1,2]") == -1, "malformed refused");
}

static void test_game_flow(void)
{
    char buf[256];
    chess_move mv;
    cmg_game_init(&G);
    CHECK(parse("{\"type\":\"matched\",\"gameId\":\"1290723\",\"color\":\"white\",\"opponentId\":\"anon-j0ehvjit\","
                "\"reconnectToken\":\"abcdefghijklmnopqrstu\"}") == 0 &&
              M.type == CMG_M_MATCHED,
          "matched");
    cmg_game_matched(&G, &M);
    CHECK(!strcmp(G.game_id, "1290723") && G.my_color == CH_WHITE && !strcmp(G.token, "abcdefghijklmnopqrstu") &&
              strcmp(G.opponent_id, G.game_id),
          "matched kept game id, colour, opponent id and token apart");
    CHECK(!cmg_game_my_turn(&G), "no input before the first state");
    parse("{\"type\":\"state\",\"gameId\":\"1290723\",\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\","
          "\"moves\":[],\"clock\":{\"whiteMs\":300000,\"blackMs\":300000,\"incrementMs\":0,\"turn\":\"white\","
          "\"lastTs\":1794332871849},\"serverTime\":1794332871849}");
    CHECK(cmg_game_apply(&G, &M, 1000) == 1 && G.have_state && cmg_game_my_turn(&G), "state enables input");
    CHECK(cmg_game_clock(&G, CH_WHITE, 3000) == 298000, "running clock uses the server offset: %lld",
          (long long)cmg_game_clock(&G, CH_WHITE, 3000));
    CHECK(cmg_game_clock(&G, CH_BLACK, 3000) == 300000, "waiting side's clock stands still");

    chess_parse_uci(&G.pos, "e2e4", &mv);
    CHECK(cmg_build_move(buf, sizeof buf, G.game_id, mv) > 0 &&
              !strcmp(buf, "{\"type\":\"move\",\"gameId\":\"1290723\",\"from\":\"e2\",\"to\":\"e4\"}"),
          "move message: %s", buf);
    G.awaiting_move = 1;
    CHECK(!cmg_game_my_turn(&G), "no second move while one is pending");
    parse("{\"type\":\"error\",\"message\":\"Illegal move.\"}");
    cmg_game_apply(&G, &M, 1500);
    CHECK(!G.awaiting_move && cmg_game_my_turn(&G) && G.pos.sq[CH_SQ(4, 1)] == CH_PIECE(CH_WHITE, CH_PAWN),
          "a refused move clears the pending state, the server's board stays");

    parse("{\"type\":\"move\",\"gameId\":\"1290723\",\"from\":\"e2\",\"to\":\"e4\",\"san\":\"e4\",\"fen\":\"rnbqkbnr/"
          "pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\",\"moves\":[\"e4\"],\"clock\":{\"whiteMs\":294190,"
          "\"blackMs\":300000,\"incrementMs\":0,\"turn\":\"black\",\"lastTs\":1794332877659},\"serverTime\":1794332877659}");
    CHECK(cmg_game_apply(&G, &M, 7000) == 1 && G.nmoves == 1 && G.last_from == CH_SQ(4, 1) && G.last_to == CH_SQ(4, 3) &&
              G.pos.side == CH_BLACK && G.history_ok,
          "move applied");
    parse("{\"type\":\"move\",\"gameId\":\"999\",\"from\":\"e7\",\"to\":\"e5\",\"fen\":\"x\"}");
    CHECK(cmg_game_apply(&G, &M, 7100) == 0 && G.nmoves == 1, "another game's message ignored");

    /* promotion message and history replay after a resume */
    parse("{\"type\":\"state\",\"gameId\":\"1290723\",\"fen\":\"Q1bqkb1r/p2npppp/5n2/8/8/8/PPPP1PPP/RNBQKBNR b KQk - 0 5\","
          "\"moves\":[\"e4\",\"d5\",\"exd5\",\"c6\",\"dxc6\",\"Nf6\",\"cxb7\",\"Nbd7\",\"bxa8=Q\"],\"clock\":{\"whiteMs\":289571,"
          "\"blackMs\":295379,\"incrementMs\":0,\"turn\":\"black\",\"lastTs\":1794332886899},\"serverTime\":1794332886900}");
    CHECK(cmg_game_apply(&G, &M, 9000) == 1 && G.nmoves == 9 && G.history_ok && G.last_to == CH_SQ(0, 7) &&
              !strcmp(G.san[8], "bxa8=Q"),
          "resume state rebuilt the history and the last move");
    {
        chess_pos p;
        chess_set_fen(&p, "k7/4P3/8/8/8/8/8/K7 w - - 0 1");
        chess_parse_uci(&p, "e7e8q", &mv);
        CHECK(cmg_build_move(buf, sizeof buf, "1", mv) > 0 && strstr(buf, "\"promotion\":\"q\""), "promotion field: %s", buf);
        chess_parse_uci(&p, "e7e8n", &mv);
        CHECK(cmg_build_move(buf, sizeof buf, "1", mv) > 0 && strstr(buf, "\"promotion\":\"n\""), "knight promotion");
    }

    parse("{\"type\":\"opponent_disconnected\",\"graceMs\":45000}");
    CHECK(cmg_game_apply(&G, &M, 9500) == 1 && G.opponent_away && G.grace_ms == 45000 && !G.finished,
          "a disconnect is not a win");
    parse("{\"type\":\"opponent_reconnected\"}");
    cmg_game_apply(&G, &M, 9600);
    CHECK(!G.opponent_away, "reconnected");

    parse("{\"type\":\"resigned\",\"gameId\":\"1290723\",\"resignedColor\":\"white\",\"winner\":\"black\",\"message\":"
          "\"White Resigns - Black Wins!\",\"clock\":{\"whiteMs\":289571,\"blackMs\":295379,\"incrementMs\":0,\"turn\":"
          "\"black\",\"lastTs\":1794332886899},\"serverTime\":1794332888057}");
    CHECK(cmg_game_apply(&G, &M, 10000) == 1 && G.finished && G.winner == CH_BLACK &&
              !strcmp(G.result, "Black wins: White resigned") && G.end == CMG_END_RESIGN,
          "resigned: %s", G.result);
    /* black was to move: its clock stops at the resignation (1158 ms after lastTs by the server's time), not back at
     * the last move's 295379 */
    CHECK(!cmg_game_my_turn(&G) && cmg_game_clock(&G, CH_BLACK, 99999) == 294221 &&
              cmg_game_clock(&G, CH_BLACK, 10001) == 294221 && cmg_game_clock(&G, CH_WHITE, 99999) == 289571,
          "finished: no input, clocks frozen where they stood: %lld", (long long)cmg_game_clock(&G, CH_BLACK, 99999));
}

static void test_results(void)
{
    cmg_game_init(&G);
    parse("{\"type\":\"matched\",\"gameId\":\"5\",\"color\":\"black\",\"opponentId\":\"anon-x\",\"reconnectToken\":\"t\"}");
    cmg_game_matched(&G, &M);
    parse("{\"type\":\"move\",\"gameId\":\"5\",\"from\":\"d8\",\"to\":\"h4\",\"san\":\"Qh4#\",\"fen\":\"rnb1kbnr/pppp1ppp/8/"
          "4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3\",\"moves\":[\"f3\",\"e5\",\"g4\",\"Qh4#\"],\"gameOver\":{\"type\":"
          "\"checkmate\",\"winner\":\"black\"}}");
    CHECK(cmg_game_apply(&G, &M, 1) == 1 && G.finished && G.winner == CH_BLACK && G.history_ok && G.end == CMG_END_MATE,
          "checkmate via gameOver");
    cmg_game_matched(&G, &M);
    parse("{\"type\":\"timeout\",\"gameId\":\"5\",\"winner\":\"white\",\"clock\":{\"whiteMs\":1000,\"blackMs\":0,"
          "\"incrementMs\":0,\"turn\":\"black\",\"lastTs\":5}}");
    CHECK(cmg_game_apply(&G, &M, 1) == 1 && G.finished && G.winner == CH_WHITE && G.end == CMG_END_TIME, "timeout");
    cmg_game_matched(&G, &M);
    parse("{\"type\":\"opponent_left\",\"winner\":\"black\"}");
    CHECK(cmg_game_apply(&G, &M, 1) == 1 && G.finished && G.winner == CH_BLACK && G.end == CMG_END_LEFT, "opponent left");

    /* the opponent leaves on their own move: their clock keeps what it had when they left (40.5 s gone), it does not
     * jump back to the 3:50 of their last move */
    cmg_game_init(&G);
    parse("{\"type\":\"matched\",\"gameId\":\"6\",\"color\":\"white\",\"opponentId\":\"anon-y\",\"reconnectToken\":\"t\"}");
    cmg_game_matched(&G, &M);
    parse("{\"type\":\"state\",\"gameId\":\"6\",\"fen\":\"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\","
          "\"moves\":[\"e4\"],\"clock\":{\"whiteMs\":240000,\"blackMs\":230000,\"incrementMs\":8000,\"turn\":\"black\","
          "\"lastTs\":1000000},\"serverTime\":1000000}");
    cmg_game_apply(&G, &M, 500);
    parse("{\"type\":\"opponent_left\",\"winner\":\"white\"}");
    CHECK(cmg_game_apply(&G, &M, 41000) == 1 && G.finished && cmg_game_clock(&G, CH_BLACK, 99999) == 189500 &&
              cmg_game_clock(&G, CH_WHITE, 99999) == 240000,
          "opponent left: clocks stop where they stood: %lld", (long long)cmg_game_clock(&G, CH_BLACK, 99999));
}

static void test_builders(void)
{
    char b[160];
    CHECK(cmg_build_lobby_hello(b, sizeof b) > 0 && !strcmp(b, "{\"type\":\"lobby_hello\"}"), "lobby_hello");
    CHECK(cmg_build_create_open(b, sizeof b, 5, 8, "beginner", "random") > 0 &&
              !strcmp(b, "{\"type\":\"create_open\",\"minutes\":5,\"increment\":8,\"level\":\"beginner\",\"color\":\"random\"}"),
          "create_open: %s", b);
    CHECK(cmg_build_join_open(b, sizeof b, "abc") > 0 && !strcmp(b, "{\"type\":\"join_open\",\"gameId\":\"abc\"}"), "join");
    CHECK(cmg_build_cancel_open(b, sizeof b, "abc") > 0 && strstr(b, "cancel_open"), "cancel");
    CHECK(cmg_build_create_private(b, sizeof b, 5, 0, "white") > 0 &&
              !strcmp(b, "{\"type\":\"create_private\",\"minutes\":5,\"increment\":0,\"color\":\"white\"}"),
          "create_private");
    CHECK(cmg_build_resume(b, sizeof b, "tok\"en") > 0 && !strcmp(b, "{\"type\":\"resume\",\"token\":\"tok\\\"en\"}"),
          "resume escapes: %s", b);
    CHECK(cmg_build_response(b, sizeof b, "rematch_response", "9", 1) > 0 &&
              !strcmp(b, "{\"type\":\"rematch_response\",\"gameId\":\"9\",\"accept\":true}"),
          "response");
    CHECK(cmg_build_leave_beacon(b, sizeof b, "t1") > 0 && !strcmp(b, "{\"token\":\"t1\"}"), "leave beacon body");
    CHECK(cmg_build_join_open(b, 8, "much-too-long-id") == -1, "overflow refused");
}

int main(void)
{
    test_san();
    test_lobby();
    test_game_flow();
    test_results();
    test_builders();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
