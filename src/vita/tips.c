/* Chess Online: "Chess tips". Our own code, lessons (src/portable/lessons.c) and thumbnails drawn from the
 * lesson's own position. */
#include "tips.h"

#include <stdio.h>
#include <string.h>

#include "chess.h"
#include "lessons.h"
#include "ui.h"

static struct {
    int view; /* 0 gallery, 1 lesson */
    int sel, lesson, step;
    int close_sel;
} T;

/* gallery grid: 3 cards on the first row, 2 centred under them; 16:9 thumbnails */
#define TW 272
#define TH 153
static void card_xy(int i, float *x, float *y)
{
    if (i < 3) {
        *x = 48 + i * (TW + 24);
        *y = 92;
    } else {
        *x = 196 + (i - 3) * (TW + 24);
        *y = 306;
    }
}

static const unsigned ACCENT[5] = {RGBA8(0, 150, 210, 255), RGBA8(40, 160, 90, 255), RGBA8(220, 140, 20, 255),
                                   RGBA8(120, 96, 230, 255), RGBA8(210, 70, 70, 255)};

int tips_count(void)
{
    return LESSON_COUNT;
}

const char *tips_title(int i)
{
    return i >= 0 && i < LESSON_COUNT ? LESSONS[i].title : "";
}

static void draw_step_board(const ui_board *g, const lesson *l, int step)
{
    chess_pos p;
    chess_move last;
    ui_board_state s;
    int has_last, sq[16], n, k;
    if (lesson_position(l, step, &p, &last, &has_last)) return;
    ui_board_state_init(&s, &p);
    if (has_last) {
        s.last_from = last.from;
        s.last_to = last.to;
    }
    if (chess_in_check(&p, p.side))
        for (k = 0; k < 64; k++)
            if (p.sq[k] == CH_PIECE(p.side, CH_KING)) s.check_sq = k;
    ui_draw_board(g, &s);
    n = lesson_squares(l->steps[step].marks, sq, 16);
    for (k = 0; k < n; k++) ui_mark(g, sq[k], RGBA8(255, 190, 36, 230));
    n = lesson_squares(l->steps[step].arrows, sq, 16);
    for (k = 0; k + 1 < n; k += 2) ui_arrow(g, sq[k], sq[k + 1], RGBA8(18, 150, 64, 200));
}

void tips_draw_thumb(int i, float x, float y, float w, float h, int selected)
{
    const lesson *l;
    char buf[16];
    ui_board g;
    float left, ic;
    int S;
    if (i < 0 || i >= LESSON_COUNT) return;
    l = &LESSONS[i];
    if (selected) ui_sel_ring(x, y, w, h, 11, C_GOLD, 0);
    ui_round(x, y, w, h, 12, ui_mix(ACCENT[i % 5], C_BG_BOTTOM, 0.45f));
    ui_round(x, y, w, h * 0.5f, 12, ui_alpha(C_WHITE, 18));
    S = (int)((h - 14) / 8);
    memset(&g, 0, sizeof g);
    g.sq = S;
    g.x = (int)(x + w - 7 - 8 * S);
    g.y = (int)(y + (h - 8 * S) / 2);
    draw_step_board(&g, l, l->thumb_step);
    left = (float)g.x - x - 4;
    ic = h * 0.48f;
    ui_circle(x + left / 2, y + h * 0.42f, ic * 0.62f, ui_alpha(C_WHITE, 46));
    ui_piece_scaled(l->icon, x + left / 2 - ic / 2, y + h * 0.42f - ic / 2, ic);
    snprintf(buf, sizeof buf, "Tip %d", i + 1);
    { /* the badge grows with its word ("Consiglio 1"), at least 60 px */
        int size = h > 120 ? 15 : 13, bw = ui_text_w(F_BOLD, size, buf) + 16;
        if (bw < 60) bw = 60;
        ui_round(x + left / 2 - bw / 2.0f, y + h - 30, (float)bw, 22, 11, ui_alpha(C_BG_BOTTOM, 170));
        ui_text_center(F_BOLD, (int)(x + left / 2), (int)(y + h - 14), C_WHITE, size, buf);
    }
}

void tips_open(int lesson)
{
    T.close_sel = 0;
    if (lesson >= 0 && lesson < LESSON_COUNT) {
        T.view = 1;
        T.lesson = T.sel = lesson;
        T.step = 0;
    } else {
        T.view = 0;
    }
}

static void open_lesson(int i)
{
    T.view = 1;
    T.lesson = i;
    T.step = 0;
}

/* ---------- gallery ---------- */
static void draw_gallery(void)
{
    int i;
    ui_text(F_XBOLD, 48, 56, C_WHITE, 34, "Chess tips");
    ui_text(F_REG, 50, 80, C_ON_DARK_SOFT, 17, "Short lessons played out on the board. Pick one to start.");
    if (T.close_sel) ui_circle(912, 44, 24, C_GOLD);
    ui_circle(912, 44, 19, ui_alpha(C_WHITE, 46));
    ui_line(905, 37, 919, 51, 4, C_WHITE);
    ui_line(919, 37, 905, 51, 4, C_WHITE);
    for (i = 0; i < LESSON_COUNT; i++) {
        float x, y;
        card_xy(i, &x, &y);
        tips_draw_thumb(i, x, y, TW, TH, !T.close_sel && T.sel == i);
        ui_text_fit(F_XBOLD, (int)x + 2, (int)(y + TH + 27), C_WHITE, 20, TW, LESSONS[i].title);
        ui_text_fit(F_REG, (int)x + 2, (int)(y + TH + 49), C_ON_DARK_SOFT, 16, TW, LESSONS[i].blurb);
    }
    {
        int x = 48;
        x = ui_hint(G_OK, x, 532, "Open");
        ui_hint(G_BACK, x, 532, "Back");
    }
}

