/* Chess Online: "Chess tips", short lessons played out on the app's own board. Our own text and positions. */
#ifndef CO_LESSONS_H
#define CO_LESSONS_H

#include "chess.h"

typedef struct {
    const char *fen;    /* start this step from a new position (NULL: continue from the previous step) */
    const char *moves;  /* SAN moves played before the step is shown, space separated (may be "") */
    const char *arrows; /* "g1f3 b1c3": arrows from-to */
    const char *marks;  /* "f7 e5": highlighted squares */
    const char *text;
} lesson_step;

typedef struct {
    const char *title;
    const char *blurb;  /* one line for the gallery */
    int icon;           /* CH_PIECE value for the thumbnail */
    int thumb_step;     /* the step drawn on the thumbnail */
    const lesson_step *steps;
    int nsteps;
} lesson;

extern const lesson LESSONS[];
extern const int LESSON_COUNT;

/* The position shown at a step and its last move (*has_last 0 when the step starts from a FEN with no moves).
 * Returns 0, or -1 if the lesson data doesn't replay (caught by the host tests). */
int lesson_position(const lesson *l, int step, chess_pos *out, chess_move *last, int *has_last);
/* Parses "e2e4 g1f3" / "f7 e5" into squares: returns the count (pairs for arrows). */
int lesson_squares(const char *s, int *out, int max);

#endif
