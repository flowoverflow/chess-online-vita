/* Chess Online: "Play vs computer" with our own engine. Our own code; see localgame.h.
 * The engine runs on its own thread so the screen keeps drawing; a generation number drops answers that
 * arrive after a takeback or a new game. */
#include "localgame.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardui.h"
#include "chess.h"
#include "engine.h"
#include "gamelog.h"
#include "statsui.h"
#include "ui.h"

#define PLY_MAX 600
#define MIN_THINK_MS 450 /* the computer's move appears after at least this long */

static struct {
    int active, level, my_color;
    chess_pos start, pos;
    chess_move moves[PLY_MAX];
    char san[PLY_MAX][12];
    char keys[PLY_MAX + 1][80]; /* position keys for repetition */
    int nmoves;
    int finished, winner; /* winner: CH_WHITE / CH_BLACK / -1 draw */
    int end;              /* how it ended: GL_END_* */
    char result[64];
    uint64_t started;     /* for the statistics: when the game began */
    int recorded;         /* the finished game is in the statistics */
    char stats_line[96];  /* the result card's line */
    unsigned stats_c;
    boardui bu;
    int menu_open, menu_sel, confirm, result_sel;
    uint64_t now, think_started;
    /* engine thread */
    int thinking;
} L;

static pthread_mutex_t ai_lock = PTHREAD_MUTEX_INITIALIZER;
static int ai_gen, ai_ready, ai_ready_gen, ai_ok;
/* one stop flag per search (ring): a cancelled search keeps seeing its own flag set */
static volatile int ai_stop[8];
static chess_move ai_move;
static engine_info ai_info;

typedef struct {
    chess_pos pos;
    int level, gen;
    uint32_t seed;
} ai_job;

static void pos_key(const chess_pos *p, char *out, size_t n)
{
    char fen[100], a[80], b[4], c[8], d[4];
    chess_get_fen(p, fen, sizeof fen);
    sscanf(fen, "%79s %3s %7s %3s", a, b, c, d);
    snprintf(out, n, "%s %s %s %s", a, b, c, d);
}

static void *ai_main(void *arg)
{
    ai_job *j = (ai_job *)arg;
    chess_move m;
    engine_info info;
    int ok = engine_pick(&j->pos, j->level, j->seed, plat_ms, &ai_stop[j->gen & 7], &m, &info) == 0;
    pthread_mutex_lock(&ai_lock);
    if (j->gen == ai_gen) {
        ai_move = m;
        ai_info = info;
        ai_ok = ok;
        ai_ready = 1;
        ai_ready_gen = j->gen;
    }
    pthread_mutex_unlock(&ai_lock);
    free(j);
    return NULL;
}

static void cancel_ai(void)
{
    pthread_mutex_lock(&ai_lock);
    ai_stop[ai_gen & 7] = 1;
    ai_gen++;
    ai_ready = 0;
    pthread_mutex_unlock(&ai_lock);
    L.thinking = 0;
}

static void start_ai(void)
{
    pthread_attr_t at;
    pthread_t th;
    ai_job *j = (ai_job *)calloc(1, sizeof *j);
    if (!j) return;
    pthread_mutex_lock(&ai_lock);
    j->gen = ++ai_gen;
    ai_stop[j->gen & 7] = 0;
    ai_ready = 0;
    pthread_mutex_unlock(&ai_lock);
    j->pos = L.pos;
    j->level = L.level;
    j->seed = (uint32_t)(plat_ms() * 2654435761u) ^ (uint32_t)L.nmoves;
    L.thinking = 1;
    L.think_started = L.now;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, ai_main, j)) {
        free(j);
        L.thinking = 0;
        plat_log("local: couldn't start the engine thread");
    }
    pthread_attr_destroy(&at);
}

static const char *LEVEL_ABOUT[ENGINE_LEVELS] = {"Just learning the moves", "Makes lots of mistakes", "A casual player",
                                                 "Knows the basics", "A club beginner", "Careful and steady",
                                                 "Hard to beat", "Our strongest"};

