/* Chess Online online play: anonymous guest games on Coolmath's chess server. Our own code; see guest.h.
 *
 * Transport purposes (each change of purpose is a fresh connection):
 *   LOBBY:   sends lobby_hello; open games and counters; create_open / join_open / cancel_open. Kept open in the
 *            background from app start, so the home screen shows who is online.
 *   PRIVATE: sends create_private or join_private; the game then continues on this same connection.
 *   GAME:    sends resume with the saved reconnect token (after a public match, and after every drop).
 * Reconnect tokens are credentials: kept in memory only, never logged or shown. */
#include "guest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardui.h"
#include "chess.h"
#include "cmg.h"
#include "gamelog.h"
#include "i18n.h"
#include "statsui.h"
#include "ui.h"

#define CMG_LEAVE_URL "https://chess1.coolmathgames.com/api/leave_game"

typedef enum { GT_NONE, GT_LOBBY, GT_PRIVATE, GT_GAME } purpose_t;
typedef enum { GV_NONE, GV_RANDOM, GV_FRIEND, GV_GAME } view_t;

static const char *LEVELS[] = {"beginner", "medium", "advanced"};
static const char *LEVEL_NAMES[] = {"Beginner", "Medium", "Advanced"};
static const char *COLORS[] = {"random", "white", "black"};
static const char *COLOR_NAMES[] = {"Random", "White", "Black"};

enum { RF_MIN, RF_INC, RF_LEVEL, RF_COLOR, RF_CREATE, RF_ROWS };      /* random: create form */
enum { FF_MIN, FF_INC, FF_COLOR, FF_MAKE, FF_CODE, FF_JOIN, FF_ROWS }; /* friend: right side, then left */

static struct {
    view_t view, came_from;
    purpose_t purpose;
    int open;
    int attempts;
    uint64_t retry_at;
    int ws_fails, use_poll;
    char note[160];
    uint64_t note_until;
    uint64_t now;
    /* lobby */
    cmg_open_game games[CMG_MAX_OPEN];
    int ngames, games_total, have_list;
    int players, active_games, have_stats;
    int focus; /* random view: 0 list, 1 form */
    int list_sel, list_top, form_sel, friend_sel;
    int minutes, increment, level, color;
    char open_id[24];
    int own_listed; /* the last list had our own open game in it (left out) */
    int create_pending, join_pending;
    /* private */
    int priv_action; /* 1 create, 2 join */
    char priv_code[16];
    char join_code[16]; /* typed by the player for "Join a friend" */
    char join_err[96];  /* why the server refused that code, shown under Join game until the code or view changes */
    int priv_waiting;
    /* game */
    cmg_game game;
    int in_game, resume_in_flight;
    boardui bu;
    chess_move pending;
    int has_pending;
    int menu_open, menu_sel, confirm, result_sel;
    /* statistics: the game's time control (from what was created or joined, then from the server's clock at move
     * 0), when it began, and whether its result is recorded */
    int tc_min, tc_inc, tc_known;
    uint64_t game_started;
    int recorded;
    char stats_line[96];
    unsigned stats_c;
} S = {.minutes = 5, .increment = 8};

static cmg_msg msg; /* large; parsed one at a time on the main thread */

static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(S.note, sizeof S.note, fmt, ap);
    va_end(ap);
    S.note_until = S.now + 6000;
    plat_log("online: %s", S.note);
}

static int reconnect_delay(int attempt, int cap)
{
    int base = 1000;
    while (attempt-- > 0 && base < cap) base *= 2;
    if (base > cap) base = cap;
    return base * (75 + rand() % 51) / 100; /* +/- 25 % jitter */
}

static void connect_for(purpose_t p)
{
    S.purpose = p;
    S.open = 0;
    S.retry_at = 0;
    net_cmg_open(S.use_poll);
    plat_log("online: connecting for %s (%s)", p == GT_LOBBY ? "lobby" : p == GT_PRIVATE ? "private game" : "game",
             S.use_poll ? "long-poll" : "WebSocket");
}

static int send_json(const char *json)
{
    if (net_cmg_send(json)) {
        note("Not connected right now");
        return -1;
    }
    return 0;
}

static void reset_lobby_state(void)
{
    S.open_id[0] = 0;
    S.create_pending = S.join_pending = 0;
    S.priv_action = 0;
    S.priv_waiting = 0;
    S.priv_code[0] = 0;
}

static void game_ui_reset(void)
{
    boardui_reset(&S.bu);
    S.has_pending = 0;
    S.menu_open = S.confirm = 0;
    S.result_sel = 0;
}

/* back to the lobby connection; view GV_NONE hands the screen back to the home menu */
static void to_lobby(view_t view)
{
    S.in_game = 0;
    S.resume_in_flight = 0;
    memset(S.game.token, 0, sizeof S.game.token);
    S.view = view;
    game_ui_reset();
    reset_lobby_state();
    S.attempts = 0;
    net_cmg_close();
    connect_for(GT_LOBBY);
}

void guest_start(void)
{
    S.view = GV_NONE;
    game_ui_reset();
    reset_lobby_state();
    connect_for(GT_LOBBY);
}

void guest_open(int view)
{
    S.view = view == GUEST_VIEW_FRIEND ? GV_FRIEND : GV_RANDOM;
    S.came_from = S.view;
    S.focus = 0;
    S.join_err[0] = 0;
    if (S.purpose == GT_NONE || (!net_cmg_state() && !S.retry_at)) connect_for(GT_LOBBY);
}

int guest_playing(void)
{
    return S.in_game && !S.game.finished;
}

int guest_showing(void)
{
    return S.view != GV_NONE;
}

void guest_get_status(guest_status *st)
{
    memset(st, 0, sizeof *st);
    st->state = net_cmg_state();
    if (!st->state && S.retry_at > S.now) st->retry_in_s = (int)((S.retry_at - S.now + 999) / 1000);
    st->have_stats = S.have_stats;
    st->players = S.players;
    st->active_games = S.active_games;
    st->open_games = S.have_list ? S.games_total : -1;
    st->poll = S.use_poll;
}

/* ---------- messages ---------- */
static void start_game_from(const cmg_msg *m)
{
    cmg_game_matched(&S.game, m);
    S.in_game = 1;
    S.view = GV_GAME;
    S.tc_known = 0; /* a rematch keeps the last game's time control until the clock says otherwise */
    S.game_started = S.now;
    S.recorded = 0;
    S.stats_line[0] = 0;
    game_ui_reset();
    reset_lobby_state();
    plat_log("online: matched game %s as %s (opponent %s)", S.game.game_id,
             S.game.my_color == CH_WHITE ? "white" : "black", S.game.opponent_id);
}

