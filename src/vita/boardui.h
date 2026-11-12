/* Chess Online: board input and drawing shared by the online and computer game screens. Our own code.
 * Touch (tap-tap or drag), d-pad cursor with the console's confirm button, legal-move dots and the promotion
 * picker. The board sits at (24, 24) with 62 px squares in a 16 px frame (0.8.0; 0.7.0: (16, 16), 64 px). */
#ifndef CO_BOARDUI_H
#define CO_BOARDUI_H

#include "chess.h"
#include "platform.h"

typedef struct {
    int sel_sq, cursor, dragging, drag_from;
    chess_move dests[CH_MAX_MOVES];
    int ndests;
    int promo_open, promo_sel;
    chess_move promo[4];
} boardui;

void boardui_reset(boardui *b);
/* Returns 1 with *out set when the player chose a legal move. can_move = it is the player's turn and a move may
 * be sent now; pieces can still be picked up to look at their moves otherwise. */
int boardui_input(boardui *b, const plat_input *in, const chess_pos *pos, int my_color, int can_move, chess_move *out);
/* shown: the position to draw (may include a pending move); dim: grey the board (read-only / loading). */
void boardui_draw(const boardui *b, const chess_pos *shown, int last_from, int last_to, int my_color, int dim);
/* Takes the input while the promotion picker is open (it sits over the board). */
int boardui_modal(const boardui *b);

/* Where the finger is (screen pixels), set by the main loop each frame: a dragged piece follows it. */
extern int boardui_touch_x, boardui_touch_y;

#endif
