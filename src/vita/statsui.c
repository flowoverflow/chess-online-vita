/* Chess Online: your statistics on the Vita. Our own code; see statsui.h. Coordinates are the mock-ups' (layout B,
 * experiments/chessonline/mockups/stats.js). */
#include "statsui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess.h"
#include "localgame.h"
#include "ui.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LIST_ROWS 8
#define LIST_Y 150
#define LIST_STEP 45

static struct {
    gl_game *games;
    int n, cap;
    gl_figures f;
    int tab, top, confirm, confirm_sel;
} T;

static const unsigned WDL_C[3] = {C_GREEN, (unsigned)RGBA8(128, 146, 178, 255), C_RED};  /* won, drawn, lost */
static const unsigned WDL_T[3] = {(unsigned)RGBA8(110, 214, 132, 255), (unsigned)RGBA8(178, 194, 220, 255),
                                  (unsigned)RGBA8(255, 122, 122, 255)}; /* the same as text on the dark panels */
static const char *TABS[4] = {"Overview", "Vs computer", "Online", "Recent games"};

/* ---------- the games file ---------- */
static int push(const gl_game *g)
{
    if (T.n == T.cap) {
        int cap = T.cap ? T.cap * 2 : 64;
        gl_game *p = (gl_game *)realloc(T.games, sizeof *p * (size_t)cap);
        if (!p) return -1;
        T.games = p;
        T.cap = cap;
    }
    T.games[T.n++] = *g;
    return 0;
}

static void refigure(void)
{
    gl_compute(T.games, T.n, &T.f);
}