/* the game's result into the statistics, once; left: you left before it ended (a loss, as the app tells you) */
static void record_game(int left)
{
    gl_game g;
    const cmg_game *cg = &S.game;
    if (S.recorded || !S.in_game) return;
    memset(&g, 0, sizeof g);
    g.mode = S.came_from == GV_FRIEND ? GL_FRIEND : GL_RANDOM;
    g.minutes = S.tc_min;
    g.increment = S.tc_inc;
    g.white = cg->my_color == CH_WHITE;
    if (left) {
        g.result = GL_LOST;
        g.end = GL_END_LEFT;
    } else {
        g.result = cg->winner < 0 ? GL_DRAWN : cg->winner == cg->my_color ? GL_WON : GL_LOST;
        switch (cg->end) {
        case CMG_END_MATE: g.end = GL_END_MATE; break;
        case CMG_END_RESIGN: g.end = GL_END_RESIGN; break;
        case CMG_END_TIME: g.end = GL_END_TIME; break;
        case CMG_END_LEFT: g.end = GL_END_LEFT; break;
        case CMG_END_STALEMATE: g.end = GL_END_STALEMATE; break;
        default: g.end = GL_END_DRAW; break;
        }
        if (g.result == GL_DRAWN && g.end <= GL_END_LEFT) g.end = GL_END_DRAW; /* e.g. a timeout with no winner */
    }
    g.moves = (cg->nmoves + 1) / 2;
    g.secs = (int)((S.now - S.game_started) / 1000);
    S.stats_c = stats_record(&g, S.stats_line, sizeof S.stats_line);
    S.recorded = 1;
}

static void on_message(const char *text, size_t len)
{
    if (cmg_parse(text, len, &msg)) {
        plat_log("online: unreadable message (%d bytes) ignored", (int)len);
        return;
    }
    if (msg.type != CMG_M_OPEN_GAMES && msg.type != CMG_M_STATS)
        plat_log("online: << %s%s%s", msg.type_name, msg.game_id[0] ? " game " : "", msg.game_id);
    switch (msg.type) {
    case CMG_M_OPEN_GAMES: {
        /* the server lists our own waiting game too (seen 2026-11-12, with a Join button): leave it out of the list
         * and out of the count, which is then the games of other players */
        int i, n = 0, own = 0;
        for (i = 0; i < msg.ngames; i++) {
            if (S.open_id[0] && !strcmp(msg.games[i].id, S.open_id)) {
                own = 1;
                continue;
            }
            S.games[n++] = msg.games[i];
        }
        if (own && !S.own_listed) plat_log("online: our open game %s is in the list: left out", S.open_id);
        S.own_listed = own;
        S.ngames = n;
        S.games_total = msg.games_total - own;
        if (S.games_total < n) S.games_total = n;
        S.have_list = 1;
        if (S.list_sel >= S.ngames) S.list_sel = S.ngames ? S.ngames - 1 : 0;
        return;
    }
    case CMG_M_STATS:
        S.players = msg.players;
        S.active_games = msg.active_games;
        S.have_stats = 1;
        return;
    case CMG_M_OPEN_CREATED:
        S.create_pending = 0;
        snprintf(S.open_id, sizeof S.open_id, "%s", msg.game_id);
        return;
    case CMG_M_OPEN_CANCELLED:
        S.open_id[0] = 0;
        S.create_pending = 0;
        return;
    case CMG_M_PRIVATE_CREATED:
        snprintf(S.priv_code, sizeof S.priv_code, "%s", msg.code);
        S.priv_waiting = 1;
        return;
    case CMG_M_PRIVATE_CANCELLED:
        S.priv_waiting = 0;
        S.priv_code[0] = 0;
        note("The private game was cancelled");
        return;
    case CMG_M_MATCHED:
        if (S.purpose == GT_GAME && S.in_game && !strcmp(msg.game_id, S.game.game_id)) {
            if (msg.token[0]) snprintf(S.game.token, sizeof S.game.token, "%s", msg.token); /* our resume */
            S.game.my_color = msg.color;
            return;
        }
        start_game_from(&msg);
        if (S.purpose == GT_LOBBY) {
            /* public match: hand over to a fresh game connection that resumes with the token */
            S.resume_in_flight = 1;
            net_cmg_close();
            connect_for(GT_GAME);
        } else {
            S.purpose = GT_GAME; /* private (and rematch) games continue on this connection */
        }
        return;
    case CMG_M_ERROR:
        if (S.resume_in_flight) {
            note("That game is no longer available (%s)", msg.message[0] ? msg.message : "resume refused");
            to_lobby(S.came_from);
            return;
        }
        if (S.in_game) {
            int was_waiting = S.game.awaiting_move;
            cmg_game_apply(&S.game, &msg, S.now);
            S.has_pending = 0;
            note("%s%s", msg.message[0] ? msg.message : "Refused by the server", was_waiting ? " (board restored)" : "");
        } else {
            int joining = S.priv_action == 2;
            S.create_pending = S.join_pending = 0;
            if (joining) {
                S.priv_action = 0;
                to_lobby(GV_FRIEND);
                /* 1.0.0: a refused code shows under Join game (draw_friend), not as the bottom toast, which covered
                 * the Make a code side's hint line */
                snprintf(S.join_err, sizeof S.join_err, "%s", msg.message[0] ? msg.message : "The server refused that");
                plat_log("online: join refused: %s", S.join_err);
            } else {
                note("%s", msg.message[0] ? msg.message : "The server refused that");
            }
        }
        return;
    default:
        break;
    }
    if (S.in_game) {
        int r = cmg_game_apply(&S.game, &msg, S.now);
        if (r < 0) note("Couldn't read the position from the server");
        if (r > 0) {
            if (msg.type == CMG_M_STATE) S.resume_in_flight = 0;
            if (msg.type == CMG_M_STATE || msg.type == CMG_M_MOVE || msg.type == CMG_M_TAKEBACK_APPLIED) {
                S.has_pending = 0;
                S.bu.sel_sq = -1;
                S.bu.ndests = 0;
                S.bu.promo_open = 0;
                if (!S.game.history_ok && msg.has_moves) plat_log("online: move list didn't replay to the server's FEN");
            }
            if (!S.tc_known && S.game.clock.valid && S.game.nmoves == 0) {
                const cmg_clock *c = &S.game.clock;
                int64_t ms = c->white_ms > c->black_ms ? c->white_ms : c->black_ms;
                S.tc_min = (int)((ms + 30000) / 60000);
                S.tc_inc = (int)(c->increment_ms / 1000);
                S.tc_known = 1;
            }
            if (S.game.finished && !S.recorded) {
                memset(S.game.token, 0, sizeof S.game.token); /* a finished game can't be resumed */
                plat_log("online: game %s finished: %s", S.game.game_id, S.game.result);
                record_game(0);
            }
        }
    }
}

