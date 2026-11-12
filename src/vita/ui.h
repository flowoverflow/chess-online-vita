/* Chess Online: drawing helpers on vita2d. Our own code, palette and layout (navy panels, cyan buttons, white
 * dialogs, a wooden board). */
#ifndef CO_UI_H
#define CO_UI_H

#include <vita2d.h>

#include "chess.h"

#define SCREEN_W 960
#define SCREEN_H 544

/* palette (RGBA8 is vita2d's ABGR packing) */
#define C_BG_TOP ((unsigned)RGBA8(28, 52, 86, 255))
#define C_BG_BOTTOM ((unsigned)RGBA8(10, 20, 36, 255))
#define C_PANEL ((unsigned)RGBA8(18, 34, 60, 235))
#define C_PANEL_HI ((unsigned)RGBA8(36, 64, 104, 255))
#define C_PANEL_LINE ((unsigned)RGBA8(64, 98, 144, 255))
#define C_INK ((unsigned)RGBA8(26, 36, 58, 255))
#define C_INK_SOFT ((unsigned)RGBA8(102, 114, 138, 255))
#define C_ON_DARK ((unsigned)RGBA8(255, 255, 255, 255))
#define C_ON_DARK_SOFT ((unsigned)RGBA8(166, 190, 222, 255))
#define C_WHITE ((unsigned)RGBA8(255, 255, 255, 255))
#define C_CARD ((unsigned)RGBA8(255, 255, 255, 255))
#define C_FIELD ((unsigned)RGBA8(236, 242, 250, 255))
#define C_FIELD_LINE ((unsigned)RGBA8(204, 214, 230, 255))
#define C_SHADOW ((unsigned)RGBA8(0, 0, 0, 90))
#define C_CYAN ((unsigned)RGBA8(0, 170, 228, 255))
#define C_CYAN_HI ((unsigned)RGBA8(40, 200, 255, 255))
#define C_CYAN_DARK ((unsigned)RGBA8(0, 112, 170, 255))
#define C_GOLD ((unsigned)RGBA8(255, 190, 36, 255))
#define C_GREEN ((unsigned)RGBA8(56, 176, 86, 255))
#define C_AMBER ((unsigned)RGBA8(244, 156, 26, 255))
#define C_RED ((unsigned)RGBA8(226, 64, 64, 255))
#define C_VIOLET ((unsigned)RGBA8(130, 104, 255, 255))
#define C_DIM ((unsigned)RGBA8(4, 10, 22, 180))
#define C_SUN C_GOLD /* the selection highlight */

enum { F_REG = 0, F_BOLD, F_XBOLD };

int ui_load(void);
/* rasterizes the next n font sizes the app uses in the language in use's font family; returns 1 once all are ready
 * (draw no text before that). A language from another family (0.8.0: Latin, Cyrillic, Japanese, Korean, Simplified
 * or Traditional Chinese) starts it over. */
int ui_warm_step(int n);

void ui_background(void);
void ui_dim(void);
void ui_rect(float x, float y, float w, float h, unsigned c);
void ui_round(float x, float y, float w, float h, float r, unsigned c);
void ui_card(float x, float y, float w, float h, float r, unsigned c);
void ui_circle(float x, float y, float r, unsigned c);
void ui_ring(float x, float y, float r, float thick, unsigned c);
/* the part of a rounded rectangle between x0 and x1 (a bar made of coloured parts) */
void ui_round_band(float x, float y, float w, float h, float r, float x0, float x1, unsigned c);
/* part of a ring, angles in radians clockwise from the +x axis (screen y points down) */
void ui_arc(float x, float y, float r, float thick, float a0, float a1, unsigned c);
/* computer level i (0..7): green to amber to red */
unsigned ui_level_tint(int i);
void ui_poly(const float *xy, int n, unsigned c); /* convex polygon */
void ui_line(float x0, float y0, float x1, float y1, float thick, unsigned c);
unsigned ui_mix(unsigned a, unsigned b, float t);
unsigned ui_alpha(unsigned c, int a);

/* text: y is the baseline. Every text is translated into the language in use as it is drawn (i18n.h); the _raw
 * functions draw or measure a text as it is (a language's own name in the language picker) */
int ui_text(int font, int x, int y, unsigned c, int size, const char *s);
int ui_text_raw(int font, int x, int y, unsigned c, int size, const char *s);
int ui_text_w_raw(int font, int size, const char *s);
/* a text as it is, in language lang's font family (0.8.0: the picker shows each language's name in its own letters) */
int ui_text_in(int lang, int font, int x, int y, unsigned c, int size, const char *s);
int ui_text_w_in(int lang, int font, int size, const char *s);
int ui_text_w(int font, int size, const char *s);
void ui_text_center(int font, int cx, int y, unsigned c, int size, const char *s);
void ui_text_right(int font, int rx, int y, unsigned c, int size, const char *s);
/* draws s cut with "..." to fit max_w */
void ui_text_fit(int font, int x, int y, unsigned c, int size, int max_w, const char *s);
/* word-wrapped text; returns the number of lines (c == 0: measure only) */
int ui_text_wrap(int font, int x, int y, unsigned c, int size, int max_w, int line_h, const char *s);

/* PS button glyphs; G_OK / G_BACK follow the console's confirm-button setting (cross or circle) */
enum { G_CROSS, G_CIRCLE, G_TRIANGLE, G_SQUARE, G_OK, G_BACK };
void ui_set_ok_is_circle(int on);
void ui_glyph(int g, float cx, float cy, float size);
/* "[glyph] label" hint (light text, for the dark background); returns the x after it */
int ui_hint(int g, int x, int y, const char *label);
int ui_hint_c(int g, int x, int y, const char *label, unsigned c);