void stats_load(void)
{
    size_t len = 0;
    char *s = plat_read_file(CO_GAMES_FILE, &len), *line, *save = NULL;
    int skipped = 0;
    T.n = 0;
    if (s) {
        for (line = strtok_r(s, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            gl_game g;
            if (gl_parse(line, &g) || push(&g)) skipped++;
        }
        free(s);
    }
    refigure();
    plat_log("stats: %d games loaded from %s (%d lines skipped)", T.n, CO_GAMES_FILE, skipped);
}

unsigned stats_record(gl_game *g, char *line, size_t n)
{
    char text[128];
    int len, first;
    plat_local_time(&g->year, &g->month, &g->day, &g->hour, &g->minute);
    line[0] = 0;
    len = gl_format(g, text, sizeof text - 1);
    if (len < 0 || push(g)) {
        plat_log("stats: couldn't record a game");
        return C_INK_SOFT;
    }
    refigure();
    plat_log("stats: recorded %s", text);
    text[len++] = '\n';
    if (plat_append_file(CO_GAMES_FILE, text, (size_t)len)) plat_log("stats: couldn't write %s", CO_GAMES_FILE);
    first = gl_result_line(T.games, T.n, line, n);
    return first ? C_CYAN_DARK : C_INK_SOFT;
}

static void rewrite(void)
{
    char *buf = (char *)malloc((size_t)T.n * 96 + 1);
    size_t k = 0;
    int i;
    if (!buf) return;
    for (i = 0; i < T.n; i++) {
        int len = gl_format(&T.games[i], buf + k, 95);
        if (len < 0) continue;
        k += (size_t)len;
        buf[k++] = '\n';
    }
    if (plat_write_file(CO_GAMES_FILE, buf, k)) plat_log("stats: couldn't rewrite %s", CO_GAMES_FILE);
    free(buf);
}

void stats_unrecord_last(void)
{
    if (!T.n) return;
    T.n--;
    refigure();
    rewrite();
    plat_log("stats: the last game was taken back; %d games kept", T.n);
}

static void clear_all(void)
{
    /* the file is put aside (replacing the last one put aside), not deleted */
    if (plat_rename_file(CO_GAMES_FILE, CO_GAMES_CLEARED_FILE)) plat_log("stats: couldn't move %s aside", CO_GAMES_FILE);
    plat_log("stats: %d games cleared (kept in %s)", T.n, CO_GAMES_CLEARED_FILE);
    T.n = 0;
    T.top = 0;
    refigure();
}

/* ---------- drawing helpers ---------- */
static int part(const gl_wdl *w, int i)
{
    return i == 0 ? w->won : i == 1 ? w->drawn : w->lost;
}

static void icon(int piece, float cx, float cy, float r)
{
    float ps = roundf(r * 1.48f);
    ui_circle(cx, cy, r, C_WHITE);
    ui_piece_scaled(piece, cx - ps / 2, cy - ps / 2, ps);
}

/* won / drawn / lost as one rounded bar */
static void bar(float x, float y, float w, float h, const gl_wdl *o)
{
    float cx = x;
    int i;
    if (!o->n) {
        ui_round(x, y, w, h, h / 2, ui_alpha(C_WHITE, 26));
        return;
    }
    for (i = 0; i < 3; i++) {
        int k = part(o, i);
        float sw;
        if (!k) continue;
        sw = w * (float)k / (float)o->n;
        ui_round_band(x, y, w, h, h / 2, cx, cx + sw + 0.5f, WDL_C[i]);
        cx += sw;
    }
}

/* the same as a ring, clockwise from the top */
static void ring(float cx, float cy, float r, float th, const gl_wdl *o)
{
    float a = -(float)M_PI / 2;
    int i;
    if (!o->n) {
        ui_ring(cx, cy, r, th, ui_alpha(C_WHITE, 26));
        return;
    }
    for (i = 0; i < 3; i++) {
        int k = part(o, i);
        float b;
        if (!k) continue;
        b = a + 2 * (float)M_PI * (float)k / (float)o->n;
        ui_arc(cx, cy, r, th, a, b + 0.01f, WDL_C[i]);
        a = b;
    }
}

/* "18 won  3 drawn  10 lost" in their colours; right > 0 right-aligns it there. When the words are wider than
 * max_w (> 0), as they are in most of the other languages, the numbers alone follow a dot in each colour (0.7.0). */
static void counts(int x, int y, int size, const gl_wdl *o, int right, int max_w)
{
    char p[3][48];
    int i, gap = (int)(size * 0.85f + 0.5f), w = 2 * gap;
    snprintf(p[0], sizeof p[0], "%d won", o->won);
    snprintf(p[1], sizeof p[1], "%d drawn", o->drawn);
    snprintf(p[2], sizeof p[2], "%d lost", o->lost);
    for (i = 0; i < 3; i++) w += ui_text_w(F_BOLD, size, p[i]);
    if (max_w > 0 && w > max_w) {
        int r = (int)(size * 0.3f + 0.5f), dgap = (int)(size * 0.35f + 0.5f);
        w = 2 * gap;
        for (i = 0; i < 3; i++) {
            snprintf(p[i], sizeof p[i], "%d", part(o, i));
            w += 2 * r + dgap + ui_text_w_raw(F_BOLD, size, p[i]);
        }
        if (right > 0) x = right - w;
        for (i = 0; i < 3; i++) {
            ui_circle((float)(x + r), (float)y - (int)(size * 0.35f + 0.5f), (float)r, WDL_C[i]);
            x += 2 * r + dgap;
            x += ui_text_raw(F_BOLD, x, y, WDL_T[i], size, p[i]) + gap;
        }
        return;
    }
    if (right > 0) x = right - w;
    for (i = 0; i < 3; i++) x += ui_text(F_BOLD, x, y, WDL_T[i], size, p[i]) + gap;
}

static void sub_text(const gl_wdl *o, char *out, size_t n)
{
    if (o->n) snprintf(out, n, "%d game%s, %d%% won", o->n, o->n == 1 ? "" : "s", gl_pct(o));
    else snprintf(out, n, "No games yet");
}

static void num(int font, int rx, int y, unsigned c, int size, int v)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%d", v);
    ui_text_right(font, rx, y, c, size, buf);
}