int guest_on_net(const net_msg *m, uint64_t now)
{
    if (m->tag != NET_TAG_CMG || m->kind == NM_RESPONSE) return 0;
    S.now = now;
    if (m->gen != net_cmg_gen()) return 1; /* from a transport we replaced */
    if (m->kind == NM_OPEN) {
        char buf[160];
        S.open = 1;
        S.attempts = 0;
        if (!S.use_poll) S.ws_fails = 0;
        if (S.purpose == GT_LOBBY) {
            cmg_build_lobby_hello(buf, sizeof buf);
            send_json(buf);
        } else if (S.purpose == GT_PRIVATE) {
            if (S.priv_action == 1) cmg_build_create_private(buf, sizeof buf, S.minutes, S.increment, COLORS[S.color]);
            else cmg_build_join_private(buf, sizeof buf, S.priv_code);
            send_json(buf);
        } else if (S.purpose == GT_GAME && S.game.token[0]) {
            cmg_build_resume(buf, sizeof buf, S.game.token);
            S.resume_in_flight = 1;
            send_json(buf);
            memset(buf, 0, sizeof buf);
            plat_log("online: resume sent for game %s", S.game.game_id);
        }
        return 1;
    }
    if (m->kind == NM_LINE) {
        on_message(m->data, m->len);
        return 1;
    }
    if (m->kind == NM_END) {
        int was_open = S.open;
        S.open = 0;
        plat_log("online: transport ended (%s, HTTP %d, %s, was %s)", S.use_poll ? "long-poll" : "WebSocket", m->status,
                 m->curl_rc == -1 ? "poll error" : net_error_text(m->curl_rc), was_open ? "open" : "connecting");
        if (!was_open) {
            if (!S.use_poll && ++S.ws_fails >= 2) {
                S.use_poll = 1; /* WebSockets keep failing on this network: try HTTPS long-polling */
                note("WebSocket failed twice: trying the HTTPS fallback");
            } else if (S.use_poll) {
                S.use_poll = 0;
                S.ws_fails = 0;
                note("Can't reach the chess server (%s)", m->curl_rc == -1 ? "HTTPS fallback failed"
                                                                         : net_error_text(m->curl_rc));
            } else if (S.view != GV_NONE) {
                note("Can't connect: %s", net_error_text(m->curl_rc));
            }
        }
        if (S.purpose == GT_GAME) {
            if (!S.in_game || S.game.finished) return 1; /* nothing to resume */
            S.resume_in_flight = 0;
            S.has_pending = 0; /* never resend: the state after resume says what happened */
            S.retry_at = now + (uint64_t)reconnect_delay(S.attempts++, 8000);
        } else {
            if (S.open_id[0]) note("Your open game was lost with the connection; create it again if you like");
            if (S.priv_waiting || S.priv_action) note("The private game was lost with the connection");
            reset_lobby_state();
            S.purpose = GT_LOBBY;
            S.retry_at = now + (uint64_t)reconnect_delay(S.attempts++, 30000);
        }
        return 1;
    }
    return 1;
}

void guest_on_response(const net_msg *m)
{
    if (m->tag == GUEST_REQ_LEAVE) plat_log("online: leave_game request answered HTTP %d", m->status);
}

void guest_timers(uint64_t now)
{
    S.now = now;
    if (S.retry_at && now >= S.retry_at && !net_cmg_state()) {
        S.retry_at = 0;
        connect_for(S.purpose == GT_NONE ? GT_LOBBY : S.purpose);
    }
}

/* ---------- shared bits ---------- */
static const char *speed_name(int minutes, int inc)
{
    return gl_speed_name(gl_speed(minutes, inc));
}

static const int LEVEL_PIECE[3] = {CH_PIECE(CH_WHITE, CH_PAWN), CH_PIECE(CH_WHITE, CH_BISHOP),
                                   CH_PIECE(CH_WHITE, CH_QUEEN)};
static const unsigned LEVEL_COLOR[3] = {C_GREEN, C_AMBER, C_RED};
static const int COLOR_PIECE[3] = {UI_BOTH_KINGS, CH_PIECE(CH_WHITE, CH_KING), CH_PIECE(CH_BLACK, CH_KING)};

static int level_index(const char *lv)
{
    return !strcmp(lv, "advanced") ? 2 : !strcmp(lv, "medium") ? 1 : 0;
}

/* the dialogs: one size for both lobby views */
#define MX 40
#define MY 14
#define MW 880
#define MH 504

static void conn_text(char *buf, size_t n, unsigned *dot)
{
    int st = net_cmg_state();
    if (st == 2 && S.have_stats) snprintf(buf, n, "%d online, %d games", S.players, S.active_games);
    else if (st == 2) snprintf(buf, n, "Online as a guest");
    else if (st == 1) snprintf(buf, n, "Connecting%s...", S.use_poll ? " (HTTPS)" : "");
    else if (S.retry_at > S.now) snprintf(buf, n, "Reconnecting in %d s", (int)((S.retry_at - S.now + 999) / 1000));
    else snprintf(buf, n, "Offline");
    *dot = st == 2 ? C_GREEN : st == 1 ? C_GOLD : C_RED;
}

static void modal_status(int mx, int mw)
{
    char buf[96];
    unsigned dot;
    int w, x;
    conn_text(buf, sizeof buf, &dot);
    w = ui_text_w(F_BOLD, 16, buf);
    x = mx + mw - 82 - w;
    ui_circle(x - 14, MY + 37, 6, dot);
    ui_text(F_BOLD, x, MY + 43, C_ON_DARK_SOFT, 16, buf);
}

static void draw_note(void)
{
    int w;
    if (!S.note[0] || S.now >= S.note_until) return;
    w = ui_text_w(F_BOLD, 19, S.note) + 40;
    if (w > 900) w = 900;
    ui_card((SCREEN_W - w) / 2.0f, 470, (float)w, 44, 22, C_INK);
    ui_text_fit(F_BOLD, (SCREEN_W - w) / 2 + 20, 499, C_WHITE, 19, w - 40, S.note);
}

