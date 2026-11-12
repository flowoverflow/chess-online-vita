/* Chess Online guest mode: Coolmath chess server messages and game model. Our own code; see cmg.h. */
#include "cmg.h"

#include <stdio.h>
#include <string.h>

#include "json.h"

static void cpy(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

static int color_of(const char *s)
{
    return s && !strcmp(s, "black") ? CH_BLACK : CH_WHITE;
}

static const struct {
    const char *name;
    cmg_type type;
} TYPES[] = {
    {"open_games", CMG_M_OPEN_GAMES}, {"stats", CMG_M_STATS}, {"open_created", CMG_M_OPEN_CREATED},
    {"open_cancelled", CMG_M_OPEN_CANCELLED}, {"private_created", CMG_M_PRIVATE_CREATED},
    {"private_cancelled", CMG_M_PRIVATE_CANCELLED}, {"matched", CMG_M_MATCHED}, {"state", CMG_M_STATE},
    {"move", CMG_M_MOVE}, {"timeout", CMG_M_TIMEOUT}, {"resigned", CMG_M_RESIGNED},
    {"opponent_disconnected", CMG_M_OPP_DISCONNECTED}, {"opponent_reconnected", CMG_M_OPP_RECONNECTED},
    {"opponent_left", CMG_M_OPP_LEFT}, {"takeback_offer_sent", CMG_M_TAKEBACK_SENT},
    {"takeback_offer_received", CMG_M_TAKEBACK_RECEIVED}, {"takeback_offer_cancelled", CMG_M_TAKEBACK_CANCELLED},
    {"takeback_declined", CMG_M_TAKEBACK_DECLINED}, {"takeback_applied", CMG_M_TAKEBACK_APPLIED},
    {"rematch_offer_sent", CMG_M_REMATCH_SENT}, {"rematch_offer_received", CMG_M_REMATCH_RECEIVED},
    {"rematch_offer_cancelled", CMG_M_REMATCH_CANCELLED}, {"rematch_declined", CMG_M_REMATCH_DECLINED},
    {"error", CMG_M_ERROR},
};

static void read_clock(const jval *c, cmg_clock *out)
{
    memset(out, 0, sizeof *out);
    if (!c || c->t != J_OBJ) return;
    out->valid = 1;
    out->white_ms = (int64_t)json_num(c, "whiteMs", 0);
    out->black_ms = (int64_t)json_num(c, "blackMs", 0);
    out->increment_ms = (int64_t)json_num(c, "incrementMs", 0);
    out->last_ts = (int64_t)json_num(c, "lastTs", 0);
    out->turn = color_of(json_str(c, "turn", "white"));
}

int cmg_parse(const char *text, size_t len, cmg_msg *m)
{
    jval *root = json_parse(text, len);
    const jval *v;
    const char *t;
    size_t i;
    memset(m, 0, sizeof *m);
    if (!root || root->t != J_OBJ) {
        if (root) json_free(root);
        return -1;
    }
    t = json_str(root, "type", "");
    cpy(m->type_name, sizeof m->type_name, t);
    for (i = 0; i < sizeof TYPES / sizeof *TYPES; i++)
        if (!strcmp(TYPES[i].name, t)) m->type = TYPES[i].type;
    v = json_get(root, "serverTime");
    if (v && v->t == J_NUM) {
        m->has_server_time = 1;
        m->server_time = (int64_t)v->num;
    }
    /* game ids have been numeric strings; accept numbers too */
    v = json_get(root, "gameId");
    if (v && v->t == J_STR) cpy(m->game_id, sizeof m->game_id, v->str);
    else if (v && v->t == J_NUM) snprintf(m->game_id, sizeof m->game_id, "%.0f", v->num);
    cpy(m->message, sizeof m->message, json_str(root, "message", ""));

    switch (m->type) {
    case CMG_M_OPEN_GAMES: {
        const jval *g, *arr = json_get(root, "games");
        for (g = arr && arr->t == J_ARR ? arr->child : NULL; g; g = g->next) {
            cmg_open_game *o;
            const jval *id = json_get(g, "id");
            m->games_total++;
            if (m->ngames >= CMG_MAX_OPEN || !id) continue;
            o = &m->games[m->ngames++];
            if (id->t == J_STR) cpy(o->id, sizeof o->id, id->str);
            else if (id->t == J_NUM) snprintf(o->id, sizeof o->id, "%.0f", id->num);
            o->minutes = (int)json_num(g, "minutes", 0);
            o->increment = (int)json_num(g, "increment", 0);
            cpy(o->level, sizeof o->level, json_str(g, "level", "beginner"));
            cpy(o->color, sizeof o->color, json_str(g, "color", "random"));
            o->created_at = (int64_t)json_num(g, "createdAt", 0);
        }
        break;
    }
    case CMG_M_STATS:
        m->players = (int)json_num(root, "players", 0);
        m->active_games = (int)json_num(root, "activeGames", 0);
        m->open_sessions = (int)json_num(root, "openGameSessions", 0);
        break;
    case CMG_M_PRIVATE_CREATED:
        cpy(m->code, sizeof m->code, json_str(root, "code", ""));
        break;
    case CMG_M_MATCHED:
        m->color = color_of(json_str(root, "color", "white"));
        cpy(m->opponent_id, sizeof m->opponent_id, json_str(root, "opponentId", ""));
        cpy(m->token, sizeof m->token, json_str(root, "reconnectToken", ""));
        break;
    case CMG_M_STATE:
    case CMG_M_MOVE:
    case CMG_M_TAKEBACK_APPLIED: {
        const jval *mv = json_get(root, "moves"), *s;
        const jval *go = json_get(root, "gameOver");
        const char *pr = json_str(root, "promotion", "");
        cpy(m->fen, sizeof m->fen, json_str(root, "fen", ""));
        if (mv && mv->t == J_ARR) {
            m->has_moves = 1;
            for (s = mv->child; s && m->nmoves < CMG_PLY_MAX; s = s->next)
                if (s->t == J_STR) cpy(m->moves[m->nmoves++], sizeof m->moves[0], s->str);
        }
        cpy(m->from, sizeof m->from, json_str(root, "from", ""));
        cpy(m->to, sizeof m->to, json_str(root, "to", ""));
        m->promotion = pr[0];
        cpy(m->san, sizeof m->san, json_str(root, "san", ""));
        read_clock(json_get(root, "clock"), &m->clock);
        if (go && go->t == J_OBJ) {
            m->game_over = 1;
            cpy(m->over_type, sizeof m->over_type, json_str(go, "type", ""));
            cpy(m->winner, sizeof m->winner, json_str(go, "winner", ""));
        }
        break;
    }
    case CMG_M_TIMEOUT:
    case CMG_M_RESIGNED:
    case CMG_M_OPP_LEFT:
        cpy(m->winner, sizeof m->winner, json_str(root, "winner", ""));
        cpy(m->resigned_color, sizeof m->resigned_color, json_str(root, "resignedColor", ""));
        read_clock(json_get(root, "clock"), &m->clock);
        break;
    case CMG_M_OPP_DISCONNECTED:
        m->grace_ms = (int)json_num(root, "graceMs", 0);
        break;
    default:
        break;
    }
    json_free(root);
    return 0;
}

/* ---- client messages ---- */
static int esc(char *out, size_t n, const char *s)
{
    size_t k = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (k + 7 >= n) return -1;
        if (c == '"' || c == '\\') {
            out[k++] = '\\';
            out[k++] = (char)c;
        } else if (c < 0x20) {
            k += (size_t)snprintf(out + k, n - k, "\\u%04x", c);
        } else {
            out[k++] = (char)c;
        }
    }
    out[k] = 0;
    return (int)k;
}