static void shoulder(float x, float y, const char *s)
{
    ui_round(x, y, 34, 22, 7, ui_alpha(C_WHITE, 40));
    ui_text_center(F_XBOLD, (int)x + 17, (int)y + 16, C_WHITE, 14, s);
}

static void tab_geo(int i, int *x, int *w)
{
    int k, xx = 92;
    for (k = 0; k < i; k++) xx += ui_text_w(F_BOLD, 17, TABS[k]) + 40 + 8;
    *x = xx;
    *w = ui_text_w(F_BOLD, 17, TABS[i]) + 40;
}

static void draw_empty(float y0)
{
    static const int pieces[3] = {CH_PIECE(CH_WHITE, CH_KNIGHT), CH_PIECE(CH_BLACK, CH_QUEEN), CH_PIECE(CH_WHITE, CH_KING)};
    float cy = y0 + (508 - y0) / 2 - 40;
    int i;
    ui_round(48, y0, 864, 508 - y0, 18, C_PANEL);
    for (i = 0; i < 3; i++) icon(pieces[i], 420 + (float)i * 60, cy - 20, 24);
    ui_text_center(F_XBOLD, 480, (int)cy + 50, C_WHITE, 26, "No games yet");
    ui_text_center(F_REG, 480, (int)cy + 82, C_ON_DARK_SOFT, 18, "Finish a game and its result is kept here, on this Vita.");
    ui_text_center(F_REG, 480, (int)cy + 108, C_ON_DARK_SOFT, 16,
                   "Games vs the computer, random opponents and friends all count.");
}

/* ---------- the four tabs ---------- */
static void draw_overview(void)
{
    const gl_figures *f = &T.f;
    char buf[48], parts[3][16];
    int i, tw = 40, x;
    ui_round(48, 150, 400, 362, 18, C_PANEL);
    ui_text(F_XBOLD, 68, 186, C_WHITE, 20, "All games");
    snprintf(buf, sizeof buf, "%d played", f->all.n);
    ui_text_right(F_REG, 428, 186, C_ON_DARK_SOFT, 16, buf);
    ring(248, 310, 96, 26, &f->all);
    snprintf(buf, sizeof buf, "%d%%", gl_pct(&f->all));
    ui_text_center(F_XBOLD, 248, 318, C_WHITE, 46, buf);
    ui_text_center(F_REG, 248, 344, C_ON_DARK_SOFT, 17, "won");
    snprintf(parts[0], sizeof parts[0], "%d won", f->all.won);
    snprintf(parts[1], sizeof parts[1], "%d drawn", f->all.drawn);
    snprintf(parts[2], sizeof parts[2], "%d lost", f->all.lost);
    for (i = 0; i < 3; i++) tw += 18 + ui_text_w(F_BOLD, 17, parts[i]);
    x = 248 - tw / 2;
    for (i = 0; i < 3; i++) {
        ui_circle((float)x + 6, 466, 6, WDL_C[i]);
        x += 18 + ui_text(F_BOLD, x + 18, 472, C_WHITE, 17, parts[i]) + 20;
    }

    ui_round(464, 150, 448, 362, 18, C_PANEL);
    ui_text(F_XBOLD, 484, 186, C_WHITE, 20, "By colour");
    for (i = 0; i < 2; i++) {
        int y0 = 202 + i * 62;
        const gl_wdl *o = &f->color[i];
        ui_circle(506, (float)y0 + 26, 22, RGBA8(226, 234, 246, 255));
        ui_piece_scaled(CH_PIECE(i ? CH_BLACK : CH_WHITE, CH_KING), 506 - 17, (float)y0 + 9, 34);
        ui_text(F_BOLD, 540, y0 + 22, C_WHITE, 18, i ? "As black" : "As white");
        sub_text(o, buf, sizeof buf);
        ui_text_fit(F_REG, 541, y0 + 44, C_ON_DARK_SOFT, 15, 172, buf); /* 6 px clear of the counts at 720 */
        bar(720, (float)y0 + 12, 172, 12, o);
        counts(720, y0 + 44, 14, o, 0, 176);
    }
    for (i = 0; i < 4; i++) {
        static const char *labels[4] = {"Win streak now", "Best win streak", "Time played", "Fastest checkmate"};
        float x0 = 484 + (float)(i % 2) * 208, y0 = 338 + (float)(i / 2) * 84;
        if (i == 0) snprintf(buf, sizeof buf, "%d", f->streak);
        else if (i == 1) snprintf(buf, sizeof buf, "%d", f->best);
        else if (i == 2) gl_played_text(f->secs, buf, sizeof buf);
        else if (f->fastest) snprintf(buf, sizeof buf, "%d moves", f->fastest);
        else snprintf(buf, sizeof buf, "-");
        ui_round(x0, y0, 200, 72, 14, C_PANEL_HI);
        ui_text(F_XBOLD, (int)x0 + 16, (int)y0 + 34, C_WHITE, 24, buf);
        ui_text(F_REG, (int)x0 + 16, (int)y0 + 58, C_ON_DARK_SOFT, 15, labels[i]);
    }
}