static int step(int v, int d, int lo, int hi)
{
    v += d;
    if (v < lo) v = hi;
    if (v > hi) v = lo;
    return v;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void waiting_dots(float cx, float y)
{
    int k, ph = (int)((S.now / 300) % 3);
    for (k = 0; k < 3; k++) ui_circle(cx + (k - 1) * 18, y, k == ph ? 6 : 4, C_CYAN);
}

/* ---------- random opponent ----------
 * The dialog is only as wide as its content (728 px, centred). Join list on the left: its Level column is just the
 * piece in the level colour. Create side on the right: exactly three 76 px squares wide, each row of squares under
 * its one-line name. Play a friend keeps the full-width dialog (MX, MW) and its own constants. */
#define RMX 116
#define RMW 728
#define LX 140
#define LW 404
#define RX 572
#define RW 248
#define ROW_Y0 166
#define ROW_H 46
#define ROW_STEP 52
#define ROWS_VIS 6
#define COL_SPEED 62 /* list columns, from LX (0.7.0: 62, so "Niveau" and "Livello" don't run into the next header) */
#define COL_TIME 146
#define COL_YOU 210
#define RSL1_Y 136
#define RSL2_Y 186 /* 50 px under the first slider (0.6.1, was 46): the selected one's gold box clears the other */
#define SQ 76
#define SQ_GAP 10
#define LV_LABEL_Y 254
#define LV_Y 262
#define CL_LABEL_Y 362
#define CL_Y 370
#define CREATE_Y 456 /* the button and its 4 px lip end at 500: 18 px above the dialog's bottom */
#define CREATE_H 40
#define SL1_Y 140 /* Play a friend's sliders and tiles */
#define SL2_Y 196
#define TILE_H 60

static float sq_x(int i)
{
    return RX + i * (SQ + SQ_GAP);
}

static void draw_random(void)
{
    char buf[96];
    int i, y, form = S.focus == 1;
    ui_modal(RMX, MY, RMW, MH, "Random opponent", CH_PIECE(CH_WHITE, CH_KNIGHT), 0);
    modal_status(RMX, RMW);
    ui_rect(LX + LW + 13, MY + 92, 2, MH - 112, C_FIELD_LINE);

    /* join: the waiting players */
    ui_text(F_XBOLD, LX, 128, C_INK, 22, "Join a game");
    if (S.have_list) {
        if (S.games_total > S.ngames) snprintf(buf, sizeof buf, "%d of %d waiting", S.ngames, S.games_total);
        else snprintf(buf, sizeof buf, "%d waiting", S.ngames);
        ui_text_right(F_REG, LX + LW, 128, C_INK_SOFT, 15, buf);
    }
    ui_text(F_BOLD, LX + 10, 154, C_INK_SOFT, 14, "Level");
    ui_text(F_BOLD, LX + COL_SPEED, 154, C_INK_SOFT, 14, "Speed");
    ui_text(F_BOLD, LX + COL_TIME, 154, C_INK_SOFT, 14, "Time");
    ui_text(F_BOLD, LX + COL_YOU, 154, C_INK_SOFT, 14, "You play");
    if (!S.have_list) {
        ui_text(F_REG, LX + 10, 204, C_INK_SOFT, 19, net_cmg_state() == 2 ? "Loading the list..." : "Waiting for the connection...");
    } else if (!S.ngames) {
        ui_text(F_BOLD, LX + 10, 204, C_INK, 19, "Nobody is waiting right now.");
        ui_text(F_REG, LX + 10, 232, C_INK_SOFT, 18, "Create a game and a player can join you.");
    }
    if (S.list_sel < S.list_top) S.list_top = S.list_sel;
    if (S.list_sel >= S.list_top + ROWS_VIS) S.list_top = S.list_sel - ROWS_VIS + 1;
    for (i = S.list_top, y = ROW_Y0; i < S.ngames && i < S.list_top + ROWS_VIS; i++, y += ROW_STEP) {
        const cmg_open_game *g = &S.games[i];
        int li = level_index(g->level), you;
        if (!form && S.list_sel == i) ui_sel_ring(LX, y, LW, ROW_H, 12, C_GOLD, 0);
        ui_round(LX, y, LW, ROW_H, 12, C_FIELD);
        ui_piece_tinted(LEVEL_PIECE[li], LX + 12, y + 8, 30, LEVEL_COLOR[li]);
        ui_text(F_BOLD, LX + COL_SPEED, y + 30, C_INK, 17, speed_name(g->minutes, g->increment));
        snprintf(buf, sizeof buf, "%d+%d", g->minutes, g->increment);
        ui_text(F_XBOLD, LX + COL_TIME, y + 30, C_INK, 19, buf);
        you = !strcmp(g->color, "white") ? 2 : !strcmp(g->color, "black") ? 1 : 0; /* the creator's colour */
        if (you) {
            ui_piece_scaled(COLOR_PIECE[you], LX + COL_YOU + 4, y + 8, 30);
        } else {
            ui_piece_scaled(CH_PIECE(CH_WHITE, CH_KING), LX + COL_YOU - 4, y + 8, 30);
            ui_piece_scaled(CH_PIECE(CH_BLACK, CH_KING), LX + COL_YOU + 10, y + 8, 30);
        }
        /* 0.8.0: 2 px nearer the kings and cut 4 px before the Join button instead of running under it (0.7.0:
         * French "Au hasard" did) */
        ui_text_fit(F_REG, LX + COL_YOU + 42, y + 29, C_INK_SOFT, 15, LW - 84 - COL_YOU - 46, COLOR_NAMES[you]);
        ui_button(LX + LW - 84, y + 7, 76, 32, C_CYAN, C_WHITE, "Join", 0);
    }
    if (S.join_pending) ui_text(F_BOLD, LX, 500, C_CYAN_DARK, 18, "Joining the game...");

    /* create */
    if (S.open_id[0]) {
        ui_text(F_XBOLD, RX, 128, C_CYAN_DARK, 22, "Waiting for a player");
        waiting_dots(RX + ui_text_w(F_XBOLD, 22, "Waiting for a player") + 30, 121);
    } else {
        ui_text(F_XBOLD, RX, 128, C_INK, 22, "Create a game");
    }
    snprintf(buf, sizeof buf, "%d min", S.minutes);
    ui_slider(RX, RSL1_Y, RW, "Minutes", buf, S.minutes, 1, 30, form && S.form_sel == RF_MIN);
    snprintf(buf, sizeof buf, "%d s", S.increment);
    ui_slider(RX, RSL2_Y, RW, "Increment per move", buf, S.increment, 0, 10, form && S.form_sel == RF_INC);
    ui_text(F_BOLD, RX, LV_LABEL_Y, C_INK, 18, "Opponent level");
    for (i = 0; i < 3; i++)
        ui_choice_tint(sq_x(i), LV_Y, SQ, SQ, C_CYAN, LEVEL_PIECE[i], LEVEL_NAMES[i], S.level == i,
                       form && S.form_sel == RF_LEVEL && S.level == i, LEVEL_COLOR[i]);
    ui_text(F_BOLD, RX, CL_LABEL_Y, C_INK, 18, "Your colour");
    for (i = 0; i < 3; i++)
        ui_choice(sq_x(i), CL_Y, SQ, SQ, C_CYAN, COLOR_PIECE[i], COLOR_NAMES[i], S.color == i,
                  form && S.form_sel == RF_COLOR && S.color == i);
    if (S.open_id[0] || S.create_pending)
        ui_button(RX, CREATE_Y, RW, CREATE_H, C_INK_SOFT, C_WHITE, S.create_pending ? "Creating..." : "Cancel waiting",
                  form && S.form_sel == RF_CREATE);
    else
        ui_button(RX, CREATE_Y, RW, CREATE_H, C_GOLD, C_INK, "Create game", form && S.form_sel == RF_CREATE);
    {
        int x = RMX + 8;
        x = ui_hint(G_OK, x, 538, form ? "Choose" : "Join");
        x = ui_hint(G_BACK, x, 538, S.open_id[0] ? "Cancel" : "Back");
        ui_text(F_BOLD, x, 538, C_ON_DARK_SOFT, 17, "L / R  switch side");
    }
}

static void random_activate(void)
{
    char buf[200];
    if (S.focus == 0) {
        if (!S.ngames || S.join_pending || S.create_pending || S.open_id[0]) {
            if (S.open_id[0]) note("Cancel your own waiting game first");
            return;
        }
        cmg_build_join_open(buf, sizeof buf, S.games[S.list_sel].id);
        if (!send_json(buf)) {
            S.join_pending = 1;
            S.tc_min = S.games[S.list_sel].minutes;
            S.tc_inc = S.games[S.list_sel].increment;
        }
        return;
    }
    if (S.form_sel != RF_CREATE) return;
    if (S.open_id[0]) {
        cmg_build_cancel_open(buf, sizeof buf, S.open_id);
        send_json(buf);
    } else if (!S.create_pending && !S.join_pending) {
        cmg_build_create_open(buf, sizeof buf, S.minutes, S.increment, LEVELS[S.level], COLORS[S.color]);
        if (!send_json(buf)) {
            S.create_pending = 1;
            S.tc_min = S.minutes;
            S.tc_inc = S.increment;
        }
    }
}

static void random_value(int row, int d)
{
    if (row == RF_MIN) S.minutes = clampi(S.minutes + d, 1, 30);
    else if (row == RF_INC) S.increment = clampi(S.increment + d, 0, 10);
    else if (row == RF_LEVEL) S.level = step(S.level, d, 0, 2);
    else if (row == RF_COLOR) S.color = step(S.color, d, 0, 2);
}

static int random_back(void)
{
    char buf[120];
    if (S.open_id[0]) {
        cmg_build_cancel_open(buf, sizeof buf, S.open_id);
        send_json(buf);
        return 0;
    }
    S.view = GV_NONE; /* the lobby connection stays for the home screen */
    return 1;
}

static int random_input(const plat_input *in)
{
    int i;
    if (in->pressed & (BTN_L | BTN_R)) S.focus ^= 1;
    if (in->pressed & BTN_BACK) return random_back();
    if (S.focus == 0) {
        if ((in->pressed & BTN_UP) && S.list_sel > 0) S.list_sel--;
        if ((in->pressed & BTN_DOWN) && S.list_sel + 1 < S.ngames) S.list_sel++;
        if (in->pressed & BTN_RIGHT) S.focus = 1;
    } else {
        if (in->pressed & BTN_UP) S.form_sel = (S.form_sel + RF_ROWS - 1) % RF_ROWS;
        if (in->pressed & BTN_DOWN) S.form_sel = (S.form_sel + 1) % RF_ROWS;
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) {
            int d = (in->pressed & BTN_RIGHT) ? 1 : -1;
            if (S.form_sel == RF_CREATE) {
                if (d < 0) S.focus = 0;
            } else {
                random_value(S.form_sel, d);
            }
        }
    }
    if (in->pressed & BTN_OK) {
        if (S.focus == 1 && S.form_sel != RF_CREATE) S.form_sel++;
        else random_activate();
    }
    if (in->touch_up) {
        int x = in->tx, y = in->ty;
        if (ui_modal_close_hit(x, y, RMX, MY, RMW)) return random_back();
        for (i = S.list_top; i < S.ngames && i < S.list_top + ROWS_VIS; i++)
            if (ui_hit(x, y, LX, ROW_Y0 + (i - S.list_top) * ROW_STEP, LW, ROW_H)) {
                S.focus = 0;
                S.list_sel = i;
                random_activate();
                return 0;
            }
        if (ui_slider_hit(x, y, RX, RSL1_Y, RW)) {
            S.focus = 1;
            S.form_sel = RF_MIN;
            S.minutes = ui_slider_value(x, RX, RW, 1, 30);
        } else if (ui_slider_hit(x, y, RX, RSL2_Y, RW)) {
            S.focus = 1;
            S.form_sel = RF_INC;
            S.increment = ui_slider_value(x, RX, RW, 0, 10);
        }
        for (i = 0; i < 3; i++) {
            if (ui_hit(x, y, sq_x(i), LV_Y, SQ, SQ)) {
                S.focus = 1;
                S.form_sel = RF_LEVEL;
                S.level = i;
            }
            if (ui_hit(x, y, sq_x(i), CL_Y, SQ, SQ)) {
                S.focus = 1;
                S.form_sel = RF_COLOR;
                S.color = i;
            }
        }
        if (ui_hit(x, y, RX, CREATE_Y, RW, CREATE_H + 4)) {
            S.focus = 1;
            S.form_sel = RF_CREATE;
            random_activate();
        }
    }
    return 0;
}

