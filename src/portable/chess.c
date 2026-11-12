/* Chess Online: chess rules core. Original code; see chess.h. */
#include "chess.h"

#include <stdio.h>
#include <string.h>

static const int KNIGHT_D[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int KING_D[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
static const int ROOK_D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int BISHOP_D[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

static int on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

void chess_square_name(int sq, char out[3])
{
    out[0] = (char)('a' + CH_FILE(sq));
    out[1] = (char)('1' + CH_RANK(sq));
    out[2] = 0;
}

void chess_startpos(chess_pos *p)
{
    chess_set_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

static int piece_from_char(char c)
{
    int color = (c >= 'a' && c <= 'z') ? CH_BLACK : CH_WHITE;
    switch (c | 0x20) {
    case 'p': return CH_PIECE(color, CH_PAWN);
    case 'n': return CH_PIECE(color, CH_KNIGHT);
    case 'b': return CH_PIECE(color, CH_BISHOP);
    case 'r': return CH_PIECE(color, CH_ROOK);
    case 'q': return CH_PIECE(color, CH_QUEEN);
    case 'k': return CH_PIECE(color, CH_KING);
    }
    return -1;
}

static char char_from_piece(int pc)
{
    static const char t[] = " pnbrqk";
    char c = t[CH_TYPE(pc)];
    return CH_COLOR(pc) == CH_WHITE ? (char)(c - 32) : c;
}

int chess_set_fen(chess_pos *p, const char *fen)
{
    chess_pos q;
    int file = 0, rank = 7, wk = 0, bk = 0, sq;
    const char *s = fen;
    if (!fen) return -1;
    while (*s == ' ') s++;
    if (strncmp(s, "startpos", 8) == 0) {
        chess_startpos(p);
        return 0;
    }
    memset(&q, 0, sizeof q);
    q.ep = -1;
    q.fullmove = 1;
    for (; *s && *s != ' '; s++) {
        if (*s == '/') {
            if (file != 8 || rank == 0) return -1;
            file = 0;
            rank--;
        } else if (*s >= '1' && *s <= '8') {
            file += *s - '0';
            if (file > 8) return -1;
        } else {
            int pc = piece_from_char(*s);
            if (pc < 0 || file >= 8) return -1;
            q.sq[CH_SQ(file, rank)] = (uint8_t)pc;
            if (pc == CH_PIECE(CH_WHITE, CH_KING)) wk++;
            if (pc == CH_PIECE(CH_BLACK, CH_KING)) bk++;
            file++;
        }
    }
    if (rank != 0 || file != 8 || wk != 1 || bk != 1) return -1;
    while (*s == ' ') s++;
    if (*s == 'w') q.side = CH_WHITE;
    else if (*s == 'b') q.side = CH_BLACK;
    else return -1;
    s++;
    while (*s == ' ') s++;
    for (; *s && *s != ' '; s++) {
        switch (*s) {
        case 'K': q.castle |= CH_WK; break;
        case 'Q': q.castle |= CH_WQ; break;
        case 'k': q.castle |= CH_BK; break;
        case 'q': q.castle |= CH_BQ; break;
        case '-': break;
        default: return -1; /* Chess960 castling letters are not supported */
        }
    }
    /* drop rights whose king or rook is not on its home square */
    if (q.sq[4] != CH_PIECE(CH_WHITE, CH_KING)) q.castle &= ~(CH_WK | CH_WQ);
    if (q.sq[7] != CH_PIECE(CH_WHITE, CH_ROOK)) q.castle &= ~CH_WK;
    if (q.sq[0] != CH_PIECE(CH_WHITE, CH_ROOK)) q.castle &= ~CH_WQ;
    if (q.sq[60] != CH_PIECE(CH_BLACK, CH_KING)) q.castle &= ~(CH_BK | CH_BQ);
    if (q.sq[63] != CH_PIECE(CH_BLACK, CH_ROOK)) q.castle &= ~CH_BK;
    if (q.sq[56] != CH_PIECE(CH_BLACK, CH_ROOK)) q.castle &= ~CH_BQ;
    while (*s == ' ') s++;
    if (*s && *s != '-') {
        if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
        sq = CH_SQ(s[0] - 'a', s[1] - '1');
        q.ep = sq;
        s += 2;
    } else if (*s == '-') {
        s++;
    }
    while (*s == ' ') s++;
    if (*s) {
        q.halfmove = 0;
        while (*s >= '0' && *s <= '9') q.halfmove = q.halfmove * 10 + (*s++ - '0');
        while (*s == ' ') s++;
        if (*s >= '0' && *s <= '9') {
            q.fullmove = 0;
            while (*s >= '0' && *s <= '9') q.fullmove = q.fullmove * 10 + (*s++ - '0');
            if (q.fullmove < 1) q.fullmove = 1;
        }
    }
    *p = q;
    return 0;
}

void chess_get_fen(const chess_pos *p, char *out, size_t n)
{
    char buf[100];
    int k = 0, rank, file, empty;
    for (rank = 7; rank >= 0; rank--) {
        empty = 0;
        for (file = 0; file < 8; file++) {
            int pc = p->sq[CH_SQ(file, rank)];
            if (!pc) {
                empty++;
                continue;
            }
            if (empty) buf[k++] = (char)('0' + empty);
            empty = 0;
            buf[k++] = char_from_piece(pc);
        }
        if (empty) buf[k++] = (char)('0' + empty);
        if (rank) buf[k++] = '/';
    }
    buf[k++] = ' ';
    buf[k++] = p->side == CH_WHITE ? 'w' : 'b';
    buf[k++] = ' ';
    if (!p->castle) buf[k++] = '-';
    if (p->castle & CH_WK) buf[k++] = 'K';
    if (p->castle & CH_WQ) buf[k++] = 'Q';
    if (p->castle & CH_BK) buf[k++] = 'k';
    if (p->castle & CH_BQ) buf[k++] = 'q';
    buf[k++] = ' ';
    if (p->ep >= 0) {
        chess_square_name(p->ep, buf + k);
        k += 2;
    } else {
        buf[k++] = '-';
    }
    buf[k] = 0;
    snprintf(out, n, "%s %d %d", buf, p->halfmove, p->fullmove);
}

int chess_square_attacked(const chess_pos *p, int sq, int by)
{
    int f = CH_FILE(sq), r = CH_RANK(sq), i;
    /* pawns */
    if (by == CH_WHITE) {
        if (r > 0 && f > 0 && p->sq[sq - 9] == CH_PIECE(CH_WHITE, CH_PAWN)) return 1;
        if (r > 0 && f < 7 && p->sq[sq - 7] == CH_PIECE(CH_WHITE, CH_PAWN)) return 1;
    } else {
        if (r < 7 && f < 7 && p->sq[sq + 9] == CH_PIECE(CH_BLACK, CH_PAWN)) return 1;
        if (r < 7 && f > 0 && p->sq[sq + 7] == CH_PIECE(CH_BLACK, CH_PAWN)) return 1;
    }
    for (i = 0; i < 8; i++) {
        int nf = f + KNIGHT_D[i][0], nr = r + KNIGHT_D[i][1];
        if (on_board(nf, nr) && p->sq[CH_SQ(nf, nr)] == CH_PIECE(by, CH_KNIGHT)) return 1;
        nf = f + KING_D[i][0];
        nr = r + KING_D[i][1];
        if (on_board(nf, nr) && p->sq[CH_SQ(nf, nr)] == CH_PIECE(by, CH_KING)) return 1;
    }
    for (i = 0; i < 4; i++) {
        int nf = f + ROOK_D[i][0], nr = r + ROOK_D[i][1];
        while (on_board(nf, nr)) {
            int pc = p->sq[CH_SQ(nf, nr)];
            if (pc) {
                if (CH_COLOR(pc) == by && (CH_TYPE(pc) == CH_ROOK || CH_TYPE(pc) == CH_QUEEN)) return 1;
                break;
            }
            nf += ROOK_D[i][0];
            nr += ROOK_D[i][1];
        }
        nf = f + BISHOP_D[i][0];
        nr = r + BISHOP_D[i][1];
        while (on_board(nf, nr)) {
            int pc = p->sq[CH_SQ(nf, nr)];
            if (pc) {
                if (CH_COLOR(pc) == by && (CH_TYPE(pc) == CH_BISHOP || CH_TYPE(pc) == CH_QUEEN)) return 1;
                break;
            }
            nf += BISHOP_D[i][0];
            nr += BISHOP_D[i][1];
        }
    }
    return 0;
}

static int find_king(const chess_pos *p, int color)
{
    int i;
    for (i = 0; i < 64; i++)
        if (p->sq[i] == CH_PIECE(color, CH_KING)) return i;
    return -1;
}

int chess_in_check(const chess_pos *p, int color)
{
    int k = find_king(p, color);
    return k >= 0 && chess_square_attacked(p, k, color ^ 1);
}

static int add(chess_move *out, int n, int from, int to, int promo, int flags)
{
    out[n].from = (uint8_t)from;
    out[n].to = (uint8_t)to;
    out[n].promo = (uint8_t)promo;
    out[n].flags = (uint8_t)flags;
    return n + 1;
}

static int add_pawn(chess_move *out, int n, int from, int to, int flags)
{
    int r = CH_RANK(to);
    if (r == 0 || r == 7) {
        n = add(out, n, from, to, CH_QUEEN, flags);
        n = add(out, n, from, to, CH_ROOK, flags);
        n = add(out, n, from, to, CH_BISHOP, flags);
        n = add(out, n, from, to, CH_KNIGHT, flags);
        return n;
    }
    return add(out, n, from, to, 0, flags);
}

static int pseudo_moves(const chess_pos *p, chess_move *out)
{
    int n = 0, sq, i, us = p->side, them = us ^ 1;
    for (sq = 0; sq < 64; sq++) {
        int pc = p->sq[sq], f, r;
        if (!pc || CH_COLOR(pc) != us) continue;
        f = CH_FILE(sq);
        r = CH_RANK(sq);
        switch (CH_TYPE(pc)) {
        case CH_PAWN: {
            int dir = us == CH_WHITE ? 1 : -1, start = us == CH_WHITE ? 1 : 6;
            int nr = r + dir, df;
            if (nr >= 0 && nr < 8 && !p->sq[CH_SQ(f, nr)]) {
                n = add_pawn(out, n, sq, CH_SQ(f, nr), 0);
                if (r == start && !p->sq[CH_SQ(f, nr + dir)]) n = add(out, n, sq, CH_SQ(f, nr + dir), 0, CH_F_DOUBLE);
            }
            for (df = -1; df <= 1; df += 2) {
                int nf = f + df, to;
                if (!on_board(nf, nr)) continue;
                to = CH_SQ(nf, nr);
                if (p->sq[to] && CH_COLOR(p->sq[to]) == them) n = add_pawn(out, n, sq, to, CH_F_CAPTURE);
                else if (to == p->ep) n = add(out, n, sq, to, 0, CH_F_CAPTURE | CH_F_EP);
            }
            break;
        }
        case CH_KNIGHT:
        case CH_KING: {
            const int(*d)[2] = CH_TYPE(pc) == CH_KNIGHT ? KNIGHT_D : KING_D;
            for (i = 0; i < 8; i++) {
                int nf = f + d[i][0], nr = r + d[i][1], to;
                if (!on_board(nf, nr)) continue;
                to = CH_SQ(nf, nr);
                if (!p->sq[to]) n = add(out, n, sq, to, 0, 0);
                else if (CH_COLOR(p->sq[to]) == them) n = add(out, n, sq, to, 0, CH_F_CAPTURE);
            }
            break;
        }
        default: {
            int t = CH_TYPE(pc), k;
            for (k = 0; k < 8; k++) {
                const int *d;
                int nf, nr;
                if (k < 4) {
                    if (t == CH_BISHOP) continue;
                    d = ROOK_D[k];
                } else {
                    if (t == CH_ROOK) continue;
                    d = BISHOP_D[k - 4];
                }
                nf = f + d[0];
                nr = r + d[1];
                while (on_board(nf, nr)) {
                    int to = CH_SQ(nf, nr);
                    if (!p->sq[to]) {
                        n = add(out, n, sq, to, 0, 0);
                    } else {
                        if (CH_COLOR(p->sq[to]) == them) n = add(out, n, sq, to, 0, CH_F_CAPTURE);
                        break;
                    }
                    nf += d[0];
                    nr += d[1];
                }
            }
            break;
        }
        }
    }
    /* castling: rights, empty squares, and the king neither in check nor passing an attacked square */
    if (us == CH_WHITE) {
        if ((p->castle & CH_WK) && !p->sq[5] && !p->sq[6] && !chess_square_attacked(p, 4, them) &&
            !chess_square_attacked(p, 5, them) && !chess_square_attacked(p, 6, them))
            n = add(out, n, 4, 6, 0, CH_F_CASTLE);
        if ((p->castle & CH_WQ) && !p->sq[3] && !p->sq[2] && !p->sq[1] && !chess_square_attacked(p, 4, them) &&
            !chess_square_attacked(p, 3, them) && !chess_square_attacked(p, 2, them))
            n = add(out, n, 4, 2, 0, CH_F_CASTLE);
    } else {
        if ((p->castle & CH_BK) && !p->sq[61] && !p->sq[62] && !chess_square_attacked(p, 60, them) &&
            !chess_square_attacked(p, 61, them) && !chess_square_attacked(p, 62, them))
            n = add(out, n, 60, 62, 0, CH_F_CASTLE);
        if ((p->castle & CH_BQ) && !p->sq[59] && !p->sq[58] && !p->sq[57] && !chess_square_attacked(p, 60, them) &&
            !chess_square_attacked(p, 59, them) && !chess_square_attacked(p, 58, them))
            n = add(out, n, 60, 58, 0, CH_F_CASTLE);
    }
    return n;
}

static void clear_rights_for(chess_pos *p, int sq)
{
    switch (sq) {
    case 0: p->castle &= ~CH_WQ; break;
    case 7: p->castle &= ~CH_WK; break;
    case 4: p->castle &= ~(CH_WK | CH_WQ); break;
    case 56: p->castle &= ~CH_BQ; break;
    case 63: p->castle &= ~CH_BK; break;
    case 60: p->castle &= ~(CH_BK | CH_BQ); break;
    }
}

void chess_make(chess_pos *p, chess_move m)
{
    int pc = p->sq[m.from], us = p->side;
    int captured = p->sq[m.to];
    p->sq[m.from] = 0;
    if (m.flags & CH_F_EP) {
        int cap = us == CH_WHITE ? m.to - 8 : m.to + 8;
        p->sq[cap] = 0;
        captured = 1;
    }
    p->sq[m.to] = (uint8_t)(m.promo ? CH_PIECE(us, m.promo) : pc);
    if (m.flags & CH_F_CASTLE) {
        if (m.to == 6) { p->sq[7] = 0; p->sq[5] = CH_PIECE(CH_WHITE, CH_ROOK); }
        else if (m.to == 2) { p->sq[0] = 0; p->sq[3] = CH_PIECE(CH_WHITE, CH_ROOK); }
        else if (m.to == 62) { p->sq[63] = 0; p->sq[61] = CH_PIECE(CH_BLACK, CH_ROOK); }
        else if (m.to == 58) { p->sq[56] = 0; p->sq[59] = CH_PIECE(CH_BLACK, CH_ROOK); }
    }
    clear_rights_for(p, m.from);
    clear_rights_for(p, m.to);
    p->ep = (m.flags & CH_F_DOUBLE) ? (m.from + m.to) / 2 : -1;
    if (CH_TYPE(pc) == CH_PAWN || captured) p->halfmove = 0;
    else p->halfmove++;
    if (us == CH_BLACK) p->fullmove++;
    p->side = us ^ 1;
}

int chess_legal_moves(const chess_pos *p, chess_move *out)
{
    chess_move tmp[CH_MAX_MOVES];
    int n = pseudo_moves(p, tmp), i, k = 0;
    for (i = 0; i < n; i++) {
        chess_pos q = *p;
        chess_make(&q, tmp[i]);
        if (!chess_in_check(&q, p->side)) out[k++] = tmp[i];
    }
    return k;
}

int chess_status(const chess_pos *p)
{
    chess_move m[CH_MAX_MOVES];
    if (chess_legal_moves(p, m) > 0) return CH_ONGOING;
    return chess_in_check(p, p->side) ? CH_CHECKMATE : CH_STALEMATE;
}

static int promo_from_char(char c)
{
    switch (c | 0x20) {
    case 'q': return CH_QUEEN;
    case 'r': return CH_ROOK;
    case 'b': return CH_BISHOP;
    case 'n': return CH_KNIGHT;
    }
    return -1;
}

int chess_parse_uci(const chess_pos *p, const char *uci, chess_move *out)
{
    chess_move m[CH_MAX_MOVES];
    int n, i, from, to, promo = 0, len = (int)strlen(uci);
    if (len < 4 || len > 5) return -1;
    if (uci[0] < 'a' || uci[0] > 'h' || uci[1] < '1' || uci[1] > '8' || uci[2] < 'a' || uci[2] > 'h' || uci[3] < '1' ||
        uci[3] > '8')
        return -1;
    from = CH_SQ(uci[0] - 'a', uci[1] - '1');
    to = CH_SQ(uci[2] - 'a', uci[3] - '1');
    if (len == 5) {
        promo = promo_from_char(uci[4]);
        if (promo < 0) return -1;
    }
    /* king-takes-own-rook castling notation (e1h1, e1a1, e8h8, e8a8) */
    if (p->sq[from] == CH_PIECE(p->side, CH_KING) && p->sq[to] == CH_PIECE(p->side, CH_ROOK)) {
        if (from == 4 && to == 7) to = 6;
        else if (from == 4 && to == 0) to = 2;
        else if (from == 60 && to == 63) to = 62;
        else if (from == 60 && to == 56) to = 58;
    }
    n = chess_legal_moves(p, m);
    for (i = 0; i < n; i++) {
        if (m[i].from == from && m[i].to == to && m[i].promo == promo) {
            *out = m[i];
            return 0;
        }
    }
    return -1;
}

void chess_move_uci(chess_move m, char out[6])
{
    static const char pc[] = "  nbrq";
    chess_square_name(m.from, out);
    chess_square_name(m.to, out + 2);
    if (m.promo) {
        out[4] = pc[m.promo];
        out[5] = 0;
    }
}

void chess_move_san(const chess_pos *p, chess_move m, char *out, size_t n)
{
    static const char letters[] = "  NBRQK";
    char buf[16], sqn[3];
    int k = 0, type = CH_TYPE(p->sq[m.from]);
    chess_pos q;
    if (m.flags & CH_F_CASTLE) {
        strcpy(buf, CH_FILE(m.to) == 6 ? "O-O" : "O-O-O");
        k = (int)strlen(buf);
    } else {
        if (type == CH_PAWN) {
            if (m.flags & CH_F_CAPTURE) buf[k++] = (char)('a' + CH_FILE(m.from));
        } else {
            chess_move all[CH_MAX_MOVES];
            int cnt = chess_legal_moves(p, all), i, ambiguous = 0, same_file = 0, same_rank = 0;
            buf[k++] = letters[type];
            for (i = 0; i < cnt; i++) {
                if (all[i].to != m.to || all[i].from == m.from || CH_TYPE(p->sq[all[i].from]) != type) continue;
                ambiguous = 1;
                if (CH_FILE(all[i].from) == CH_FILE(m.from)) same_file = 1;
                if (CH_RANK(all[i].from) == CH_RANK(m.from)) same_rank = 1;
            }
            if (ambiguous) {
                if (!same_file) {
                    buf[k++] = (char)('a' + CH_FILE(m.from));
                } else if (!same_rank) {
                    buf[k++] = (char)('1' + CH_RANK(m.from));
                } else {
                    buf[k++] = (char)('a' + CH_FILE(m.from));
                    buf[k++] = (char)('1' + CH_RANK(m.from));
                }
            }
        }
        if (m.flags & CH_F_CAPTURE) buf[k++] = 'x';
        chess_square_name(m.to, sqn);
        buf[k++] = sqn[0];
        buf[k++] = sqn[1];
        if (m.promo) {
            buf[k++] = '=';
            buf[k++] = letters[m.promo];
        }
    }
    q = *p;
    chess_make(&q, m);
    if (chess_in_check(&q, q.side)) buf[k++] = chess_status(&q) == CH_CHECKMATE ? '#' : '+';
    buf[k] = 0;
    snprintf(out, n, "%s", buf);
}

static void san_core(const char *in, char *out, size_t n)
{
    size_t k = 0;
    for (; *in && k + 1 < n; in++) {
        if (*in == '+' || *in == '#' || *in == '!' || *in == '?' || *in == ' ') continue;
        out[k++] = *in == '0' ? 'O' : *in; /* "0-0" written with zeros */
    }
    out[k] = 0;
}

int chess_parse_san(const chess_pos *p, const char *san, chess_move *out)
{
    chess_move m[CH_MAX_MOVES];
    char want[16], have[16], gen[16];
    int n = chess_legal_moves(p, m), i;
    san_core(san, want, sizeof want);
    if (!want[0]) return -1;
    for (i = 0; i < n; i++) {
        chess_move_san(p, m[i], gen, sizeof gen);
        san_core(gen, have, sizeof have);
        if (!strcmp(have, want)) {
            *out = m[i];
            return 0;
        }
    }
    return -1;
}

int chess_same_fen(const char *a, const char *b)
{
    char fa[4][80], fb[4][80];
    int i;
    if (!a || !b) return 0;
    for (i = 0; i < 4; i++) fa[i][0] = fb[i][0] = 0;
    if (sscanf(a, "%79s %79s %79s %79s", fa[0], fa[1], fa[2], fa[3]) < 3) return 0;
    if (sscanf(b, "%79s %79s %79s %79s", fb[0], fb[1], fb[2], fb[3]) < 3) return 0;
    for (i = 0; i < 3; i++)
        if (strcmp(fa[i], fb[i])) return 0;
    if (!strcmp(fa[3], fb[3])) return 1;
    return !fa[3][0] || !fb[3][0] || !strcmp(fa[3], "-") || !strcmp(fb[3], "-");
}

uint64_t chess_perft(const chess_pos *p, int depth)
{
    chess_move m[CH_MAX_MOVES];
    int n, i;
    uint64_t total = 0;
    if (depth == 0) return 1;
    n = chess_legal_moves(p, m);
    if (depth == 1) return (uint64_t)n;
    for (i = 0; i < n; i++) {
        chess_pos q = *p;
        chess_make(&q, m[i]);
        total += chess_perft(&q, depth - 1);
    }
    return total;
}