static int fit(int r, size_t n)
{
    return r < 0 || (size_t)r >= n ? -1 : r;
}

int cmg_build_lobby_hello(char *out, size_t n)
{
    return fit(snprintf(out, n, "{\"type\":\"lobby_hello\"}"), n);
}

int cmg_build_create_open(char *out, size_t n, int minutes, int increment, const char *level, const char *color)
{
    char l[24], c[16];
    if (esc(l, sizeof l, level) < 0 || esc(c, sizeof c, color) < 0) return -1;
    return fit(snprintf(out, n, "{\"type\":\"create_open\",\"minutes\":%d,\"increment\":%d,\"level\":\"%s\",\"color\":\"%s\"}",
                        minutes, increment, l, c), n);
}

static int with_id(char *out, size_t n, const char *type, const char *key, const char *val)
{
    char v[128];
    if (esc(v, sizeof v, val) < 0) return -1;
    return fit(snprintf(out, n, "{\"type\":\"%s\",\"%s\":\"%s\"}", type, key, v), n);
}

int cmg_build_join_open(char *out, size_t n, const char *id) { return with_id(out, n, "join_open", "gameId", id); }
int cmg_build_cancel_open(char *out, size_t n, const char *id) { return with_id(out, n, "cancel_open", "gameId", id); }
int cmg_build_join_private(char *out, size_t n, const char *code) { return with_id(out, n, "join_private", "code", code); }
int cmg_build_resume(char *out, size_t n, const char *token) { return with_id(out, n, "resume", "token", token); }
int cmg_build_simple(char *out, size_t n, const char *type, const char *id) { return with_id(out, n, type, "gameId", id); }