/* ---------- play a friend ---------- */
#define FLX 64
#define FLW 380
#define FRX 488
#define FRW 408
#define FIELD_Y 206
#define JOIN_Y 296
#define FTILE_Y 290
#define MAKE_Y 380
#define FTILE_W ((FRW - 16) / 3)

static int friend_busy(void)
{
    return S.priv_waiting || S.priv_action;
}

static void draw_friend(void)
{
    char buf[64];
    int i, busy = friend_busy();
    ui_modal(MX, MY, MW, MH, "Play a friend", CH_PIECE(CH_WHITE, CH_KING), 0);
    modal_status(MX, MW);
    ui_rect(FLX + FLW + 21, MY + 92, 2, MH - 112, C_FIELD_LINE);

    /* join with a friend's code */
    ui_text(F_XBOLD, FLX, 128, C_INK, 22, "Join a friend");
    ui_text_wrap(F_REG, FLX, 156, C_INK_SOFT, 17, FLW, 22, "Type in the code your friend made, then join their game.");
    if (S.friend_sel == FF_CODE) ui_sel_ring(FLX, FIELD_Y, FLW, 70, 14, C_GOLD, 0);
    ui_round(FLX, FIELD_Y, FLW, 70, 14, C_FIELD_LINE);
    ui_round(FLX + 2, FIELD_Y + 2, FLW - 4, 66, 12, C_FIELD);
    if (S.join_code[0]) ui_text_center(F_XBOLD, FLX + FLW / 2, FIELD_Y + 50, C_INK, 40, S.join_code);
    else ui_text_center(F_BOLD, FLX + FLW / 2, FIELD_Y + 42, C_INK_SOFT, 19, "Tap here to type the code");
    ui_button(FLX, JOIN_Y, FLW, 54, S.join_code[0] ? C_CYAN : ui_mix(C_CYAN, C_WHITE, 0.45f), C_WHITE,
              S.priv_action == 2 ? "Joining..." : "Join game", S.friend_sel == FF_JOIN);
    if (S.priv_action == 2) waiting_dots(FLX + FLW / 2, JOIN_Y + 84);
    else if (S.join_err[0]) { /* 1.0.0: the server's answer to a refused code, under the button */
        if (ui_text_w(F_BOLD, 17, S.join_err) <= FLW) ui_text_center(F_BOLD, FLX + FLW / 2, JOIN_Y + 92, C_RED, 17, S.join_err);
        else ui_text_wrap(F_BOLD, FLX, JOIN_Y + 92, C_RED, 17, FLW, 22, S.join_err);
    }

    /* make a code */
    if (S.priv_waiting && S.priv_code[0]) {
        ui_text(F_XBOLD, FRX, 128, C_INK, 22, "Your game code");
        ui_round(FRX, 140, FRW, 196, 18, C_FIELD);
        ui_text_center(F_BOLD, FRX + FRW / 2, 178, C_INK_SOFT, 18, "Send this code to your friend");
        ui_text_center(F_XBOLD, FRX + FRW / 2, 262, C_CYAN_DARK, 64, S.priv_code);
        ui_text_center(F_REG, FRX + FRW / 2, 304, C_INK_SOFT, 16, "The game starts when they enter it.");
        waiting_dots(FRX + FRW / 2, 324);
        ui_button(FRX, MAKE_Y, FRW, 54, C_INK_SOFT, C_WHITE, "Cancel the code", S.friend_sel == FF_MAKE);
    } else {
        ui_text(F_XBOLD, FRX, 128, C_INK, 22, "Make a code");
        snprintf(buf, sizeof buf, "%d min", S.minutes);
        ui_slider(FRX, SL1_Y, FRW, "Minutes", buf, S.minutes, 1, 30, S.friend_sel == FF_MIN);
        snprintf(buf, sizeof buf, "%d s", S.increment);
        ui_slider(FRX, SL2_Y + 4, FRW, "Increment per move", buf, S.increment, 0, 10, S.friend_sel == FF_INC);
        ui_text(F_BOLD, FRX, FTILE_Y - 10, C_INK, 18, "Your colour");
        for (i = 0; i < 3; i++)
            ui_choice(FRX + i * (FTILE_W + 8), FTILE_Y, FTILE_W, TILE_H, C_CYAN, COLOR_PIECE[i], COLOR_NAMES[i],
                      S.color == i, S.friend_sel == FF_COLOR && S.color == i);
        ui_button(FRX, MAKE_Y, FRW, 54, C_GOLD, C_INK, S.priv_action == 1 ? "Making a code..." : "Make a code",
                  S.friend_sel == FF_MAKE);
        ui_text_wrap(F_REG, FRX, 468, C_INK_SOFT, 16, FRW, 21,
                     "Send the code to your friend: the game starts as soon as they join.");
    }
    (void)busy;
    {
        int x = MX + 8;
        x = ui_hint(G_OK, x, 538, "Choose");
        x = ui_hint(G_BACK, x, 538, busy ? "Cancel" : "Back");
        ui_text(F_BOLD, x, 538, C_ON_DARK_SOFT, 17, "L / R  switch side");
    }
}