static void draw_levels(void)
{
    static const int COLS[4] = {488, 560, 636, 700}; /* right edges: played, won, drawn, lost */
    static const char *HEADS[4] = {"Played", "Won", "Drawn", "Lost"};
    const gl_figures *f = &T.f;
    char buf[96];
    int i, j;
    ui_round(48, 150, 864, 362, 18, C_PANEL);
    ui_text(F_REG, 72, 180, C_ON_DARK_SOFT, 14, "Level");
    for (j = 0; j < 4; j++) ui_text_right(F_REG, COLS[j], 180, C_ON_DARK_SOFT, 14, HEADS[j]);
    for (i = 0; i < GL_LEVELS; i++) {
        int y = 192 + i * 36;
        const gl_wdl *o = &f->level[i];
        if (i % 2 == 0) ui_round(60, (float)y, 840, 32, 10, ui_alpha(C_WHITE, 10));
        ui_round(72, (float)y + 4, 36, 24, 12, ui_level_tint(i));
        snprintf(buf, sizeof buf, "%d", i + 1);
        ui_text_center(F_XBOLD, 90, y + 21, C_WHITE, 15, buf);
        ui_text(F_REG, 120, y + 22, o->n ? C_WHITE : C_ON_DARK_SOFT, 16, local_level_about(i + 1));
        if (!o->n) {
            ui_text_right(F_REG, 488, y + 22, C_ON_DARK_SOFT, 15, "Not played yet");
            continue;
        }
        num(F_BOLD, COLS[0], y + 22, C_WHITE, 16, o->n);
        for (j = 0; j < 3; j++) num(F_BOLD, COLS[j + 1], y + 22, part(o, j) ? WDL_T[j] : C_ON_DARK_SOFT, 16, part(o, j));
        bar(724, (float)y + 10, 160, 12, o);
    }
    if (f->top) snprintf(buf, sizeof buf, "Strongest level beaten: level %d, \"%s\"", f->top, local_level_about(f->top));
    else snprintf(buf, sizeof buf, "No wins against the computer yet");
    ui_text(F_BOLD, 72, 500, C_ON_DARK_SOFT, 16, buf);
}