int cmg_build_create_private(char *out, size_t n, int minutes, int increment, const char *color)
{
    char c[16];
    if (esc(c, sizeof c, color) < 0) return -1;
    return fit(snprintf(out, n, "{\"type\":\"create_private\",\"minutes\":%d,\"increment\":%d,\"color\":\"%s\"}", minutes,
                        increment, c), n);
}

int cmg_build_move(char *out, size_t n, const char *game_id, chess_move m)
{
    static const char promo[] = "  nbrq";
    char id[48], f[3], t[3];
    if (esc(id, sizeof id, game_id) < 0) return -1;
    chess_square_name(m.from, f);
    chess_square_name(m.to, t);
    if (m.promo)
        return fit(snprintf(out, n, "{\"type\":\"move\",\"gameId\":\"%s\",\"from\":\"%s\",\"to\":\"%s\",\"promotion\":\"%c\"}",
                            id, f, t, promo[m.promo]), n);
    return fit(snprintf(out, n, "{\"type\":\"move\",\"gameId\":\"%s\",\"from\":\"%s\",\"to\":\"%s\"}", id, f, t), n);
}

int cmg_build_response(char *out, size_t n, const char *type, const char *game_id, int accept)
{
    char id[48];
    if (esc(id, sizeof id, game_id) < 0) return -1;
    return fit(snprintf(out, n, "{\"type\":\"%s\",\"gameId\":\"%s\",\"accept\":%s}", type, id, accept ? "true" : "false"),
               n);
}

int cmg_build_leave_beacon(char *out, size_t n, const char *token)
{
    char t[128];
    if (esc(t, sizeof t, token) < 0) return -1;
    return fit(snprintf(out, n, "{\"token\":\"%s\"}", t), n);
}

/* ---- game model ---- */
void cmg_game_init(cmg_game *g)
{
    memset(g, 0, sizeof *g);
    g->last_from = g->last_to = -1;
    g->winner = -1;
    chess_startpos(&g->pos);
}