static void friend_type_code(void)
{
    if (friend_busy() || plat_ime_active()) return;
    S.friend_sel = FF_CODE;
    plat_ime_start(tr("Your friend's game code"), S.join_code, 12, 0); /* the keyboard's title, translated */
}

static void friend_join(void)
{
    if (friend_busy()) return;
    if (!S.join_code[0]) {
        friend_type_code();
        return;
    }
    snprintf(S.priv_code, sizeof S.priv_code, "%s", S.join_code);
    S.join_err[0] = 0;
    S.priv_action = 2;
    S.tc_min = S.tc_inc = 0; /* a friend's game: its time control comes from the server's clock */
    S.priv_waiting = 0;
    net_cmg_close();
    connect_for(GT_PRIVATE);
}

static void friend_make(void)
{
    if (S.priv_waiting || S.priv_action == 1) { /* cancel: nobody joined yet, dropping the connection drops it */
        to_lobby(GV_FRIEND);
        S.friend_sel = FF_MAKE;
        return;
    }
    if (S.priv_action) return;
    S.priv_action = 1;
    S.tc_min = S.minutes;
    S.tc_inc = S.increment;
    net_cmg_close();
    connect_for(GT_PRIVATE);
}

void guest_ime_result(const char *text)
{
    size_t i, k = 0;
    char code[16];
    for (i = 0; text[i] && k + 1 < sizeof code; i++) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) code[k++] = c;
    }
    code[k] = 0;
    if (strcmp(S.join_code, code)) S.join_err[0] = 0; /* a new code: the last one's error no longer applies */
    snprintf(S.join_code, sizeof S.join_code, "%s", code);
    if (k) S.friend_sel = FF_JOIN;
}

static int friend_back(void)
{
    if (friend_busy()) {
        to_lobby(GV_FRIEND);
        return 0;
    }
    S.view = GV_NONE;
    return 1;
}

static int friend_input(const plat_input *in)
{
    int left = S.friend_sel == FF_CODE || S.friend_sel == FF_JOIN, waiting = S.priv_waiting && S.priv_code[0];
    if (in->pressed & BTN_BACK) return friend_back();
    if (in->pressed & (BTN_L | BTN_R)) S.friend_sel = left ? (waiting ? FF_MAKE : FF_MIN) : FF_CODE;
    else if (in->pressed & (BTN_UP | BTN_DOWN)) {
        int d = (in->pressed & BTN_DOWN) ? 1 : -1;
        if (left) S.friend_sel = S.friend_sel == FF_CODE ? FF_JOIN : FF_CODE;
        else if (!waiting) S.friend_sel = (S.friend_sel + d + 4) % 4; /* FF_MIN..FF_MAKE */
    } else if (in->pressed & (BTN_LEFT | BTN_RIGHT)) {
        int d = (in->pressed & BTN_RIGHT) ? 1 : -1;
        if (S.friend_sel == FF_MIN) S.minutes = clampi(S.minutes + d, 1, 30);
        else if (S.friend_sel == FF_INC) S.increment = clampi(S.increment + d, 0, 10);
        else if (S.friend_sel == FF_COLOR) S.color = step(S.color, d, 0, 2);
        else if (S.friend_sel == FF_MAKE && d < 0) S.friend_sel = FF_JOIN;
        else if (left && d > 0) S.friend_sel = waiting ? FF_MAKE : FF_MIN;
    }
    if (in->pressed & BTN_OK) {
        switch (S.friend_sel) {
        case FF_CODE: friend_type_code(); break;
        case FF_JOIN: friend_join(); break;
        case FF_MAKE: friend_make(); break;
        default: S.friend_sel++; break;
        }
    }
    if (in->touch_up) {
        int x = in->tx, y = in->ty, i;
        if (ui_modal_close_hit(x, y, MX, MY, MW)) return friend_back();
        if (ui_hit(x, y, FLX, FIELD_Y, FLW, 70)) friend_type_code();
        else if (ui_hit(x, y, FLX, JOIN_Y, FLW, 58)) {
            S.friend_sel = FF_JOIN;
            friend_join();
        } else if (ui_hit(x, y, FRX, MAKE_Y, FRW, 58)) {
            S.friend_sel = FF_MAKE;
            friend_make();
        } else if (!waiting) {
            if (ui_slider_hit(x, y, FRX, SL1_Y, FRW)) {
                S.friend_sel = FF_MIN;
                S.minutes = ui_slider_value(x, FRX, FRW, 1, 30);
            } else if (ui_slider_hit(x, y, FRX, SL2_Y + 4, FRW)) {
                S.friend_sel = FF_INC;
                S.increment = ui_slider_value(x, FRX, FRW, 0, 10);
            }
            for (i = 0; i < 3; i++)
                if (ui_hit(x, y, FRX + i * (FTILE_W + 8), FTILE_Y, FTILE_W, TILE_H)) {
                    S.friend_sel = FF_COLOR;
                    S.color = i;
                }
        }
    }
    return 0;
}

