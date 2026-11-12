/* Chess Online: host tests for the lessons (every step replays; arrows are legal moves for one side or the other). */
#include <stdio.h>
#include <string.h>

#include "chess.h"
#include "lessons.h"

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

/* an arrow is a plain line from a piece on its start square */
static int arrow_ok(const chess_pos *p, int from, int to)
{
    return from != to && p->sq[from] != 0;
}

int main(void)
{
    int i, s;
    CHECK(LESSON_COUNT == 5, "five lessons: %d", LESSON_COUNT);
    for (i = 0; i < LESSON_COUNT; i++) {
        const lesson *l = &LESSONS[i];
        CHECK(l->nsteps >= 4, "%s has %d steps", l->title, l->nsteps);
        CHECK(l->thumb_step >= 0 && l->thumb_step < l->nsteps, "%s thumbnail step", l->title);
        for (s = 0; s < l->nsteps; s++) {
            chess_pos p;
            chess_move last;
            int has_last, sq[16], n, k;
            CHECK(lesson_position(l, s, &p, &last, &has_last) == 0, "%s step %d replays", l->title, s + 1);
            n = lesson_squares(l->steps[s].arrows, sq, 16);
            CHECK(n >= 0 && n % 2 == 0, "%s step %d arrows parse", l->title, s + 1);
            for (k = 0; k + 1 < n; k += 2) CHECK(arrow_ok(&p, sq[k], sq[k + 1]), "%s step %d arrow %d starts on a piece", l->title, s + 1, k / 2);
            CHECK(lesson_squares(l->steps[s].marks, sq, 16) >= 0, "%s step %d marks parse", l->title, s + 1);
            CHECK(strlen(l->steps[s].text) > 20 && strlen(l->steps[s].text) < 240, "%s step %d text length", l->title, s + 1);
        }
    }
    /* the mates really are mates */
    {
        chess_pos p;
        chess_move m;
        int h;
        lesson_position(&LESSONS[4], 1, &p, &m, &h);
        CHECK(chess_status(&p) == CH_CHECKMATE, "back-rank mate");
        lesson_position(&LESSONS[4], 3, &p, &m, &h);
        CHECK(chess_status(&p) == CH_CHECKMATE, "queen mate");
        lesson_position(&LESSONS[4], 5, &p, &m, &h);
        CHECK(chess_status(&p) == CH_CHECKMATE, "the f7 mate");
        lesson_position(&LESSONS[3], 1, &p, &m, &h);
        CHECK(chess_in_check(&p, CH_BLACK), "the fork gives check");
        lesson_position(&LESSONS[1], 1, &p, &m, &h);
        CHECK(p.sq[CH_SQ(6, 0)] == CH_PIECE(CH_WHITE, CH_KING) && p.sq[CH_SQ(5, 0)] == CH_PIECE(CH_WHITE, CH_ROOK),
              "white castled");
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