const char *local_level_about(int level)
{
    return level >= 1 && level <= ENGINE_LEVELS ? LEVEL_ABOUT[level - 1] : "";
}

/* the finished game goes into the statistics once; a takeback after the end takes it out again */
static void record_finish(void)
{
    gl_game g;
    if (L.recorded) return;
    memset(&g, 0, sizeof g);
    g.mode = GL_CPU;
    g.level = L.level;
    g.white = L.my_color == CH_WHITE;
    g.result = L.winner < 0 ? GL_DRAWN : L.winner == L.my_color ? GL_WON : GL_LOST;
    g.end = L.end;
    g.moves = (L.nmoves + 1) / 2;
    g.secs = (int)((L.now - L.started) / 1000);
    L.stats_c = stats_record(&g, L.stats_line, sizeof L.stats_line);
    L.recorded = 1;
}

static void check_end(void)
{
    int st = chess_status(&L.pos), i, reps = 0;
    if (st == CH_CHECKMATE) {
        L.finished = 1;
        L.winner = L.pos.side ^ 1;
        L.end = GL_END_MATE;
        snprintf(L.result, sizeof L.result, "%s wins by checkmate", L.winner == CH_WHITE ? "White" : "Black");
    } else if (st == CH_STALEMATE) {
        L.finished = 1;
        L.winner = -1;
        L.end = GL_END_STALEMATE;
        snprintf(L.result, sizeof L.result, "Draw by stalemate");
    } else if (engine_insufficient_material(&L.pos)) {
        L.finished = 1;
        L.winner = -1;
        L.end = GL_END_MATERIAL;
        snprintf(L.result, sizeof L.result, "Draw: not enough pieces to mate");
    } else if (L.pos.halfmove >= 100) {
        L.finished = 1;
        L.winner = -1;
        L.end = GL_END_FIFTY;
        snprintf(L.result, sizeof L.result, "Draw by the 50-move rule");
    } else {
        for (i = 0; i <= L.nmoves; i++)
            if (!strcmp(L.keys[i], L.keys[L.nmoves])) reps++;
        if (reps >= 3) {
            L.finished = 1;
            L.winner = -1;
            L.end = GL_END_REPETITION;
            snprintf(L.result, sizeof L.result, "Draw by repetition");
        }
    }
    if (L.finished) {
        plat_log("local: game over: %s after %d moves", L.result, L.nmoves);
        record_finish();
    }
}

static void play(chess_move m)
{
    if (L.nmoves >= PLY_MAX) return;
    chess_move_san(&L.pos, m, L.san[L.nmoves], sizeof L.san[0]);
    L.moves[L.nmoves++] = m;
    chess_make(&L.pos, m);
    pos_key(&L.pos, L.keys[L.nmoves], sizeof L.keys[0]);
    check_end();
}

static void replay_to(int n)
{
    int i;
    L.pos = L.start;
    for (i = 0; i < n; i++) chess_make(&L.pos, L.moves[i]);
    L.nmoves = n;
    L.finished = 0;
    L.winner = -1;
    L.result[0] = 0;
}

void local_start(int level, int color_choice, uint64_t now)
{
    cancel_ai();
    memset(L.san, 0, sizeof L.san);
    L.active = 1;
    L.now = now;
    L.level = level < 1 ? 1 : level > ENGINE_LEVELS ? ENGINE_LEVELS : level;
    L.my_color = color_choice == 1 ? CH_WHITE : color_choice == 2 ? CH_BLACK : (int)(plat_ms() & 1);
    chess_startpos(&L.start);
    L.pos = L.start;
    L.nmoves = 0;
    pos_key(&L.pos, L.keys[0], sizeof L.keys[0]);
    L.finished = 0;
    L.winner = -1;
    L.result[0] = 0;
    L.started = now;
    L.recorded = 0;
    L.stats_line[0] = 0;
    L.menu_open = L.confirm = 0;
    L.result_sel = 0;
    boardui_reset(&L.bu);
    plat_log("local: new game, level %d, you play %s", L.level, L.my_color == CH_WHITE ? "white" : "black");
    if (L.pos.side != L.my_color) start_ai();
}

