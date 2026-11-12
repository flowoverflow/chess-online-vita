/* Chess Online: the statistics kept on the Vita. Our own code; see gamelog.h. */
#include "gamelog.h"

#include <stdio.h>
#include <string.h>

static const char *MODE_NAMES[GL_MODES] = {"cpu", "random", "friend"};
static const char *RESULT_NAMES[3] = {"won", "drawn", "lost"};
static const char *END_NAMES[GL_ENDS] = {"mate", "resign", "time", "left", "stalemate", "repetition", "fifty",
                                         "material", "draw"};
static const char *SPEED_NAMES[GL_SPEEDS] = {"Bullet", "Blitz", "Rapid", "Classical"};
static const char *MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int find(const char *const *names, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(names[i], s)) return i;
    return -1;
}

int gl_format(const gl_game *g, char *out, size_t n)
{
    char param[16];
    int len;
    if (g->mode < 0 || g->mode >= GL_MODES || g->result < 0 || g->result > 2 || g->end < 0 || g->end >= GL_ENDS) return -1;
    if (g->mode == GL_CPU) snprintf(param, sizeof param, "%d", g->level);
    else snprintf(param, sizeof param, "%d+%d", g->minutes, g->increment);
    len = snprintf(out, n, "v1 %04d-%02d-%02d %02d:%02d %s %s %s %s %s %d %d", g->year, g->month, g->day, g->hour,
                   g->minute, MODE_NAMES[g->mode], param, g->white ? "white" : "black", RESULT_NAMES[g->result],
                   END_NAMES[g->end], g->moves, g->secs);
    return len < 0 || (size_t)len >= n ? -1 : len;
}

int gl_parse(const char *line, gl_game *g)
{
    char mode[16], param[16], color[8], result[8], end[16], tail[2];
    memset(g, 0, sizeof *g);
    if (sscanf(line, "v1 %d-%d-%d %d:%d %15s %15s %7s %7s %15s %d %d %1s", &g->year, &g->month, &g->day, &g->hour,
               &g->minute, mode, param, color, result, end, &g->moves, &g->secs, tail) != 12)
        return -1;
    if (g->month < 1 || g->month > 12 || g->day < 1 || g->day > 31 || g->hour < 0 || g->hour > 23 || g->minute < 0 ||
        g->minute > 59 || g->moves < 0 || g->secs < 0)
        return -1;
    g->mode = find(MODE_NAMES, GL_MODES, mode);
    g->result = find(RESULT_NAMES, 3, result);
    g->end = find(END_NAMES, GL_ENDS, end);
    if (g->mode < 0 || g->result < 0 || g->end < 0) return -1;
    if (!strcmp(color, "white")) g->white = 1;
    else if (strcmp(color, "black")) return -1;
    if (g->mode == GL_CPU) {
        if (sscanf(param, "%d%1s", &g->level, tail) != 1 || g->level < 1 || g->level > GL_LEVELS) return -1;
    } else if (sscanf(param, "%d+%d%1s", &g->minutes, &g->increment, tail) != 2 || g->minutes < 0 || g->increment < 0) {
        return -1;
    }
    return 0;
}

static void add(gl_wdl *w, int result)
{
    w->n++;
    if (result == GL_WON) w->won++;
    else if (result == GL_DRAWN) w->drawn++;
    else w->lost++;
}

int gl_speed(int minutes, int increment)
{
    int t = minutes * 60 + increment * 40;
    return t < 180 ? GL_BULLET : t < 480 ? GL_BLITZ : t < 1500 ? GL_RAPID : GL_CLASSICAL;
}

const char *gl_speed_name(int speed)
{
    return speed >= 0 && speed < GL_SPEEDS ? SPEED_NAMES[speed] : "";
}

void gl_compute(const gl_game *games, int n, gl_figures *f)
{
    int i, run = 0;
    memset(f, 0, sizeof *f);
    for (i = 0; i < n; i++) {
        const gl_game *g = &games[i];
        add(&f->all, g->result);
        add(&f->mode[g->mode], g->result);
        add(&f->color[g->white ? 0 : 1], g->result);
        if (g->mode == GL_CPU) {
            add(&f->level[g->level - 1], g->result);
            if (g->result == GL_WON && g->level > f->top) f->top = g->level;
        } else {
            add(&f->speed[gl_speed(g->minutes, g->increment)], g->result);
            if (g->result == GL_DRAWN) f->online_draws++;
            else if (g->end <= GL_END_LEFT) add(&f->ended[g->end], g->result);
        }
        f->secs += g->secs;
        run = g->result == GL_WON ? run + 1 : 0;
        if (run > f->best) f->best = run;
        if (g->result == GL_WON && g->end == GL_END_MATE && (!f->fastest || g->moves < f->fastest)) f->fastest = g->moves;
        if (g->moves > f->longest) f->longest = g->moves;
    }
    f->streak = run;
}

