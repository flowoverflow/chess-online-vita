/* Chess Online: shared board input and drawing. Our own code; see boardui.h. */
#include "boardui.h"

#include <string.h>

#include "ui.h"

static ui_board geom(int my_color)
{
    ui_board g = UI_MAIN_BOARD(my_color == CH_BLACK);
    return g;
}

void boardui_reset(boardui *b)
{
    memset(b, 0, sizeof *b);
    b->sel_sq = b->cursor = b->drag_from = -1;
}

int boardui_modal(const boardui *b)
{
    return b->promo_open;
}

static void select_sq(boardui *b, const chess_pos *pos, int sq)
{
    chess_move all[CH_MAX_MOVES];
    int n, i;
    b->sel_sq = sq;
    b->ndests = 0;
    if (sq < 0) return;
    n = chess_legal_moves(pos, all);
    for (i = 0; i < n; i++)
        if (all[i].from == sq) b->dests[b->ndests++] = all[i];
}

static int own(const chess_pos *pos, int sq, int color)
{
    int pc = pos->sq[sq];
    return pc && CH_COLOR(pc) == color;
}

/* 1 with *out when a single move matches; opens the picker for promotions */
static int try_to(boardui *b, int to, chess_move *out)
{
    int i, k = 0;
    chess_move found[4];
    for (i = 0; i < b->ndests && k < 4; i++)
        if (b->dests[i].to == to) found[k++] = b->dests[i];
    if (!k) return 0;
    if (k == 1) {
        *out = found[0];
        b->sel_sq = -1;
        b->ndests = 0;
        return 1;
    }
    memcpy(b->promo, found, sizeof b->promo);
    b->promo_open = 1;
    b->promo_sel = 0;
    return 0;
}

static int is_dest(const boardui *b, int sq)
{
    int i;
    for (i = 0; i < b->ndests; i++)
        if (b->dests[i].to == sq) return 1;
    return 0;
}

static int tap(boardui *b, const chess_pos *pos, int sq, int my_color, int can_move, chess_move *out)
{
    if (sq < 0) return 0;
    if (can_move && b->sel_sq >= 0 && sq != b->sel_sq && is_dest(b, sq)) return try_to(b, sq, out);
    if (own(pos, sq, my_color)) select_sq(b, pos, sq == b->sel_sq ? -1 : sq);
    else select_sq(b, pos, -1);
    return 0;
}

static void promo_box(const boardui *b, int my_color, int *x)
{
    ui_board g = geom(my_color);
    int y;
    ui_board_square_xy(&g, b->promo[0].to, x, &y);
    if (*x > 400) *x = 400;
}

int boardui_input(boardui *b, const plat_input *in, const chess_pos *pos, int my_color, int can_move, chess_move *out)
{
    ui_board g = geom(my_color);
    int i;
    if (b->promo_open) {
        int x;
        promo_box(b, my_color, &x);
        if (in->pressed & BTN_UP) b->promo_sel = (b->promo_sel + 3) % 4;
        if (in->pressed & BTN_DOWN) b->promo_sel = (b->promo_sel + 1) % 4;
        if (in->pressed & BTN_BACK) b->promo_open = 0;
        if ((in->pressed & BTN_OK) && can_move) {
            b->promo_open = 0;
            *out = b->promo[b->promo_sel];
            b->sel_sq = -1;
            b->ndests = 0;
            return 1;
        }
        if (in->touch_up && can_move)
            for (i = 0; i < 4; i++)
                if (ui_hit(in->tx, in->ty, (float)x - 14, 66 + i * 100, 96, 96)) {
                    b->promo_open = 0;
                    *out = b->promo[i];
                    b->sel_sq = -1;
                    b->ndests = 0;
                    return 1;
                }
        return 0;
    }
    if (in->pressed & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) {
        int up = g.flip ? -1 : 1, f, r;
        if (b->cursor < 0) {
            b->cursor = b->sel_sq >= 0 ? b->sel_sq : (my_color == CH_WHITE ? CH_SQ(4, 1) : CH_SQ(4, 6));
        } else {
            f = CH_FILE(b->cursor);
            r = CH_RANK(b->cursor);
            if (in->pressed & BTN_UP) r += up;
            if (in->pressed & BTN_DOWN) r -= up;
            if (in->pressed & BTN_RIGHT) f += g.flip ? -1 : 1;
            if (in->pressed & BTN_LEFT) f -= g.flip ? -1 : 1;
            f = f < 0 ? 0 : f > 7 ? 7 : f;
            r = r < 0 ? 0 : r > 7 ? 7 : r;
            b->cursor = CH_SQ(f, r);
        }
    }
    if ((in->pressed & BTN_OK) && b->cursor >= 0 && tap(b, pos, b->cursor, my_color, can_move, out)) return 1;
    if (in->pressed & BTN_BACK) select_sq(b, pos, -1);
    if (in->touch_down) {
        int sq = ui_board_square_at(&g, in->tx, in->ty);
        b->cursor = -1;
        if (sq >= 0) {
            if (can_move && b->sel_sq >= 0 && sq != b->sel_sq && is_dest(b, sq)) return try_to(b, sq, out);
            if (own(pos, sq, my_color)) {
                select_sq(b, pos, sq);
                b->dragging = can_move;
                b->drag_from = sq;
            } else {
                select_sq(b, pos, -1);
            }
        }
    }
    if (in->touch_up && b->dragging) {
        int sq = ui_board_square_at(&g, in->tx, in->ty);
        b->dragging = 0;
        if (sq >= 0 && sq != b->drag_from && is_dest(b, sq) && can_move) {
            b->drag_from = -1;
            return try_to(b, sq, out);
        }
        b->drag_from = -1;
    }
    return 0;
}

void boardui_draw(const boardui *b, const chess_pos *shown, int last_from, int last_to, int my_color, int dim)
{
    ui_board g = geom(my_color);
    ui_board_state s;
    int i;
    memset(&s, 0, sizeof s);
    s.pos = shown;
    s.last_from = last_from;
    s.last_to = last_to;
    s.selected = b->sel_sq;
    s.dests = b->dests;
    s.ndests = b->ndests;
    s.cursor = b->cursor;
    s.check_sq = -1;
    if (chess_in_check(shown, shown->side))
        for (i = 0; i < 64; i++)
            if (shown->sq[i] == CH_PIECE(shown->side, CH_KING)) s.check_sq = i;
    s.drag_from = b->dragging ? b->drag_from : -1;
    s.drag_x = boardui_touch_x;
    s.drag_y = boardui_touch_y;
    ui_draw_board(&g, &s);
    if (dim) ui_board_area(ui_alpha(C_BG_BOTTOM, 120));
    if (b->promo_open) {
        int x;
        promo_box(b, my_color, &x);
        ui_board_area(C_DIM);
        ui_card((float)x - 20, 60, 108, 416, 18, C_CARD);
        for (i = 0; i < 4; i++) {
            ui_round((float)x - 14, 66 + i * 100, 96, 96, 14, b->promo_sel == i ? C_CYAN : C_FIELD);
            ui_piece(CH_PIECE(my_color, b->promo[i].promo), (float)x - 14, 66 + i * 100, 1);
        }
    }
}

int boardui_touch_x, boardui_touch_y; /* set by the main loop: where the finger is, for dragging */