static void draw_online(void)
{
    static const int ROW_PIECE[2] = {CH_PIECE(CH_WHITE, CH_KNIGHT), CH_PIECE(CH_WHITE, CH_KING)};
    static const char *ROW_NAME[2] = {"Random opponent", "Play a friend"};
    static const char *ENDED[4] = {"Checkmate", "Resignation", "On time", "Left the game"};
    static const char *HEADS[3] = {"Won", "Drawn", "Lost"};
    const gl_figures *f = &T.f;
    char buf[48];
    int i, j;
    for (i = 0; i < 2; i++) {
        int x = 48 + i * 440;
        const gl_wdl *o = &f->mode[i ? GL_FRIEND : GL_RANDOM];
        ui_round((float)x, 150, 424, 120, 18, C_PANEL);
        icon(ROW_PIECE[i], (float)x + 44, 194, 24);
        ui_text(F_XBOLD, x + 80, 188, C_WHITE, 20, ROW_NAME[i]);
        sub_text(o, buf, sizeof buf);
        ui_text(F_REG, x + 81, 210, C_ON_DARK_SOFT, 15, buf);
        bar((float)x + 24, 228, 376, 12, o);
        counts(x + 24, 258, 15, o, 0, 380);
    }
    ui_round(48, 286, 424, 226, 18, C_PANEL);
    ui_text(F_XBOLD, 68, 318, C_WHITE, 19, "By speed");
    ui_text_right(F_REG, 264, 318, C_ON_DARK_SOFT, 13, "Games");
    for (j = 0; j < 3; j++) ui_text_right(F_REG, 324 + j * 64, 318, C_ON_DARK_SOFT, 13, HEADS[j]);
    for (i = 0; i < GL_SPEEDS; i++) {
        int y = 352 + i * 38;
        const gl_wdl *o = &f->speed[i];
        ui_text(F_BOLD, 68, y, C_WHITE, 17, gl_speed_name(i));
        num(F_BOLD, 264, y, o->n ? C_WHITE : C_ON_DARK_SOFT, 17, o->n);
        for (j = 0; j < 3; j++) num(F_BOLD, 324 + j * 64, y, part(o, j) ? WDL_T[j] : C_ON_DARK_SOFT, 17, part(o, j));
    }
    ui_round(488, 286, 424, 226, 18, C_PANEL);
    ui_text(F_XBOLD, 508, 318, C_WHITE, 19, "How games ended");
    for (j = 0; j < 3; j++) ui_text_right(F_REG, 764 + j * 64, 318, C_ON_DARK_SOFT, 13, HEADS[j]);
    for (i = 0; i < 4; i++) {
        int y = 352 + i * 34;
        const gl_wdl *o = &f->ended[i];
        ui_text(F_BOLD, 508, y, C_WHITE, 17, ENDED[i]);
        num(F_BOLD, 764, y, o->won ? WDL_T[0] : C_ON_DARK_SOFT, 17, o->won);
        ui_text_right(F_BOLD, 828, y, C_ON_DARK_SOFT, 17, "-");
        num(F_BOLD, 892, y, o->lost ? WDL_T[2] : C_ON_DARK_SOFT, 17, o->lost);
    }
    {
        int y = 352 + 4 * 34;
        ui_text(F_BOLD, 508, y, C_WHITE, 17, "Draw");
        ui_text_right(F_BOLD, 764, y, C_ON_DARK_SOFT, 17, "-");
        num(F_BOLD, 828, y, f->online_draws ? WDL_T[1] : C_ON_DARK_SOFT, 17, f->online_draws);
        ui_text_right(F_BOLD, 892, y, C_ON_DARK_SOFT, 17, "-");
    }
}

static int max_top(void)
{
    return T.n > LIST_ROWS ? T.n - LIST_ROWS : 0;
}