/* ---------- online game ---------- */
static void fmt_clock(int64_t ms, char *out, size_t n)
{
    int64_t s;
    if (ms < 0) ms = 0;
    s = ms / 1000;
    if (ms < 10000) snprintf(out, n, "%d.%d", (int)s, (int)((ms % 1000) / 100));
    else if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    else snprintf(out, n, "%d:%02d", (int)(s / 60), (int)(s % 60));
}

static int can_move(void)
{
    return S.in_game && S.open && !S.resume_in_flight && cmg_game_my_turn(&S.game) && !S.has_pending;
}

static void submit(chess_move m)
{
    char buf[200];
    cmg_build_move(buf, sizeof buf, S.game.game_id, m);
    if (send_json(buf)) return;
    S.game.awaiting_move = 1;
    S.pending = m;
    S.has_pending = 1;
}

enum { MI_TB_OFFER, MI_TB_CANCEL, MI_TB_ACCEPT, MI_TB_DECLINE, MI_RM_OFFER, MI_RM_CANCEL, MI_RM_ACCEPT, MI_RM_DECLINE,
       MI_RESIGN, MI_LEAVE, MI_CLOSE };
static const char *MENU_TEXT[] = {"Ask for a takeback", "Cancel the takeback request", "Accept the takeback",
                                  "Decline the takeback", "Offer a rematch", "Cancel the rematch offer",
                                  "Accept the rematch", "Decline the rematch", "Resign", "Leave the game", "Close"};
static int menu[12];

static int build_menu(void)
{
    int n = 0;
    cmg_game *g = &S.game;
    if (!g->finished) {
        if (g->takeback_in) {
            menu[n++] = MI_TB_ACCEPT;
            menu[n++] = MI_TB_DECLINE;
        } else if (g->takeback_out) {
            menu[n++] = MI_TB_CANCEL;
        } else if (g->nmoves > 0) {
            menu[n++] = MI_TB_OFFER;
        }
        menu[n++] = MI_RESIGN;
    } else if (S.open) {
        if (g->rematch_in) {
            menu[n++] = MI_RM_ACCEPT;
            menu[n++] = MI_RM_DECLINE;
        } else if (g->rematch_out) {
            menu[n++] = MI_RM_CANCEL;
        } else {
            menu[n++] = MI_RM_OFFER;
        }
    }
    menu[n++] = MI_LEAVE;
    menu[n++] = MI_CLOSE;
    return n;
}

/* returns to the screen the game came from (random or friend), over a fresh lobby connection */
static void leave_game(void)
{
    char buf[200];
    if (S.open && S.game.game_id[0]) {
        cmg_build_simple(buf, sizeof buf, "leave_game", S.game.game_id);
        if (!send_json(buf) && !net_cmg_flush(400)) plat_log("online: leave_game may not have gone out before closing");
    }
    if (!S.game.finished && S.game.token[0]) {
        /* intentional departure only: the server's leave endpoint frees our slot */
        cmg_build_leave_beacon(buf, sizeof buf, S.game.token);
        net_request_url(GUEST_REQ_LEAVE, "POST", CMG_LEAVE_URL, buf, 2);
        memset(buf, 0, sizeof buf);
    }
    plat_log("online: left game %s on purpose", S.game.game_id);
    if (!S.game.finished && S.game.have_state) record_game(1);
    to_lobby(S.came_from ? S.came_from : GV_RANDOM);
}

static void menu_do(int item)
{
    char buf[200];
    const char *gid = S.game.game_id;
    S.menu_open = 0;
    switch (item) {
    case MI_TB_OFFER: cmg_build_simple(buf, sizeof buf, "takeback_offer", gid); send_json(buf); break;
    case MI_TB_CANCEL: cmg_build_simple(buf, sizeof buf, "takeback_cancel", gid); send_json(buf); break;
    case MI_TB_ACCEPT: cmg_build_response(buf, sizeof buf, "takeback_response", gid, 1); send_json(buf); break;
    case MI_TB_DECLINE: cmg_build_response(buf, sizeof buf, "takeback_response", gid, 0); send_json(buf); break;
    case MI_RM_OFFER: cmg_build_simple(buf, sizeof buf, "rematch_offer", gid); send_json(buf); break;
    case MI_RM_CANCEL: cmg_build_simple(buf, sizeof buf, "rematch_cancel", gid); send_json(buf); break;
    case MI_RM_ACCEPT: cmg_build_response(buf, sizeof buf, "rematch_response", gid, 1); send_json(buf); break;
    case MI_RM_DECLINE: cmg_build_response(buf, sizeof buf, "rematch_response", gid, 0); send_json(buf); break;
    case MI_RESIGN: S.confirm = 1; break;
    case MI_LEAVE:
        if (S.game.finished) leave_game();
        else S.confirm = 2;
        break;
    default: break;
    }
}

static void player_bar(float y, int color, int is_turn, const char *name, const char *sub)
{
    char buf[24];
    int64_t ms = cmg_game_clock(&S.game, color, S.now);
    if (S.game.clock.valid) fmt_clock(ms, buf, sizeof buf);
    ui_player_bar(y, color, name, sub, S.game.clock.valid ? buf : NULL, is_turn, ms < 20000);
}

/* the takeback button: what it does right now (-1: nothing) */
static int takeback_item(void)
{
    cmg_game *g = &S.game;
    if (g->finished || !S.open) return -1;
    if (g->takeback_in) return MI_TB_ACCEPT;
    if (g->takeback_out) return MI_TB_CANCEL;
    return g->nmoves > 0 ? MI_TB_OFFER : -1;
}