static int gallery_input(const plat_input *in)
{
    int i;
    if (in->pressed & BTN_BACK) return 1;
    if (T.close_sel) {
        if (in->pressed & (BTN_DOWN | BTN_LEFT | BTN_RIGHT)) T.close_sel = 0;
    } else {
        if ((in->pressed & BTN_LEFT) && T.sel > 0) T.sel--;
        if ((in->pressed & BTN_RIGHT) && T.sel + 1 < LESSON_COUNT) T.sel++;
        if ((in->pressed & BTN_DOWN) && T.sel < 3) T.sel = T.sel + 3 < LESSON_COUNT ? T.sel + 3 : LESSON_COUNT - 1;
        if (in->pressed & BTN_UP) {
            if (T.sel >= 3) T.sel -= 3;
            else T.close_sel = 1;
        }
    }
    if (in->pressed & BTN_OK) {
        if (T.close_sel) return 1;
        open_lesson(T.sel);
    }
    if (in->touch_up) {
        float dx = in->tx - 912.0f, dy = in->ty - 44.0f;
        if (dx * dx + dy * dy < 34 * 34) return 1;
        for (i = 0; i < LESSON_COUNT; i++) {
            float x, y;
            card_xy(i, &x, &y);
            if (ui_hit(in->tx, in->ty, x, y, TW, TH + 56)) {
                T.sel = i;
                T.close_sel = 0;
                open_lesson(i);
                return 0;
            }
        }
    }
    return 0;
}

/* ---------- lesson viewer ---------- */
#define BTN_Y 452
static void draw_lesson(void)
{
    const lesson *l = &LESSONS[T.lesson];
    ui_board g = UI_MAIN_BOARD(0);
    char buf[48];
    int k, last = T.step + 1 >= l->nsteps, lines;
    draw_step_board(&g, l, T.step);
    ui_round(GP_X, 16, GP_W, 512, 18, C_PANEL);
    snprintf(buf, sizeof buf, "TIP %d", T.lesson + 1);
    {
        int bw = ui_text_w(F_XBOLD, 15, buf) + 24;
        if (bw < 70) bw = 70;
        ui_round(GP_X + 20, 32, (float)bw, 26, 13, ACCENT[T.lesson % 5]);
        ui_text_center(F_XBOLD, GP_X + 20 + bw / 2, 51, C_WHITE, 15, buf);
    }
    snprintf(buf, sizeof buf, "Step %d of %d", T.step + 1, l->nsteps);
    ui_text_right(F_BOLD, GP_X + GP_W - 20, 51, C_ON_DARK_SOFT, 16, buf);
    lines = ui_text_wrap(F_XBOLD, GP_X + 20, 94, C_WHITE, 26, GP_W - 40, 32, l->title);
    ui_rect(GP_X + 20, 94 + (lines - 1) * 32 + 16, 60, 4, C_CYAN);
    ui_text_wrap(F_REG, GP_X + 20, 94 + (lines - 1) * 32 + 56, C_WHITE, 20, GP_W - 40, 29, l->steps[T.step].text);
    for (k = 0; k < l->nsteps; k++) {
        float cx = GP_X + GP_W / 2.0f + (k - (l->nsteps - 1) / 2.0f) * 22;
        ui_circle(cx, 428, k == T.step ? 7 : 5, k == T.step ? C_CYAN_HI : ui_alpha(C_WHITE, 70));
    }
    ui_button(GP_X + 20, BTN_Y, 170, 50, T.step ? C_PANEL_HI : ui_alpha(C_PANEL_HI, 120), T.step ? C_WHITE : C_ON_DARK_SOFT,
              "Back", 0);
    ui_button(GP_X + 210, BTN_Y, 170, 50, C_GOLD, C_INK,
              last ? (T.lesson + 1 < LESSON_COUNT ? "Next tip" : "Done") : "Next", 1);
    {
        int x = 552;
        x = ui_hint(G_OK, x, 538, "Next");
        x = ui_hint(G_SQUARE, x, 538, "Step back");
        ui_hint(G_BACK, x, 538, "All tips");
    }
}

static void lesson_next(void)
{
    const lesson *l = &LESSONS[T.lesson];
    if (T.step + 1 < l->nsteps) T.step++;
    else if (T.lesson + 1 < LESSON_COUNT) {
        T.lesson++;
        T.sel = T.lesson;
        T.step = 0;
    } else {
        T.view = 0;
    }
}

static void lesson_prev(void)
{
    if (T.step > 0) T.step--;
}

static int lesson_input(const plat_input *in)
{
    if (in->pressed & BTN_BACK) {
        T.view = 0;
        T.sel = T.lesson;
        return 0;
    }
    if (in->pressed & (BTN_OK | BTN_RIGHT | BTN_R)) lesson_next();
    else if (in->pressed & (BTN_SQUARE | BTN_LEFT | BTN_L)) lesson_prev();
    if (in->touch_up) {
        if (ui_hit(in->tx, in->ty, GP_X + 20, BTN_Y, 170, 54)) lesson_prev();
        else if (ui_hit(in->tx, in->ty, GP_X + 210, BTN_Y, 170, 54)) lesson_next();
    }
    return 0;
}

int tips_input(const plat_input *in)
{
    return T.view ? lesson_input(in) : gallery_input(in);
}

void tips_draw(uint64_t now)
{
    (void)now;
    if (T.view) draw_lesson();
    else draw_gallery();
}