int local_playing(void)
{
    return L.active && !L.finished;
}

void local_update(uint64_t now)
{
    int ready, ok, gen;
    chess_move m;
    L.now = now;
    if (!L.thinking) return;
    pthread_mutex_lock(&ai_lock);
    ready = ai_ready && ai_ready_gen == ai_gen;
    ok = ai_ok;
    m = ai_move;
    gen = ai_gen;
    pthread_mutex_unlock(&ai_lock);
    if (!ready || now - L.think_started < MIN_THINK_MS) return;
    (void)gen;
    L.thinking = 0;
    if (!ok) {
        check_end();
        return;
    }
    plat_log("local: engine level %d: depth %d, %llu nodes, %d ms%s", L.level, ai_info.depth_reached,
             (unsigned long long)ai_info.nodes, (int)(now - L.think_started), ai_info.timed_out ? " (time)" : "");
    play(m);
}

static void takeback(void)
{
    int n = L.nmoves;
    cancel_ai();
    if (!n) return;
    if (L.finished && L.recorded) { /* the game goes on: it isn't finished any more */
        stats_unrecord_last();
        L.recorded = 0;
        L.stats_line[0] = 0;
    }
    /* back to the player's last turn: one ply if it's the computer's turn now, else two */
    n -= (L.pos.side == L.my_color) ? 2 : 1;
    if (n < 0) n = 0;
    replay_to(n);
    boardui_reset(&L.bu);
    if (L.pos.side != L.my_color) start_ai(); /* the computer was to open the game */
}

enum { LM_TAKEBACK, LM_RESIGN, LM_NEW, LM_MENU, LM_CLOSE };
static const char *LM_TEXT[] = {"Take back a move", "Resign", "New game", "Back to the home screen", "Close"};
static int lmenu[6];

static int build_menu(void)
{
    int n = 0;
    if (L.nmoves > 0) lmenu[n++] = LM_TAKEBACK;
    if (!L.finished) lmenu[n++] = LM_RESIGN;
    lmenu[n++] = LM_NEW;
    lmenu[n++] = LM_MENU;
    lmenu[n++] = LM_CLOSE;
    return n;
}

static int menu_do(int item)
{
    L.menu_open = 0;
    switch (item) {
    case LM_TAKEBACK: takeback(); break;
    case LM_RESIGN: L.confirm = 1; break;
    case LM_NEW: local_start(L.level, L.my_color == CH_WHITE ? 1 : 2, L.now); break;
    case LM_MENU:
        cancel_ai();
        L.active = 0;
        return 1;
    default: break;
    }
    return 0;
}

int local_input(const plat_input *in, uint64_t now)
{
    chess_move m;
    L.now = now;
    if (L.confirm) {
        int hit = PLAT_TAP(in, ui_confirm_hit);
        if ((in->pressed & BTN_OK) || hit == 1) {
            cancel_ai();
            L.confirm = 0;
            L.finished = 1;
            L.winner = L.my_color ^ 1;
            L.end = GL_END_RESIGN;
            snprintf(L.result, sizeof L.result, "You resigned");
            plat_log("local: you resigned after %d moves", L.nmoves);
            record_finish();
        } else if ((in->pressed & BTN_BACK) || hit == 0) {
            L.confirm = 0;
        }
        return 0;
    }
    if (L.menu_open) {
        int n = build_menu(), hit = PLAT_TAP(in, ui_menu_hit, n);
        if (in->pressed & BTN_UP) L.menu_sel = (L.menu_sel + n - 1) % n;
        if (in->pressed & BTN_DOWN) L.menu_sel = (L.menu_sel + 1) % n;
        if ((in->pressed & (BTN_BACK | BTN_TRIANGLE)) || hit == -2) L.menu_open = 0;
        if (in->pressed & BTN_OK) return menu_do(lmenu[L.menu_sel % n]);
        if (hit >= 0) return menu_do(lmenu[hit]);
        return 0;
    }
    if (!boardui_modal(&L.bu)) {
        int b = PLAT_TAP(in, ui_game_button_hit);
        if ((in->pressed & BTN_TRIANGLE) || b == 2) {
            L.menu_open = 1;
            L.menu_sel = 0;
            return 0;
        }
        if (b == 0 && !L.finished && L.nmoves > 0) {
            takeback();
            return 0;
        }
        if (b == 1 && !L.finished) {
            L.confirm = 1;
            return 0;
        }
    }
    if (L.finished) {
        int act = -1;
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) L.result_sel ^= 1;
        if (in->pressed & BTN_OK) act = L.result_sel;
        if (in->pressed & BTN_BACK) act = 1;
        {
            int h = PLAT_TAP(in, ui_result_hit);
            if (h >= 0) act = h;
        }
        if (act == 0) local_start(L.level, L.my_color == CH_WHITE ? 1 : 2, now);
        else if (act == 1) {
            L.active = 0;
            return 1;
        }
        return 0;
    }
    if (boardui_input(&L.bu, in, &L.pos, L.my_color, !L.thinking && L.pos.side == L.my_color, &m)) {
        play(m);
        if (!L.finished) start_ai();
    }
    return 0;
}

