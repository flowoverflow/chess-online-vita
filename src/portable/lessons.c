/* Chess Online: "Chess tips". Our own lessons: text, positions and choice of examples written for this app. */
#include "lessons.h"

#include <string.h>

#define START "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

static const lesson_step DEVELOP[] = {
    {START, "", "g1f3 b1c3", "",
     "In the opening, bring your knights and bishops off the back rank. Try to move a new piece every turn instead "
     "of moving the same one twice."},
    {NULL, "e4", "f1c4 d1h5", "e4",
     "1.e4 takes a share of the centre and opens two lines at once: one for the queen and one for the bishop."},
    {NULL, "e5 Nf3", "f3e5", "e5 d4",
     "Knights work best close to the centre. From f3 this one attacks the pawn on e5 and keeps an eye on d4."},
    {NULL, "Nc6 Bc4", "c4f7", "f7",
     "Bishops like long, open diagonals. On c4 the bishop points at f7, the square next to Black's king that only "
     "the king defends."},
    {NULL, "Bc5 c3 Nf6", "e1g1", "",
     "Black has three pieces out and White two. With the knight and bishop gone, the squares between White's king "
     "and rook are empty, so White can castle."},
};

static const lesson_step CASTLE[] = {
    {START, "e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6", "e1g1", "e1",
     "A king left in the middle gets into trouble once the centre files open. Castling is the fastest way to put "
     "it somewhere safe."},
    {NULL, "O-O", "", "g1 f1",
     "Castling is one move: the king steps two squares toward a rook, and that rook hops over to the king's other "
     "side."},
    {NULL, "O-O", "", "g8 f8",
     "Black castles too. Each king now sits behind three pawns, and the rooks can come to the middle."},
    {"r3k2r/pppqbppp/2np1n2/4p3/4P3/2NP1N2/PPPQBPPP/R3K2R w KQkq - 0 1", "", "e1g1 e1c1", "",
     "You can castle on either side. Short castling (toward the h-file) is quicker; long castling also brings the "
     "rook straight to the d-file."},
    {NULL, "", "", "e1 h1 a1",
     "The rules: the king and that rook must not have moved, the squares between them must be empty, and the king "
     "may not be in check, pass over an attacked square or land on one."},
};

static const lesson_step COUNT[] = {
    {START, "", "", "",
     "Give each piece points: pawn 1, knight 3, bishop 3, rook 5, queen 9. Adding them up tells you who is ahead "
     "in material."},
    {"4k3/2p5/3n4/8/8/8/8/3RK3 w - - 0 1", "", "d1d6", "d6 c7",
     "Count before you capture. The rook can take the knight, but the pawn on c7 takes back."},
    {NULL, "Rxd6 cxd6", "", "d6",
     "That trade gave 5 points for 3: White was a point ahead and is now a point behind. A swap is good only if "
     "you get at least as much as you give."},
    {"4k3/8/8/3r4/8/2N5/8/4K3 w - - 0 1", "", "c3d5", "d5",
     "A piece nobody defends is free. Here the knight can take the rook on d5 and nothing can take back."},
    {NULL, "Nxd5", "", "d5",
     "White won a rook for nothing and is now a knight up. Keep counting after every capture and you'll always "
     "know where you stand."},
};

static const lesson_step TACTICS[] = {
    {"8/8/2k1q3/8/8/5N2/8/6K1 w - - 0 1", "", "f3d4", "",
     "A fork is one piece attacking two at once. Knights are great at it: watch the knight on f3 jump to d4."},
    {NULL, "Nd4+", "d4c6 d4e6", "c6 e6",
     "Now the knight checks the king and attacks the queen together. The king has to move, so the queen falls."},
    {NULL, "Kd6 Nxe6 Kxe6", "", "e6",
     "Even after the king takes the knight back, White traded 3 points for 9."},
    {"r1bqkbnr/ppp2ppp/2np4/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4", "", "b5e8", "c6",
     "A pin freezes a piece. The knight on c6 can't move away, because that would open the line from the bishop "
     "to its own king."},
    {NULL, "", "c8d7", "d7",
     "Black can end the pin by putting a piece on d7, such as ...Bd7. Before every move, look for forks and pins "
     "for both sides."},
};

