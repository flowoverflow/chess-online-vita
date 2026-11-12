/* Chess Online: host tests for the statistics (game lines, figures, texts). */
#include <stdio.h>
#include <string.h>

#include "gamelog.h"

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

static gl_game game(int mode, int param, int inc, int white, int result, int end, int moves, int secs)
{
    gl_game g;
    memset(&g, 0, sizeof g);
    g.year = 2026;
    g.month = 11;
    g.day = 11;
    g.hour = 21;
    g.minute = 40;
    g.mode = mode;
    if (mode == GL_CPU) g.level = param;
    else {
        g.minutes = param;
        g.increment = inc;
    }
    g.white = white;
    g.result = result;
    g.end = end;
    g.moves = moves;
    g.secs = secs;
    return g;
}

int main(void)
{
    char line[128], buf[96];
    gl_game g, h, list[12];
    gl_figures f;
    int n = 0;

    /* lines: written and read back */
    g = game(GL_CPU, 3, 0, 1, GL_WON, GL_END_MATE, 31, 1240);
    CHECK(gl_format(&g, line, sizeof line) > 0, "format");
    CHECK(!strcmp(line, "v1 2026-11-11 21:40 cpu 3 white won mate 31 1240"), "cpu line: %s", line);
    CHECK(gl_parse(line, &h) == 0 && !memcmp(&g, &h, sizeof g), "cpu line reads back");
    g = game(GL_RANDOM, 5, 8, 0, GL_LOST, GL_END_TIME, 52, 610);
    gl_format(&g, line, sizeof line);
    CHECK(!strcmp(line, "v1 2026-11-11 21:40 random 5+8 black lost time 52 610"), "random line: %s", line);
    CHECK(gl_parse(line, &h) == 0 && !memcmp(&g, &h, sizeof g), "random line reads back");
    g = game(GL_FRIEND, 10, 0, 1, GL_DRAWN, GL_END_REPETITION, 44, 900);
    gl_format(&g, line, sizeof line);
    CHECK(gl_parse(line, &h) == 0 && !memcmp(&g, &h, sizeof g), "friend line reads back: %s", line);
    CHECK(gl_format(&g, line, 10) == -1, "too small a buffer");

    /* lines that aren't games */
    CHECK(gl_parse("", &h) == -1, "empty");
    CHECK(gl_parse("v2 2026-11-11 21:40 cpu 3 white won mate 31 1240", &h) == -1, "another version");
    CHECK(gl_parse("v1 2026-11-11 21:40 cpu 9 white won mate 31 1240", &h) == -1, "level 9");
    CHECK(gl_parse("v1 2026-11-11 21:40 cpu 3 green won mate 31 1240", &h) == -1, "colour");
    CHECK(gl_parse("v1 2026-11-11 21:40 cpu 3 white won mate 31 1240 x", &h) == -1, "trailing text");
    CHECK(gl_parse("v1 2026-11-11 21:40 cpu 3+2 white won mate 31 1240", &h) == -1, "cpu with a time control");
    CHECK(gl_parse("v1 2026-11-11 21:40 random 5 white won mate 31 1240", &h) == -1, "online without increment");
    CHECK(gl_parse("v1 2026-13-11 21:40 cpu 3 white won mate 31 1240", &h) == -1, "month 13");
    CHECK(gl_parse("v1 2026-11-11 21:40 cpu 3 white won mate 31", &h) == -1, "cut short");
    CHECK(gl_parse("v1 2026-11-11 21:40 cpu 3 white won mate 31 1240\r", &h) == 0, "a CR at the end is fine");

    /* figures */
    list[n++] = game(GL_CPU, 1, 0, 1, GL_WON, GL_END_MATE, 20, 600);
    list[n++] = game(GL_CPU, 2, 0, 0, GL_WON, GL_END_MATE, 17, 500);
    list[n++] = game(GL_CPU, 3, 0, 1, GL_LOST, GL_END_RESIGN, 40, 900);
    list[n++] = game(GL_RANDOM, 5, 8, 0, GL_WON, GL_END_TIME, 52, 610);
    list[n++] = game(GL_RANDOM, 1, 0, 1, GL_LOST, GL_END_MATE, 30, 120);
    list[n++] = game(GL_FRIEND, 10, 5, 0, GL_DRAWN, GL_END_REPETITION, 74, 1500);
    list[n++] = game(GL_CPU, 3, 0, 1, GL_WON, GL_END_MATE, 35, 800);
    list[n++] = game(GL_FRIEND, 3, 2, 1, GL_WON, GL_END_LEFT, 8, 90);
    gl_compute(list, n, &f);
    CHECK(f.all.n == 8 && f.all.won == 5 && f.all.drawn == 1 && f.all.lost == 2, "all: %d %d %d %d", f.all.n, f.all.won,
          f.all.drawn, f.all.lost);
    CHECK(f.mode[GL_CPU].n == 4 && f.mode[GL_RANDOM].n == 2 && f.mode[GL_FRIEND].n == 2, "by mode");
    CHECK(f.level[2].n == 2 && f.level[2].won == 1 && f.level[2].lost == 1, "level 3");
    CHECK(f.color[0].n == 5 && f.color[1].n == 3, "by colour: %d %d", f.color[0].n, f.color[1].n);
    CHECK(f.speed[GL_BULLET].n == 1 && f.speed[GL_RAPID].n == 2 && f.speed[GL_BLITZ].n == 1, "by speed: %d %d %d %d",
          f.speed[0].n, f.speed[1].n, f.speed[2].n, f.speed[3].n);
    CHECK(f.ended[GL_END_TIME].won == 1 && f.ended[GL_END_MATE].lost == 1 && f.ended[GL_END_LEFT].won == 1, "online ends");
    CHECK(f.online_draws == 1, "online draws");
    CHECK(f.secs == 600 + 500 + 900 + 610 + 120 + 1500 + 800 + 90, "time played");
    CHECK(f.streak == 2 && f.best == 2, "streaks: now %d, best %d", f.streak, f.best);
    CHECK(f.fastest == 17 && f.longest == 74 && f.top == 3, "records: %d %d %d", f.fastest, f.longest, f.top);
    CHECK(gl_pct(&f.all) == 63, "63 %% won: %d", gl_pct(&f.all));
    gl_compute(list, 0, &f);
    CHECK(f.all.n == 0 && gl_pct(&f.all) == 0 && f.streak == 0, "no games");

    /* texts */
    gl_opponent_text(&list[0], buf, sizeof buf);
    CHECK(!strcmp(buf, "Computer, level 1"), "%s", buf);
    gl_opponent_text(&list[3], buf, sizeof buf);
    CHECK(!strcmp(buf, "Random opponent, 5+8"), "%s", buf);
    gl_how_text(&list[1], buf, sizeof buf);
    CHECK(!strcmp(buf, "Checkmate in 17 moves"), "%s", buf);
    gl_how_text(&list[2], buf, sizeof buf);
    CHECK(!strcmp(buf, "You resigned after 40 moves"), "%s", buf);
    gl_how_text(&list[3], buf, sizeof buf);
    CHECK(!strcmp(buf, "Won on time after 52 moves"), "%s", buf);
    g = list[0];
    gl_when_text(&g, 2026, 11, 11, 1, buf, sizeof buf);
    CHECK(!strcmp(buf, "Today 21:40"), "%s", buf);
    gl_when_text(&g, 2026, 11, 12, 1, buf, sizeof buf);
    CHECK(!strcmp(buf, "Yesterday 21:40"), "%s", buf);
    gl_when_text(&g, 2026, 12, 1, 0, buf, sizeof buf);
    CHECK(!strcmp(buf, "11 Nov"), "%s", buf);
    CHECK(gl_day_number(1970, 1, 1) == 0 && gl_day_number(2026, 3, 1) - gl_day_number(2026, 2, 28) == 1 &&
              gl_day_number(2024, 3, 1) - gl_day_number(2024, 2, 28) == 2,
          "day numbers");
    gl_played_text(36960, buf, sizeof buf);
    CHECK(!strcmp(buf, "10 h 16 min"), "%s", buf);
    gl_played_text(2460, buf, sizeof buf);
    CHECK(!strcmp(buf, "41 min"), "%s", buf);

    /* the result card's line */
    CHECK(gl_result_line(list, 3, buf, sizeof buf) == 0 && !strcmp(buf, "Level 3 so far: 0 won, 0 drawn, 1 lost"), "%s", buf);
    CHECK(gl_result_line(list, 7, buf, sizeof buf) == 1 && !strcmp(buf, "Your first win against level 3!"), "%s", buf);
    list[n] = list[6];
    CHECK(gl_result_line(list, n + 1, buf, sizeof buf) == 0 && !strcmp(buf, "Level 3 so far: 2 won, 0 drawn, 1 lost"),
          "%s", buf);
    CHECK(gl_result_line(list, 2, buf, sizeof buf) == 1 && !strcmp(buf, "Your first win against level 2!"), "%s", buf);
    CHECK(gl_result_line(list, 8, buf, sizeof buf) == 0 && !strcmp(buf, "Friend games so far: 1 won, 1 drawn, 0 lost"), "%s",
          buf);
    CHECK(gl_result_line(list, 5, buf, sizeof buf) == 0 && !strcmp(buf, "Random games so far: 1 won, 0 drawn, 1 lost"), "%s",
          buf);

    printf("gamelog: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