/* the newest games first */
static void draw_recent(void)
{
    static const char *CHIP[3] = {"Won", "Draw", "Lost"};
    char buf[64];
    int k, ty, tm, td, hh, mm;
    plat_local_time(&ty, &tm, &td, &hh, &mm);
    if (T.top > max_top()) T.top = max_top();
    if (T.top < 0) T.top = 0;
    for (k = 0; k < LIST_ROWS && T.top + k < T.n; k++) {
        const gl_game *g = &T.games[T.n - 1 - (T.top + k)];
        int y = LIST_Y + k * LIST_STEP;
        ui_round(48, (float)y, 864, 40, 12, C_PANEL);
        ui_round(60, (float)y + 8, 64, 24, 12, WDL_C[g->result]);
        ui_text_center(F_XBOLD, 92, y + 25, C_WHITE, 14, CHIP[g->result]);
        ui_circle(152, (float)y + 20, 15, RGBA8(226, 234, 246, 255));
        ui_piece_scaled(CH_PIECE(g->white ? CH_WHITE : CH_BLACK, CH_KING), 140, (float)y + 8, 24);
        gl_opponent_text(g, buf, sizeof buf);
        ui_text_fit(F_BOLD, 180, y + 26, C_WHITE, 17, 236, buf);
        gl_how_text(g, buf, sizeof buf);
        ui_text_fit(F_REG, 430, y + 26, C_ON_DARK_SOFT, 15, 340, buf);
        gl_when_text(g, ty, tm, td, 1, buf, sizeof buf);
        ui_text_right(F_REG, 896, y + 26, C_ON_DARK_SOFT, 15, buf);
    }
    if (T.n > LIST_ROWS) {
        float h = LIST_ROWS * LIST_STEP - 5, th = h * LIST_ROWS / (float)T.n, y;
        if (th < 24) th = 24;
        y = LIST_Y + (h - th) * (float)T.top / (float)max_top();
        ui_round(924, LIST_Y, 4, h, 2, ui_alpha(C_WHITE, 22));
        ui_round(924, y, 4, th, 2, ui_alpha(C_WHITE, 120));
    }
}

/* ---------- the screen ---------- */
void stats_open(void)
{
    T.tab = 0;
    T.top = 0;
    T.confirm = 0;
    T.confirm_sel = 0;
}

void stats_draw(void)
{
    char buf[96];
    int i, x, w;
    ui_text(F_XBOLD, 48, 56, C_WHITE, 34, "Your statistics");
    if (T.n) {
        char day[24];
        int ty, tm, td, hh, mm;
        plat_local_time(&ty, &tm, &td, &hh, &mm);
        gl_when_text(&T.games[0], ty, tm, td, 0, day, sizeof day);
        snprintf(buf, sizeof buf, "%d game%s on this Vita since %s", T.n, T.n == 1 ? "" : "s", day);
    } else {
        snprintf(buf, sizeof buf, "Kept on this Vita: nothing is sent anywhere.");
    }
    ui_text(F_REG, 50, 80, C_ON_DARK_SOFT, 17, buf);
    ui_circle(912, 44, 19, ui_alpha(C_WHITE, 46));
    ui_line(905, 37, 919, 51, 4, C_WHITE);
    ui_line(919, 37, 905, 51, 4, C_WHITE);

    shoulder(48, 105, "L");
    for (i = 0; i < 4; i++) {
        tab_geo(i, &x, &w);
        ui_round((float)x, 98, (float)w, 36, 18, T.tab == i ? C_CYAN : C_PANEL_HI);
        ui_text_center(F_BOLD, x + w / 2, 122, T.tab == i ? C_WHITE : C_ON_DARK_SOFT, 17, TABS[i]);
    }
    shoulder((float)(x + w + 10), 105, "R");
    if (T.tab == 3 && T.n) {
        int last = T.top + LIST_ROWS < T.n ? T.top + LIST_ROWS : T.n;
        snprintf(buf, sizeof buf, "%d-%d of %d", T.top + 1, last, T.n);
        ui_text_right(F_REG, 912, 122, C_ON_DARK_SOFT, 15, buf);
    }

    if (!T.n) draw_empty(150);
    else if (T.tab == 0) draw_overview();
    else if (T.tab == 1) draw_levels();
    else if (T.tab == 2) draw_online();
    else draw_recent();

    x = ui_hint(G_BACK, 48, 532, "Back");
    if (T.n) ui_hint(G_SQUARE, x, 532, "Clear statistics");
    if (T.confirm) {
        if (T.n == 1) snprintf(buf, sizeof buf, "1 game is removed.");
        else snprintf(buf, sizeof buf, "All %d games are removed.", T.n);
        /* Keep is picked first, so a stray press of OK doesn't clear anything */
        ui_confirm_draw_sel("Clear your statistics?", buf, "Clear", "Keep", T.confirm_sel);
    }
}