static const lesson_step MATES[] = {
    {"6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", "", "a1a8", "g8",
     "Checkmate wins the game: the king is in check and has no way out. Here Black's own pawns fence the king in."},
    {NULL, "Ra8#", "", "g8 f7 g7 h7",
     "Back-rank mate. The rook checks along the last rank and the pawns block every escape. Moving one of them a "
     "square earlier, such as ...h6, would have given the king a way out."},
    {"7k/8/6K1/8/8/8/8/5Q2 w - - 0 1", "", "f1f8", "",
     "With king and queen against a lone king, drive the king to the edge and bring your own king close to help."},
    {NULL, "Qf8#", "", "g7 h7 g8",
     "The queen checks and covers g8 and g7, and the white king guards h7 and g7. Checkmate."},
    {"r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 3 3", "", "h5f7 c4f7", "f7",
     "Guard f7 in the opening. The queen and the bishop both aim at it here."},
    {NULL, "Nf6 Qxf7#", "", "f7",
     "After the careless 3...Nf6, Qxf7 is checkmate. Black should defend first, with 3...g6 or 3...Qe7."},
};

#define N(a) ((int)(sizeof(a) / sizeof(*(a))))

const lesson LESSONS[] = {
    {"Develop your pieces", "Bring out your knights and bishops", CH_PIECE(CH_WHITE, CH_KNIGHT), 3, DEVELOP,
     N(DEVELOP)},
    {"Castle early", "Keep your king safe, link your rooks", CH_PIECE(CH_WHITE, CH_ROOK), 1, CASTLE, N(CASTLE)},
    {"Count the material", "Know who is ahead before a trade", CH_PIECE(CH_BLACK, CH_QUEEN), 1, COUNT, N(COUNT)},
    {"Forks and pins", "Double attacks and pinned pieces", CH_PIECE(CH_WHITE, CH_BISHOP), 1, TACTICS,
     N(TACTICS)},
    {"Checkmate patterns", "Three mates every player needs", CH_PIECE(CH_BLACK, CH_KING), 1, MATES, N(MATES)},
};
const int LESSON_COUNT = N(LESSONS);

int lesson_squares(const char *s, int *out, int max)
{
    int n = 0;
    if (!s) return 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
        out[n++] = CH_SQ(s[0] - 'a', s[1] - '1');
        s += 2;
    }
    return n;
}

static int play_san_list(chess_pos *p, const char *moves, chess_move *last, int *has_last)
{
    char tok[16];
    const char *s = moves ? moves : "";
    while (*s) {
        size_t n;
        chess_move m;
        while (*s == ' ') s++;
        if (!*s) break;
        n = strcspn(s, " ");
        if (n >= sizeof tok) return -1;
        memcpy(tok, s, n);
        tok[n] = 0;
        s += n;
        if (chess_parse_san(p, tok, &m)) return -1;
        chess_make(p, m);
        *last = m;
        *has_last = 1;
    }
    return 0;
}

int lesson_position(const lesson *l, int step, chess_pos *out, chess_move *last, int *has_last)
{
    int i;
    chess_move m;
    memset(&m, 0, sizeof m);
    *has_last = 0;
    if (step < 0 || step >= l->nsteps) return -1;
    for (i = step; i > 0 && !l->steps[i].fen; i--) {}
    if (!l->steps[i].fen || chess_set_fen(out, l->steps[i].fen)) return -1;
    for (; i <= step; i++)
        if (play_san_list(out, l->steps[i].moves, &m, has_last)) return -1;
    if (last) *last = m;
    return 0;
}
