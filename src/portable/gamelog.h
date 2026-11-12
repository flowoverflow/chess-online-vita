/* Chess Online: the statistics kept on the Vita. One text line per finished game (games.txt in the app's data
 * folder), and every figure worked out from those lines. Our own code (portable C99, host-tested). */
#ifndef CO_GAMELOG_H
#define CO_GAMELOG_H

#include <stddef.h>

enum { GL_CPU, GL_RANDOM, GL_FRIEND, GL_MODES };
enum { GL_WON, GL_DRAWN, GL_LOST };
enum {
    GL_END_MATE, GL_END_RESIGN, GL_END_TIME, GL_END_LEFT, /* the four ways a game is won or lost */
    GL_END_STALEMATE, GL_END_REPETITION, GL_END_FIFTY, GL_END_MATERIAL, GL_END_DRAW, GL_ENDS
};
enum { GL_BULLET, GL_BLITZ, GL_RAPID, GL_CLASSICAL, GL_SPEEDS };
#define GL_LEVELS 8

typedef struct {
    int year, month, day, hour, minute; /* local time the game ended */
    int mode;                           /* GL_CPU / GL_RANDOM / GL_FRIEND */
    int level;                          /* computer games: 1..GL_LEVELS */
    int minutes, increment;             /* online games' time control */
    int white;                          /* 1: you played white */
    int result;                         /* GL_WON / GL_DRAWN / GL_LOST */
    int end;                            /* GL_END_* */
    int moves;                          /* full moves */
    int secs;                           /* how long it took */
} gl_game;

/* One line without the newline: "v1 2026-11-11 21:40 cpu 3 white won mate 31 1240" or
 * "v1 2026-11-11 21:52 random 5+8 black lost time 52 610". Returns the length, or -1 when it doesn't fit. */
int gl_format(const gl_game *g, char *out, size_t n);
/* 0, or -1 for a line that isn't a game (the caller skips it) */
int gl_parse(const char *line, gl_game *g);

typedef struct {
    int n, won, drawn, lost;
} gl_wdl;

typedef struct {
    gl_wdl all, mode[GL_MODES], level[GL_LEVELS], color[2], speed[GL_SPEEDS]; /* color: 0 white, 1 black */
    gl_wdl ended[4]; /* online games won or lost by checkmate, resignation, on time, by leaving */
    int online_draws;
    long secs;
    int streak, best;     /* wins in a row now, and the most ever */
    int fastest, longest; /* fewest moves of a checkmate you gave, most moves of any game (0: none) */
    int top;              /* the strongest computer level beaten (0: none) */
} gl_figures;

void gl_compute(const gl_game *games, int n, gl_figures *f);
int gl_pct(const gl_wdl *w); /* % won, rounded */
int gl_speed(int minutes, int increment); /* GL_BULLET.., as the lobby names them */
const char *gl_speed_name(int speed);

/* texts for the list of games */
void gl_opponent_text(const gl_game *g, char *out, size_t n); /* "Computer, level 4", "Random opponent, 5+8" */
void gl_how_text(const gl_game *g, char *out, size_t n);      /* "Checkmate in 31 moves" */
/* "Today 21:40", "Yesterday 18:05", "9 Nov 21:09" relative to today (ty-tm-td); with_time 0: "today", "9 Nov" */
void gl_when_text(const gl_game *g, int ty, int tm, int td, int with_time, char *out, size_t n);
long gl_day_number(int y, int m, int d); /* days since 1970-01-01 */
void gl_played_text(long secs, char *out, size_t n); /* "10 h 16 min", "41 min" */
/* The result card's line for the game just added (the last of games[0..n-1]): "Level 3 so far: 3 won, 2 drawn,
 * 1 lost", or "Random games so far: ...". Returns 1 when it is the first win against that level (a highlight). */
int gl_result_line(const gl_game *games, int n, char *out, size_t len);

#endif