void cmg_game_matched(cmg_game *g, const cmg_msg *m)
{
    int64_t offs[8], offset = g->offset;
    int noff = g->noffsets, i;
    for (i = 0; i < 8; i++) offs[i] = g->offset_samples[i];
    cmg_game_init(g);
    for (i = 0; i < 8; i++) g->offset_samples[i] = offs[i];
    g->noffsets = noff;
    g->offset = offset;
    cpy(g->game_id, sizeof g->game_id, m->game_id);
    g->my_color = m->color;
    cpy(g->opponent_id, sizeof g->opponent_id, m->opponent_id);
    cpy(g->token, sizeof g->token, m->token);
}

void cmg_note_server_time(cmg_game *g, int64_t server_time, uint64_t local_ms)
{
    int i;
    int64_t best;
    if (g->noffsets == 8) {
        memmove(g->offset_samples, g->offset_samples + 1, sizeof g->offset_samples[0] * 7);
        g->noffsets = 7;
    }
    g->offset_samples[g->noffsets++] = server_time - (int64_t)local_ms;
    best = g->offset_samples[0];
    for (i = 1; i < g->noffsets; i++)
        if (g->offset_samples[i] > best) best = g->offset_samples[i];
    g->offset = best;
}

static int square_of(const char *s)
{
    if (!s || s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
    return CH_SQ(s[0] - 'a', s[1] - '1');
}

/* Rebuilds the move list and last move from the SAN history; the server's FEN stays the position. */
static void take_history(cmg_game *g, const cmg_msg *m)
{
    chess_pos p;
    chess_move mv;
    char fen[100];
    int i;
    g->nmoves = m->nmoves;
    for (i = 0; i < m->nmoves; i++) cpy(g->san[i], sizeof g->san[i], m->moves[i]);
    chess_startpos(&p);
    g->history_ok = 1;
    for (i = 0; i < m->nmoves; i++) {
        if (chess_parse_san(&p, m->moves[i], &mv)) {
            g->history_ok = 0;
            break;
        }
        if (i == m->nmoves - 1 && g->last_from < 0) {
            g->last_from = mv.from;
            g->last_to = mv.to;
        }
        chess_make(&p, mv);
    }
    if (g->history_ok) {
        chess_get_fen(&p, fen, sizeof fen);
        g->history_ok = chess_same_fen(fen, m->fen);
    }
}

/* a game that ends between moves (a resignation, a flag, the opponent leaving): the side to move keeps the time it
 * had at that moment. Without this its clock jumped back to the figure of the last move once it stopped running
 * (seen 2026-11-12: 27:06 -> 29:46 after a resignation, 3:09 -> 3:50 after the opponent left). */
static void freeze_clock(cmg_game *g, uint64_t local_ms)
{
    int64_t *t, used;
    if (!g->clock.valid || !g->clock.last_ts) return;
    t = g->clock.turn == CH_WHITE ? &g->clock.white_ms : &g->clock.black_ms;
    used = (int64_t)local_ms + g->offset - g->clock.last_ts;
    if (used > 0) *t -= used;
    if (*t < 0) *t = 0;
    g->clock.last_ts = 0;
}

static void finish(cmg_game *g, int winner, const char *text, int end)
{
    g->finished = 1;
    g->winner = winner;
    g->end = end;
    g->awaiting_move = 0;
    g->takeback_in = g->takeback_out = 0;
    cpy(g->result, sizeof g->result, text);
}

int cmg_game_apply(cmg_game *g, const cmg_msg *m, uint64_t local_ms)
{
    if (m->has_server_time) cmg_note_server_time(g, m->server_time, local_ms);
    if (m->game_id[0] && strcmp(m->game_id, g->game_id)) return 0; /* another game */
    switch (m->type) {
    case CMG_M_STATE:
    case CMG_M_MOVE:
    case CMG_M_TAKEBACK_APPLIED: {
        chess_pos p;
        if (chess_set_fen(&p, m->fen)) return -1;
        g->pos = p;
        g->have_state = 1;
        if (m->clock.valid) g->clock = m->clock;
        g->last_from = square_of(m->from);
        g->last_to = square_of(m->to);
        if (m->has_moves) take_history(g, m);
        else if (m->type == CMG_M_MOVE && m->san[0] && g->nmoves < CMG_PLY_MAX)
            cpy(g->san[g->nmoves++], sizeof g->san[0], m->san);
        g->awaiting_move = 0;
        if (m->type != CMG_M_STATE) g->takeback_in = g->takeback_out = 0;
        if (m->type == CMG_M_MOVE) g->rematch_in = g->rematch_out = 0;
        if (m->type == CMG_M_STATE) g->opponent_away = 0;
        if (m->game_over) {
            if (!strcmp(m->over_type, "checkmate")) {
                int w = color_of(m->winner);
                finish(g, w, w == CH_WHITE ? "White wins by checkmate" : "Black wins by checkmate", CMG_END_MATE);
            } else if (!strcmp(m->over_type, "stalemate")) {
                finish(g, -1, "Draw by stalemate", CMG_END_STALEMATE);
            } else {
                finish(g, -1, "Draw", CMG_END_DRAW);
            }
        }
        return 1;
    }
    case CMG_M_TIMEOUT: {
        int w = color_of(m->winner);
        if (m->clock.valid) g->clock = m->clock;
        freeze_clock(g, local_ms);
        finish(g, m->winner[0] ? w : -1, m->winner[0] ? (w == CH_WHITE ? "White wins on time" : "Black wins on time")
                                                      : "Game over on time", CMG_END_TIME);
        return 1;
    }
    case CMG_M_RESIGNED: {
        int w = color_of(m->winner);
        if (m->clock.valid) g->clock = m->clock;
        freeze_clock(g, local_ms);
        finish(g, m->winner[0] ? w : -1, w == CH_WHITE ? "White wins: Black resigned" : "Black wins: White resigned", CMG_END_RESIGN);
        return 1;
    }
    case CMG_M_OPP_LEFT:
        if (g->finished) return 1;
        freeze_clock(g, local_ms);
        finish(g, m->winner[0] ? color_of(m->winner) : g->my_color, "Your opponent left the game", CMG_END_LEFT);
        return 1;
    case CMG_M_OPP_DISCONNECTED:
        if (g->finished) return 1;
        g->opponent_away = 1;
        g->grace_ms = m->grace_ms;
        return 1;
    case CMG_M_OPP_RECONNECTED:
        g->opponent_away = 0;
        return 1;
    case CMG_M_TAKEBACK_SENT: g->takeback_out = 1; g->takeback_in = 0; return 1;
    case CMG_M_TAKEBACK_RECEIVED: g->takeback_in = 1; g->takeback_out = 0; return 1;
    case CMG_M_TAKEBACK_CANCELLED:
    case CMG_M_TAKEBACK_DECLINED: g->takeback_in = g->takeback_out = 0; return 1;
    case CMG_M_REMATCH_SENT: g->rematch_out = 1; g->rematch_in = 0; return 1;
    case CMG_M_REMATCH_RECEIVED: g->rematch_in = 1; g->rematch_out = 0; return 1;
    case CMG_M_REMATCH_CANCELLED:
    case CMG_M_REMATCH_DECLINED: g->rematch_in = g->rematch_out = 0; return 1;
    case CMG_M_ERROR:
        g->awaiting_move = 0; /* a refused move: the server's position stays */
        return 1;
    default:
        return 0;
    }
}

int64_t cmg_game_clock(const cmg_game *g, int color, uint64_t local_ms)
{
    int64_t t;
    if (!g->clock.valid) return 0;
    t = color == CH_WHITE ? g->clock.white_ms : g->clock.black_ms;
    if (!g->finished && g->clock.turn == color && g->clock.last_ts) {
        t -= (int64_t)local_ms + g->offset - g->clock.last_ts;
        if (t < 0) t = 0;
    }
    return t;
}

int cmg_game_my_turn(const cmg_game *g)
{
    return g->have_state && !g->finished && !g->awaiting_move && g->pos.side == g->my_color;
}