int stats_input(const plat_input *in)
{
    int i;
    if (T.confirm) {
        int hit = PLAT_TAP(in, ui_confirm_hit);
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) T.confirm_sel ^= 1;
        if (((in->pressed & BTN_OK) && T.confirm_sel) || hit == 1) {
            clear_all();
            T.confirm = 0;
        } else if ((in->pressed & (BTN_OK | BTN_BACK)) || hit == 0) {
            T.confirm = 0;
        }
        return 0;
    }
    if (in->pressed & BTN_BACK) return 1;
    if ((in->pressed & BTN_SQUARE) && T.n) {
        T.confirm = 1;
        T.confirm_sel = 0;
        return 0;
    }
    if (in->pressed & (BTN_L | BTN_LEFT)) {
        T.tab = (T.tab + 3) % 4;
        T.top = 0;
    }
    if (in->pressed & (BTN_R | BTN_RIGHT)) {
        T.tab = (T.tab + 1) % 4;
        T.top = 0;
    }
    if (T.tab == 3) {
        if (in->pressed & BTN_UP) T.top--;
        if (in->pressed & BTN_DOWN) T.top++;
    }
    if (in->touch_up) {
        int dx = in->tx - 912, dy = in->ty - 44, swipe = in->down_y - in->ty;
        if (dx * dx + dy * dy < 34 * 34) return 1;
        for (i = 0; i < 4; i++) {
            int x, w;
            tab_geo(i, &x, &w);
            if (ui_hit(in->tx, in->ty, (float)x, 98, (float)w, 36)) {
                T.tab = i;
                T.top = 0;
            }
        }
        /* the list scrolls with a swipe: one row per 45 px */
        if (T.tab == 3 && in->down_y >= LIST_Y && (swipe > 30 || swipe < -30)) T.top += swipe / LIST_STEP ? swipe / LIST_STEP : (swipe > 0 ? 1 : -1);
    }
    if (T.top > max_top()) T.top = max_top();
    if (T.top < 0) T.top = 0;
    return 0;
}

/* ---------- the home screen's strip ---------- */
void stats_draw_strip(float x, float y, float w, float h, int selected)
{
    int i;
    if (selected) ui_sel_ring(x, y, w, h, h / 2, C_GOLD, 0);
    ui_round(x, y, w, h, h / 2, C_PANEL_HI);
    for (i = 0; i < 3; i++) {
        static const float bh[3] = {10, 18, 14};
        ui_round(x + 24 + (float)i * 8, y + 36 - bh[i], 6, bh[i], 2, WDL_C[i]);
    }
    ui_text(F_XBOLD, (int)x + 58, (int)y + 33, C_WHITE, 19, "Your stats");
    {
        int room = (int)(w - 44 - 58 - 18) - ui_text_w(F_XBOLD, 19, "Your stats"); /* between the label and the chevron */
        if (T.f.all.n) counts(0, (int)y + 32, 15, &T.f.all, (int)(x + w) - 44, room);
        else if (ui_text_w(F_REG, 15, "No games yet") <= room) /* left out where it would run into the label */
            ui_text_right(F_REG, (int)(x + w) - 44, (int)y + 32, C_ON_DARK_SOFT, 15, "No games yet");
    }
    ui_line(x + w - 30, y + 18, x + w - 21, y + 26, 3, C_WHITE);
    ui_line(x + w - 21, y + 26, x + w - 30, y + 34, 3, C_WHITE);
}