void local_draw(uint64_t now)
{
    char buf[96], sub[48];
    const char *items[6];
    int me = L.my_color, opp = me ^ 1, i, lf = -1, lt = -1;
    unsigned sc;
    L.now = now;
    if (L.nmoves) {
        lf = L.moves[L.nmoves - 1].from;
        lt = L.moves[L.nmoves - 1].to;
    }
    boardui_draw(&L.bu, &L.pos, lf, lt, me, 0);
    snprintf(sub, sizeof sub, "Level %d of %d", L.level, ENGINE_LEVELS);
    ui_player_bar(16, opp, "Computer", sub, NULL, !L.finished && L.pos.side == opp, 0);

    if (L.finished) {
        snprintf(buf, sizeof buf, "%s", L.result);
        sc = C_GOLD;
    } else if (L.thinking) {
        snprintf(buf, sizeof buf, "The computer is thinking");
        sc = C_PANEL_HI;
    } else if (chess_in_check(&L.pos, me)) {
        snprintf(buf, sizeof buf, "Your move: you're in check");
        sc = C_RED;
    } else {
        snprintf(buf, sizeof buf, "Your move");
        sc = C_CYAN;
    }
    ui_status_strip(96, buf, sc, L.thinking, now);
    ui_moves_box(146, 234, (const char(*)[12])L.san, L.nmoves,
                 L.pos.side == me ? "You start: make the first move" : "The computer starts", NULL);
    ui_game_buttons(!L.finished && L.nmoves ? "Take back" : NULL, !L.finished ? "Resign" : NULL, "Menu");
    ui_player_bar(444, me, "You", me == CH_WHITE ? "White" : "Black", NULL, !L.finished && L.pos.side == me, 0);

    if (L.finished && !L.menu_open && !L.confirm) {
        const char *head = L.winner < 0 ? "Draw" : L.winner == me ? "You won!" : "You lost";
        unsigned col = L.winner < 0 ? C_CYAN_DARK : L.winner == me ? C_GREEN : C_RED;
        ui_result_draw(head, col, L.result, "Play again", "Menu", L.result_sel, L.stats_line, L.stats_c);
    }
    if (L.menu_open) {
        int n = build_menu();
        for (i = 0; i < n; i++) items[i] = LM_TEXT[lmenu[i]];
        ui_menu_draw("Game menu", items, n, L.menu_sel);
    }
    if (L.confirm) ui_confirm_draw("Resign this game?", "The computer gets the win.", "Resign", "Keep playing");
    {
        int x = 552;
        if (L.finished) {
            ui_hint(G_OK, x, 538, "Choose");
        } else {
            x = ui_hint(G_OK, x, 538, "Pick / drop");
            ui_hint(G_TRIANGLE, x, 538, "Menu");
        }
    }
}
