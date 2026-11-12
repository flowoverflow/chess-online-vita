/* Chess Online: chess rules core (original code, portable C99, host-tested).
 * Squares: a1 = 0, b1 = 1, ... h8 = 63 (index = rank * 8 + file).
 * Pieces: colour << 3 | type, colour 0 = white, 1 = black; 0 = empty. */
#ifndef CHESS_H
#define CHESS_H

#include <stddef.h>
#include <stdint.h>

enum { CH_PAWN = 1, CH_KNIGHT, CH_BISHOP, CH_ROOK, CH_QUEEN, CH_KING };
enum { CH_WHITE = 0, CH_BLACK = 1 };

#define CH_PIECE(color, type) ((uint8_t)(((color) << 3) | (type)))
#define CH_TYPE(p) ((p) & 7)
#define CH_COLOR(p) (((p) >> 3) & 1)
#define CH_FILE(sq) ((sq) & 7)
#define CH_RANK(sq) ((sq) >> 3)
#define CH_SQ(file, rank) ((rank) * 8 + (file))

/* castling rights */
#define CH_WK 1
#define CH_WQ 2
#define CH_BK 4
#define CH_BQ 8

/* move flags */
#define CH_F_CAPTURE 1
#define CH_F_EP 2
#define CH_F_CASTLE 4
#define CH_F_DOUBLE 8

#define CH_MAX_MOVES 256

typedef struct {
    uint8_t sq[64];
    int side;     /* side to move */
    int castle;   /* CH_WK | CH_WQ | CH_BK | CH_BQ */
    int ep;       /* en passant target square, or -1 */
    int halfmove;
    int fullmove;
} chess_pos;

typedef struct {
    uint8_t from, to;
    uint8_t promo; /* piece type (CH_KNIGHT..CH_QUEEN) or 0 */
    uint8_t flags;
} chess_move;

enum { CH_ONGOING = 0, CH_CHECKMATE = 1, CH_STALEMATE = 2 };

void chess_startpos(chess_pos *p);
/* Accepts a full FEN or "startpos". Returns 0 on success, -1 on a malformed FEN. */
int chess_set_fen(chess_pos *p, const char *fen);
void chess_get_fen(const chess_pos *p, char *out, size_t n);

int chess_square_attacked(const chess_pos *p, int sq, int by_color);
int chess_in_check(const chess_pos *p, int color);
/* Legal moves for the side to move; out must hold CH_MAX_MOVES. Returns the count. */
int chess_legal_moves(const chess_pos *p, chess_move *out);
void chess_make(chess_pos *p, chess_move m);
int chess_status(const chess_pos *p);

/* Finds the legal move a UCI string names ("e2e4", "e7e8q"). Castling is accepted both as the king's
 * destination ("e1g1") and as king-takes-rook ("e1h1"), which Lichess may send. 0 = found, -1 = not legal. */
int chess_parse_uci(const chess_pos *p, const char *uci, chess_move *out);
/* Standard UCI (castling as "e1g1"). out needs 6 bytes. */
void chess_move_uci(chess_move m, char out[6]);
/* SAN for a legal move in position p (before it is made), e.g. "Nbd7", "exd6", "O-O", "e8=Q+". */
void chess_move_san(const chess_pos *p, chess_move m, char *out, size_t n);

/* Finds the legal move a SAN string names ("Nbd7", "exd6", "O-O", "e8=Q+"); check marks and "!?" are ignored.
 * 0 = found, -1 = no legal move has that SAN. */
int chess_parse_san(const chess_pos *p, const char *san, chess_move *out);
/* Compares the first four FEN fields, treating the en-passant field as equal when one side omits it
 * (some engines only write it when a capture is possible). Returns 1 when the positions match. */
int chess_same_fen(const char *a, const char *b);

uint64_t chess_perft(const chess_pos *p, int depth);

void chess_square_name(int sq, char out[3]);

#endif