int gl_pct(const gl_wdl *w)
{
    return w->n ? (200 * w->won + w->n) / (2 * w->n) : 0;
}

void gl_opponent_text(const gl_game *g, char *out, size_t n)
{
    if (g->mode == GL_CPU) snprintf(out, n, "Computer, level %d", g->level);
    else snprintf(out, n, "%s, %d+%d", g->mode == GL_FRIEND ? "A friend" : "Random opponent", g->minutes, g->increment);
}

void gl_how_text(const gl_game *g, char *out, size_t n)
{
    int m = g->moves, w = g->result == GL_WON;
    const char *one = m == 1 ? "move" : "moves";
    switch (g->end) {
    case GL_END_MATE:
        if (w) snprintf(out, n, "Checkmate in %d %s", m, one);
        else snprintf(out, n, "Checkmated after %d %s", m, one);
        break;
    case GL_END_RESIGN: snprintf(out, n, "%s resigned after %d %s", w ? "They" : "You", m, one); break;
    case GL_END_TIME: snprintf(out, n, "%s on time after %d %s", w ? "Won" : "Lost", m, one); break;
    case GL_END_LEFT: snprintf(out, n, "%s left after %d %s", w ? "Opponent" : "You", m, one); break;
    case GL_END_STALEMATE: snprintf(out, n, "Stalemate after %d %s", m, one); break;
    case GL_END_REPETITION: snprintf(out, n, "Draw by repetition, %d %s", m, one); break;
    case GL_END_FIFTY: snprintf(out, n, "Draw by the 50-move rule, %d %s", m, one); break;
    case GL_END_MATERIAL: snprintf(out, n, "Draw: not enough pieces, %d %s", m, one); break;
    default: snprintf(out, n, "Draw after %d %s", m, one); break;
    }
}

/* days from civil date (the well-known proleptic Gregorian formula) */
long gl_day_number(int y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void gl_when_text(const gl_game *g, int ty, int tm, int td, int with_time, char *out, size_t n)
{
    long dd = gl_day_number(ty, tm, td) - gl_day_number(g->year, g->month, g->day);
    char day[16];
    if (dd == 0) snprintf(day, sizeof day, with_time ? "Today" : "today");
    else if (dd == 1) snprintf(day, sizeof day, with_time ? "Yesterday" : "yesterday");
    else snprintf(day, sizeof day, "%d %s", g->day, MONTHS[(g->month - 1) % 12]);
    if (with_time) snprintf(out, n, "%s %02d:%02d", day, g->hour, g->minute);
    else snprintf(out, n, "%s", day);
}

void gl_played_text(long secs, char *out, size_t n)
{
    long h = secs / 3600, m = secs % 3600 / 60;
    if (h) snprintf(out, n, "%ld h %ld min", h, m);
    else snprintf(out, n, "%ld min", m);
}

int gl_result_line(const gl_game *games, int n, char *out, size_t len)
{
    const gl_game *last;
    gl_wdl w;
    int i;
    out[0] = 0;
    if (n <= 0) return 0;
    last = &games[n - 1];
    memset(&w, 0, sizeof w);
    for (i = 0; i < n; i++)
        if (games[i].mode == last->mode && (last->mode != GL_CPU || games[i].level == last->level))
            add(&w, games[i].result);
    if (last->mode == GL_CPU && last->result == GL_WON && w.won == 1) {
        snprintf(out, len, "Your first win against level %d!", last->level);
        return 1;
    }
    if (last->mode == GL_CPU) snprintf(out, len, "Level %d so far: %d won, %d drawn, %d lost", last->level, w.won, w.drawn, w.lost);
    else snprintf(out, len, "%s games so far: %d won, %d drawn, %d lost", last->mode == GL_FRIEND ? "Friend" : "Random",
                  w.won, w.drawn, w.lost);
    return 0;
}
