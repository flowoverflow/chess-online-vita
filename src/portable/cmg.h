/* Chess Online guest mode: messages of Coolmath's chess server (wss://chess1.coolmathgames.com/ws) and a game model.
 * Our own code (portable C99, host-tested). The message shapes come from the live PC check of 2026-11-10
 * (evidence/chessonline/cmg-check-*.log) and the public client's behaviour; see docs/chessonline.md. */
#ifndef CO_CMG_H
#define CO_CMG_H

#include <stddef.h>
#include <stdint.h>

#include "chess.h"

#define CMG_MAX_OPEN 24
#define CMG_PLY_MAX 600
#define CMG_SAN 12

typedef enum {
    CMG_M_UNKNOWN = 0,
    CMG_M_OPEN_GAMES, CMG_M_STATS, CMG_M_OPEN_CREATED, CMG_M_OPEN_CANCELLED,
    CMG_M_PRIVATE_CREATED, CMG_M_PRIVATE_CANCELLED,
    CMG_M_MATCHED, CMG_M_STATE, CMG_M_MOVE,
    CMG_M_TIMEOUT, CMG_M_RESIGNED,
    CMG_M_OPP_DISCONNECTED, CMG_M_OPP_RECONNECTED, CMG_M_OPP_LEFT,
    CMG_M_TAKEBACK_SENT, CMG_M_TAKEBACK_RECEIVED, CMG_M_TAKEBACK_CANCELLED, CMG_M_TAKEBACK_DECLINED,
    CMG_M_TAKEBACK_APPLIED,
    CMG_M_REMATCH_SENT, CMG_M_REMATCH_RECEIVED, CMG_M_REMATCH_CANCELLED, CMG_M_REMATCH_DECLINED,
    CMG_M_ERROR
} cmg_type;

typedef struct {
    char id[24];
    int minutes, increment;
    char level[12]; /* beginner / medium / advanced */
    char color[8];  /* random / white / black: the creator's choice */
    int64_t created_at;
} cmg_open_game;

typedef struct {
    int valid;
    int64_t white_ms, black_ms, increment_ms, last_ts; /* last_ts and server_time are server epoch ms */
    int turn; /* CH_WHITE / CH_BLACK */
} cmg_clock;

typedef struct {
    cmg_type type;
    char type_name[32];
    int has_server_time;
    int64_t server_time;
    char game_id[24];
    /* lobby */
    cmg_open_game games[CMG_MAX_OPEN];
    int ngames, games_total;
    int players, active_games, open_sessions;
    char code[16]; /* private_created */
    /* matched */
    int color;
    char opponent_id[40];
    char token[96]; /* reconnect token: a credential, never logged */
    /* state / move / takeback_applied */
    char fen[100];
    int has_moves, nmoves;
    char moves[CMG_PLY_MAX][CMG_SAN]; /* SAN */
    char from[3], to[3], promotion;
    char san[CMG_SAN];
    cmg_clock clock;
    /* results */
    int game_over;
    char over_type[16]; /* checkmate / stalemate / draw */
    char winner[8], resigned_color[8];
    char message[160];
    int grace_ms;
} cmg_msg;

/* Parses one server message; returns 0, or -1 when it is not a JSON object. Unknown types give CMG_M_UNKNOWN.
 * The struct is large: keep it off small stacks. */
int cmg_parse(const char *text, size_t len, cmg_msg *m);

/* Client messages, written into out (JSON). Return the length, or -1 when it does not fit. */
int cmg_build_lobby_hello(char *out, size_t n);
int cmg_build_create_open(char *out, size_t n, int minutes, int increment, const char *level, const char *color);
int cmg_build_join_open(char *out, size_t n, const char *game_id);
int cmg_build_cancel_open(char *out, size_t n, const char *game_id);
int cmg_build_create_private(char *out, size_t n, int minutes, int increment, const char *color);
int cmg_build_join_private(char *out, size_t n, const char *code);
int cmg_build_resume(char *out, size_t n, const char *token);
int cmg_build_move(char *out, size_t n, const char *game_id, chess_move m);
int cmg_build_simple(char *out, size_t n, const char *type, const char *game_id); /* resign, leave_game, offers, cancels */
int cmg_build_response(char *out, size_t n, const char *type, const char *game_id, int accept);
int cmg_build_leave_beacon(char *out, size_t n, const char *token); /* body for POST /api/leave_game */

/* ---- game model ---- */
enum { CMG_END_MATE, CMG_END_RESIGN, CMG_END_TIME, CMG_END_LEFT, CMG_END_STALEMATE, CMG_END_DRAW };

typedef struct {
    char game_id[24];
    int my_color;
    char opponent_id[40];
    char token[96];
    int have_state;
    chess_pos pos;            /* the server's position */
    int nmoves;
    char san[CMG_PLY_MAX][CMG_SAN];
    int last_from, last_to;   /* -1 when unknown */
    cmg_clock clock;
    int64_t offset_samples[8];
    int noffsets;
    int64_t offset;           /* server time minus local ms (largest recent sample) */
    int awaiting_move;
    int finished;
    int winner;               /* CH_WHITE / CH_BLACK / -1 for a draw or none */
    int end;                  /* how it ended: CMG_END_* (for the statistics) */
    char result[96];
    int opponent_away, grace_ms;
    int takeback_in, takeback_out, rematch_in, rematch_out;
    int history_ok;           /* the SAN list replayed to the server's position */
} cmg_game;

void cmg_game_init(cmg_game *g);
/* Starts a game from a matched message (keeps the token). */
void cmg_game_matched(cmg_game *g, const cmg_msg *m);
void cmg_note_server_time(cmg_game *g, int64_t server_time, uint64_t local_ms);
/* Applies state / move / takeback_applied / results / opponent and offer messages for this game.
 * Returns 1 when it changed the game, 0 when it was for another game or not a game message, -1 when a
 * position from the server could not be read (the game is then not playable until the next state). */
int cmg_game_apply(cmg_game *g, const cmg_msg *m, uint64_t local_ms);
int64_t cmg_game_clock(const cmg_game *g, int color, uint64_t local_ms);
int cmg_game_my_turn(const cmg_game *g);

#endif