static void draw_game(void)
{
    chess_pos shown = S.game.pos;
    cmg_game *g = &S.game;
    const char *items[12];
    int i, me = g->my_color, opp = me ^ 1, running, lf = g->last_from, lt = g->last_to, tb = takeback_item(), dots = 0;
    char buf[160], notebuf[160];
    unsigned sc = C_PANEL_HI;
    if (S.has_pending) {
        chess_make(&shown, S.pending);
        lf = S.pending.from;
        lt = S.pending.to;
    }
    boardui_draw(&S.bu, &shown, lf, lt, me, !g->have_state || !S.open);

    running = g->have_state && !g->finished;
    player_bar(16, opp, running && g->clock.turn == opp, "Opponent",
               g->opponent_away && !g->finished ? "Disconnected" : "Guest");

    if (g->finished) {
        snprintf(buf, sizeof buf, "%s", g->result);
        sc = C_GOLD;
    } else if (!S.open && S.retry_at > S.now) {
        snprintf(buf, sizeof buf, "Connection lost: retrying in %d s", (int)((S.retry_at - S.now + 999) / 1000));
        sc = C_RED;
    } else if (!g->have_state) {
        /* the first connection to a new game: it opens and resumes before the first state, so this comes before
         * "Reconnecting", which is for a connection lost during the game (2026-11-12: each new game showed the red
         * "Reconnecting to the game" for a moment) */
        snprintf(buf, sizeof buf, "Loading the game");
        dots = 1;
    } else if (!S.open || S.resume_in_flight) {
        snprintf(buf, sizeof buf, "Reconnecting to the game");
        sc = C_RED;
        dots = 1;
    } else if (S.has_pending) {
        snprintf(buf, sizeof buf, "Sending your move");
        dots = 1;
    } else if (g->pos.side == me) {
        snprintf(buf, sizeof buf, chess_in_check(&g->pos, me) ? "Your move: you're in check" : "Your move");
        sc = chess_in_check(&g->pos, me) ? C_RED : C_CYAN;
    } else {
        snprintf(buf, sizeof buf, "Waiting for your opponent");
        dots = 1;
    }
    ui_status_strip(96, buf, sc, dots, S.now);

    notebuf[0] = 0;
    if (S.note[0] && S.now < S.note_until) snprintf(notebuf, sizeof notebuf, "%s", S.note);
    else if (g->opponent_away && !g->finished)
        snprintf(notebuf, sizeof notebuf, "Opponent disconnected: waiting up to %d s", g->grace_ms / 1000);
    else if (g->takeback_in) snprintf(notebuf, sizeof notebuf, "Your opponent asks for a takeback");
    else if (g->rematch_in) snprintf(notebuf, sizeof notebuf, "Your opponent offers a rematch");
    else if (g->takeback_out) snprintf(notebuf, sizeof notebuf, "Takeback asked: waiting for an answer");
    else if (g->rematch_out) snprintf(notebuf, sizeof notebuf, "Rematch offered: waiting for an answer");
    ui_moves_box(146, 234, (const char(*)[12])g->san, g->nmoves, g->have_state ? "No moves yet" : "Loading the game...",
                 notebuf[0] ? notebuf : NULL);
    /* while the opponent asks for a takeback the middle button declines it; Resign stays in the menu */
    ui_game_buttons(tb == MI_TB_ACCEPT ? "Accept" : tb == MI_TB_CANCEL ? "Cancel" : tb >= 0 ? "Takeback" : NULL,
                    tb == MI_TB_ACCEPT ? "Decline" : running ? "Resign" : NULL, "Menu");
    player_bar(444, me, running && g->clock.turn == me, "You", me == CH_WHITE ? "White" : "Black");

    if (g->finished && !S.menu_open && !S.confirm) {
        const char *head = g->winner < 0 ? "Draw" : g->winner == me ? "You won!" : "You lost";
        unsigned col = g->winner < 0 ? C_CYAN_DARK : g->winner == me ? C_GREEN : C_RED;
        ui_result_draw(head, col, g->result, g->rematch_out ? "Offered" : g->rematch_in ? "Accept" : "Rematch", "Done",
                       S.result_sel, S.stats_line, S.stats_c);
    }
    if (S.menu_open) {
        int n = build_menu();
        for (i = 0; i < n; i++) items[i] = MENU_TEXT[menu[i]];
        ui_menu_draw("Game menu", items, n, S.menu_sel);
    }
    if (S.confirm)
        ui_confirm_draw(S.confirm == 1 ? "Resign this game?" : "Leave this game?",
                        S.confirm == 2 ? "Leaving gives your opponent the win." : "Your opponent gets the win.",
                        S.confirm == 1 ? "Resign" : "Leave", "Keep playing");
    {
        int x = 552;
        if (g->finished) {
            ui_hint(G_OK, x, 538, "Choose");
        } else {
            x = ui_hint(G_OK, x, 538, "Pick / drop");
            ui_hint(G_TRIANGLE, x, 538, "Menu");
        }
    }
}

static void game_input(const plat_input *in)
{
    cmg_game *g = &S.game;
    chess_move mv;
    if (S.confirm) {
        int hit = PLAT_TAP(in, ui_confirm_hit);
        if ((in->pressed & BTN_OK) || hit == 1) {
            char buf[120];
            int what = S.confirm;
            S.confirm = 0;
            if (what == 1) {
                cmg_build_simple(buf, sizeof buf, "resign", g->game_id);
                send_json(buf);
            } else {
                leave_game();
            }
        } else if ((in->pressed & BTN_BACK) || hit == 0) {
            S.confirm = 0;
        }
        return;
    }
    if (S.menu_open) {
        int n = build_menu(), hit = PLAT_TAP(in, ui_menu_hit, n);
        if (in->pressed & BTN_UP) S.menu_sel = (S.menu_sel + n - 1) % n;
        if (in->pressed & BTN_DOWN) S.menu_sel = (S.menu_sel + 1) % n;
        if ((in->pressed & (BTN_BACK | BTN_TRIANGLE)) || hit == -2) S.menu_open = 0;
        if (in->pressed & BTN_OK) menu_do(menu[S.menu_sel % n]);
        else if (hit >= 0) menu_do(menu[hit]);
        return;
    }
    if (!boardui_modal(&S.bu)) {
        int b = PLAT_TAP(in, ui_game_button_hit), tb = takeback_item();
        if ((in->pressed & BTN_TRIANGLE) || b == 2) {
            S.menu_open = 1;
            S.menu_sel = 0;
            return;
        }
        if (b == 0 && tb >= 0) {
            menu_do(tb);
            return;
        }
        if (b == 1 && tb == MI_TB_ACCEPT) {
            menu_do(MI_TB_DECLINE);
            return;
        }
        if (b == 1 && g->have_state && !g->finished) {
            S.confirm = 1;
            return;
        }
    }
    if (g->finished && !boardui_modal(&S.bu)) {
        int act = -1;
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) S.result_sel ^= 1;
        if (in->pressed & BTN_OK) act = S.result_sel;
        if (in->pressed & BTN_BACK) act = 1;
        {
            int h = PLAT_TAP(in, ui_result_hit);
            if (h >= 0) act = h;
        }
        if (act == 0 && S.open) {
            if (g->rematch_in) menu_do(MI_RM_ACCEPT);
            else if (!g->rematch_out) menu_do(MI_RM_OFFER);
        } else if (act == 0) {
            note("Not connected: a rematch needs the game connection");
        } else if (act == 1) {
            leave_game();
        }
        return;
    }
    if (boardui_input(&S.bu, in, &g->pos, g->my_color, can_move(), &mv)) submit(mv);
}

int guest_input(const plat_input *in, uint64_t now)
{
    S.now = now;
    switch (S.view) {
    case GV_RANDOM: return random_input(in);
    case GV_FRIEND: return friend_input(in);
    case GV_GAME: game_input(in); return 0;
    default: return 1;
    }
}

int guest_lobby_view(void)
{
    return S.view == GV_RANDOM || S.view == GV_FRIEND;
}

void guest_draw(uint64_t now)
{
    S.now = now;
    switch (S.view) {
    case GV_RANDOM: draw_random(); draw_note(); break;
    case GV_FRIEND: draw_friend(); draw_note(); break;
    case GV_GAME: draw_game(); break;
    default: break;
    }
}