int ui_hit(int px, int py, float x, float y, float w, float h);

/* pieces: CH_PIECE values; big = 96 px set */
void ui_piece(int piece, float x, float y, int big);
void ui_piece_scaled(int piece, float x, float y, float size);
#define UI_BOTH_KINGS (-2) /* "random colour" icon for ui_choice */

/* ---- widgets ---- */
/* the selection outline: 3 px round a shape of radius r, and round the `lip` px under it too */
void ui_sel_ring(float x, float y, float w, float h, float r, unsigned c, float lip);
/* a piece multiplied by a colour: a white piece's body takes the colour, its black outline stays */
void ui_piece_tinted(int piece, float x, float y, float size, unsigned color);
/* a solid button with a lip; text colour tc */
void ui_button(float x, float y, float w, float h, unsigned color, unsigned tc, const char *label, int selected);
/* the big cyan menu pill with a piece in a white disc */
void ui_menu_pill(float x, float y, float w, float h, const char *label, const char *sub, int piece, int selected);
/* white dialog with a navy title band and a close button at its top right */
void ui_modal(float x, float y, float w, float h, const char *title, int piece, int close_sel);
int ui_modal_close_hit(int px, int py, float x, float y, float w);
/* label and value over a track; the block fits a 50 px row (0.6.1) */
void ui_slider(float x, float y, float w, const char *label, const char *val, int v, int lo, int hi, int selected);
int ui_slider_hit(int px, int py, float x, float y, float w);
int ui_slider_value(int px, float x, float w, int lo, int hi);
/* a choice tile: coloured when on, faded when off; piece < 0 none, UI_BOTH_KINGS for "random" */
void ui_choice(float x, float y, float w, float h, unsigned color, int piece, const char *label, int on, int selected);
/* the same with the piece drawn in `tint` (0: as it is); a square tile (w == h) centres its piece and label */
void ui_choice_tint(float x, float y, float w, float h, unsigned color, int piece, const char *label, int on, int selected,
                    unsigned tint);
/* a clock box; active = its side is to move, low = under 20 s */
void ui_clock(float x, float y, float w, float h, const char *text, int active, int low);

/* game side panel (x 544..944) pieces, shared by the online and computer games */
#define GP_X 544
#define GP_W 400
void ui_player_bar(float y, int color, const char *name, const char *sub, const char *clock, int turn, int low);
void ui_status_strip(float y, const char *text, unsigned color, int dots, unsigned long long now);
void ui_moves_box(float y, float h, const char (*san)[12], int nmoves, const char *empty, const char *note);
/* the three buttons under the moves (labels NULL to leave a slot out); returns nothing, see ui_game_button_hit */
#define GP_BTN_Y 388
#define GP_BTN_H 48
void ui_game_buttons(const char *a, const char *b, const char *c);
int ui_game_button_hit(int px, int py); /* 0..2, or -1 */

/* list dialog (game menu): title, items, selection */
void ui_menu_draw(const char *title, const char *const *items, int n, int sel);
int ui_menu_hit(int px, int py, int n); /* item index, -2 the close button, -1 nothing */
/* yes / no dialog */
void ui_confirm_draw(const char *title, const char *sub, const char *yes, const char *no);
/* the same with the picked button given: 1 the red "yes", 0 the other */
void ui_confirm_draw_sel(const char *title, const char *sub, const char *yes, const char *no, int sel);
int ui_confirm_hit(int px, int py); /* 1 yes, 0 no, -1 nothing */
/* result card over the board; extra (NULL: none) is one more line under the buttons, and the card grows 26 px */
void ui_result_draw(const char *head, unsigned head_c, const char *sub, const char *b1, const char *b2, int sel,
                    const char *extra, unsigned extra_c);
int ui_result_hit(int px, int py); /* 0, 1, or -1 */

typedef struct {
    int x, y, sq; /* top-left and square size */
    int flip;     /* 1: black at the bottom */
    int coords;   /* draw a-h / 1-8 (on the edge squares, or in the frame) */
    int frame;    /* 1: a 16 px wooden frame with the letters in it (0.8.0, the user's pick) */
} ui_board;

/* the big board (games, lessons, Home): since 0.8.0 its letters sit in a 16 px frame, so it is at (24, 24) with
 * 62 px squares and the frame spans 8-536 (0.7.0: (16, 16), 64 px squares, letters in the corner squares) */
#define UI_MAIN_BOARD(flip) {24, 24, 62, (flip), 1, 1}
void ui_board_area(unsigned c); /* fills the big board's area, frame included (dims over the board) */

typedef struct {
    const chess_pos *pos;
    int last_from, last_to;   /* -1 for none */
    int selected;             /* -1 */
    const chess_move *dests;  /* moves from the selected square */
    int ndests;
    int cursor;               /* -1 when hidden */
    int check_sq;             /* -1 */
    int drag_from;            /* -1; the piece follows drag_x/y */
    int drag_x, drag_y;
} ui_board_state;

void ui_draw_board(const ui_board *b, const ui_board_state *s);
int ui_board_square_at(const ui_board *b, int x, int y);
void ui_board_square_xy(const ui_board *b, int sq, int *x, int *y);
void ui_board_state_init(ui_board_state *s, const chess_pos *pos);
void ui_arrow(const ui_board *b, int from, int to, unsigned c);
void ui_mark(const ui_board *b, int sq, unsigned c);

#endif
