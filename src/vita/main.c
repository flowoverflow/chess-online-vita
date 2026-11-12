/* Chess Online: a PS Vita chess app. Online games are anonymous guest games on Coolmath's chess server
 * (wss://chess1.coolmathgames.com); "Play vs computer" uses our own engine on the console.
 * Our own code and design; personal use only. See docs/chessonline.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/system_param.h>
#include <vita2d.h>

#include "boardui.h"
#include "chess.h"
#include "engine.h"
#include "guest.h"
#include "i18n.h"
#include "langui.h"
#include "localgame.h"
#include "net.h"
#include "platform.h"
#include "statsui.h"
#include "tips.h"
#include "ui.h"
#include "updateui.h"

/* a test build can pretend to be older with -DCO_VERSION=0.9.9 (scripts/chessonline/build.sh, CO_VERSION=...) */
#ifndef APP_VERSION
#define APP_VERSION "1.0.1"
#endif
#define SPLASH_MS 1800
/* vita2d's vertex pool: three 1 MB regions, one per frame the GPU may still hold (see the render loop) */
#define POOL_BYTES (3u * 1024u * 1024u)
#define POOL_REGION (POOL_BYTES / 3)
#ifndef CO_MSAA
#define CO_MSAA 1 /* 4x multisampling; 0 builds the old plain vita2d_init for comparison */
#endif

typedef enum { SC_HOME, SC_AI_SETUP, SC_LOCAL, SC_ONLINE, SC_ABOUT, SC_TIPS, SC_STATS, SC_LANG } screen_t;

static const char *COLOR_NAMES[] = {"Random", "White", "Black"};

static struct {
    screen_t screen;
    plat_input in;
    uint64_t now;
    char *ca;
    size_t ca_len;
    int sel;               /* home: 0 random, 1 computer, 2 friend, 3-4 tip cards, 5 all tips, 6 your stats, 7 language */
    int last_pill;
    chess_pos startpos;
    vita2d_texture *splash;
    vita2d_texture *logo56, *logo140; /* the user's logo at its two sizes (home header, About), drawn 1:1 */
    vita2d_texture *wordmark;         /* Home's title: the user's outlined CHESS ONLINE on one line, 310 px */
    vita2d_texture *wordstack, *logo90; /* Home's header: the stacked CHESS / ONLINE and the logo, both 90 px tall */
    uint64_t start;
    int ai_level, ai_color, ai_sel;
    int lang_chosen; /* a language was picked (kept in settings.txt); until then the Vita's own language */
    int names_warmed; /* the languages' own names are in their fonts (lang_warm_names, once) */
    unsigned frames, pool_peak; /* for the gfx log lines */
    uint64_t fps_t0, t_draw, t_swap; /* microseconds in the current fps window */
    int fps_logged;
} A;

extern unsigned co_text_pad_size(void);

/* ---------- settings ---------- */
/* the Vita's own language (since 0.8.0 the app has every one the Vita offers; Portugal shares Brazil's texts, the UK
 * the US's) */
static int system_language(void)
{
    switch (plat_system_lang()) {
    case SCE_SYSTEM_PARAM_LANG_SPANISH: return I18N_ES;
    case SCE_SYSTEM_PARAM_LANG_FRENCH: return I18N_FR;
    case SCE_SYSTEM_PARAM_LANG_GERMAN: return I18N_DE;
    case SCE_SYSTEM_PARAM_LANG_ITALIAN: return I18N_IT;
    case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
    case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR: return I18N_PT;
    case SCE_SYSTEM_PARAM_LANG_DUTCH: return I18N_NL;
    case SCE_SYSTEM_PARAM_LANG_POLISH: return I18N_PL;
    case SCE_SYSTEM_PARAM_LANG_TURKISH: return I18N_TR;
    case SCE_SYSTEM_PARAM_LANG_SWEDISH: return I18N_SV;
    case SCE_SYSTEM_PARAM_LANG_DANISH: return I18N_DA;
    case SCE_SYSTEM_PARAM_LANG_NORWEGIAN: return I18N_NB;
    case SCE_SYSTEM_PARAM_LANG_FINNISH: return I18N_FI;
    case SCE_SYSTEM_PARAM_LANG_RUSSIAN: return I18N_RU;
    case SCE_SYSTEM_PARAM_LANG_JAPANESE: return I18N_JA;
    case SCE_SYSTEM_PARAM_LANG_KOREAN: return I18N_KO;
    case SCE_SYSTEM_PARAM_LANG_CHINESE_S: return I18N_ZH_HANS;
    case SCE_SYSTEM_PARAM_LANG_CHINESE_T: return I18N_ZH_HANT;
    default: return I18N_EN;
    }
}

static void load_settings(void)
{
    char *s = plat_read_file(CO_SETTINGS_FILE, NULL), *line;
    int lang = -1;
    A.ai_level = 2;
    A.ai_color = 0;
    if (s) {
        for (line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) {
            char key[32], word[16];
            int val;
            if (sscanf(line, "lang=%15s", word) == 1) {
                lang = i18n_from_code(word);
                continue;
            }
            if (sscanf(line, "%31[^=]=%d", key, &val) != 2) continue;
            if (!strcmp(key, "ai_level") && val >= 1 && val <= ENGINE_LEVELS) A.ai_level = val;
            else if (!strcmp(key, "ai_color") && val >= 0 && val <= 2) A.ai_color = val;
        }
        free(s);
    }
    A.lang_chosen = lang >= 0;
    i18n_set(lang >= 0 ? lang : system_language());
    plat_log("lang: %s (%s)", i18n_code(i18n_get()), lang >= 0 ? "chosen" : "the Vita's language");
}

static void save_settings(void)
{
    char buf[96];
    int n = snprintf(buf, sizeof buf, "ai_level=%d\nai_color=%d\n", A.ai_level, A.ai_color);
    if (A.lang_chosen) n += snprintf(buf + n, sizeof buf - (size_t)n, "lang=%s\n", i18n_code(i18n_get()));
    plat_write_file(CO_SETTINGS_FILE, buf, (size_t)n);
}

/* ---------- network messages ---------- */
static void handle_net(void)
{
    net_msg m;
    int budget = 200;
    while (budget-- > 0 && net_poll(&m)) {
        if (m.kind == NM_RESPONSE && m.tag == NET_TAG_UPDATE) update_on_response(&m, A.now);
        else if (m.kind == NM_RESPONSE) guest_on_response(&m);
        else guest_on_net(&m, A.now);
        net_msg_free(&m);
    }
}

/* ---------- HOME ---------- */
#define PILL_X 72
#define PILL_W 400
#define PILL_H 76
/* "Start a game", the three pills and the Your stats strip, 20 px apart, sit 58 px from the board's top and bottom
 * edges (the heading's capitals start at 74, the strip ends at 470) */
#define HEAD_Y 100
static const float PILL_Y[3] = {130, 226, 322};
#define STRIP_Y 418
#define STRIP_H 52
#define TIP_Y 254
#define TIP_W 176
#define TIP_H 99
static const float TIP_X[2] = {560, 752};
#define ALL_X 624
#define ALL_Y 398
#define ALL_W 240
#define ALL_H 40

static void home_activate(int item)
{
    if (item == 0) {
        A.screen = SC_ONLINE;
        guest_open(GUEST_VIEW_RANDOM);
    } else if (item == 1) {
        A.ai_sel = 0;
        A.screen = SC_AI_SETUP;
    } else if (item == 2) {
        A.screen = SC_ONLINE;
        guest_open(GUEST_VIEW_FRIEND);
    } else if (item == 6) {
        stats_open();
        A.screen = SC_STATS;
    } else if (item == 7) {
        lang_picker_open();
        A.screen = SC_LANG;
    } else {
        tips_open(item == 3 ? 0 : item == 4 ? 1 : -1);
        A.screen = SC_TIPS;
    }
}

/* the status under Home's title: its text and the colour of its dot */
static unsigned status_text(char *buf, size_t n)
{
    guest_status st;
    guest_get_status(&st);
    if (!net_wifi_connected()) snprintf(buf, n, "No Wi-Fi: vs computer still works");
    else if (st.state == 2) snprintf(buf, n, "Online as a guest");
    else if (st.state == 1) snprintf(buf, n, "Connecting%s...", st.poll ? " (HTTPS)" : "");
    else if (st.retry_in_s) snprintf(buf, n, "Offline: retrying in %d s", st.retry_in_s);
    else snprintf(buf, n, "Offline");
    return st.state == 2 ? C_GREEN : st.state == 1 ? C_GOLD : C_RED;
}

static void status_line(int x, int y)
{
    char buf[96];
    unsigned dot = status_text(buf, sizeof buf);
    ui_circle((float)x + 6, (float)y - 6, 6, dot);
    ui_text_fit(F_BOLD, x + 20, y, C_ON_DARK_SOFT, 16, 340, buf);
}

/* the same, centred on cx: the dot and its text as one */
static void status_centred(int cx, int y)
{
    char buf[96];
    int w;
    status_text(buf, sizeof buf);
    w = ui_text_w(F_BOLD, 16, buf);
    if (w > 340) w = 340;
    status_line(cx - (20 + w + 1) / 2, y);
}

static void draw_home(int active)
{
    guest_status st;
    char sub[96], buf[32];
    int i;
    ui_board g = UI_MAIN_BOARD(0);
    ui_board_state bs;
    guest_get_status(&st);

    /* left: the board waiting for a game, and the three ways to start one */
    ui_board_state_init(&bs, &A.startpos);
    ui_draw_board(&g, &bs);
    ui_board_area(ui_alpha(C_BG_BOTTOM, 196));
    ui_text_center(F_XBOLD, 272, HEAD_Y, C_WHITE, 36, "Start a game");
    if (st.state == 2 && st.open_games > 0) snprintf(sub, sizeof sub, "%d waiting now, or create a game", st.open_games);
    else snprintf(sub, sizeof sub, "Play a guest from anywhere");
    ui_menu_pill(PILL_X, PILL_Y[0], PILL_W, PILL_H, "Random opponent", sub, CH_PIECE(CH_WHITE, CH_KNIGHT),
                 active && A.sel == 0);
    snprintf(sub, sizeof sub, "Level %d of %d, %s", A.ai_level, ENGINE_LEVELS,
             A.ai_color ? (A.ai_color == 1 ? "you play white" : "you play black") : "random colour");
    ui_menu_pill(PILL_X, PILL_Y[1], PILL_W, PILL_H, "Play vs computer", sub, CH_PIECE(CH_BLACK, CH_QUEEN),
                 active && A.sel == 1);
    ui_menu_pill(PILL_X, PILL_Y[2], PILL_W, PILL_H, "Play a friend", "Share a game code", CH_PIECE(CH_WHITE, CH_KING),
                 active && A.sel == 2);
    stats_draw_strip(PILL_X, STRIP_Y, PILL_W, STRIP_H, active && A.sel == 6);

    /* right: the logo with "Chess Online" on its bottom line and the status under them; who is online, chess tips */
    ui_round(GP_X, 16, GP_W, 512, 18, C_PANEL);
    /* the box of who is online: where the header leaves it (its bottom stays at 200, so nothing under it moves) */
    int by = 114, bh = 86, bnum = 32;
    if (A.wordstack && A.logo90) {
        /* the user's pick (2026-11-12): the stacked CHESS / ONLINE and then the logo, both 90 px tall and touching,
         * centred on the panel, the status centred under them (res/wordmark-stack.png and res/logo90.png,
         * tools/chessonline/make_user_art.py; mock-ups' Header "C at B size", gap 0) */
        int ww = (int)vita2d_texture_get_width(A.wordstack), lw = (int)vita2d_texture_get_width(A.logo90);
        int x0 = 744 - (ww + lw + 1) / 2;
        vita2d_draw_texture(A.wordstack, (float)x0, 20);
        vita2d_draw_texture(A.logo90, (float)(x0 + ww), 20);
        status_centred(744, 132);
        by = 142;
        bh = 58;
        bnum = 26;
    } else if (A.logo56) {
        vita2d_draw_texture(A.logo56, 558, 20);
        int tx = 558 + (int)vita2d_texture_get_width(A.logo56) + 12;
        /* the user's design (2026-11-12): CHESS ONLINE on one line, across to the panel's edge, centred on the logo
         * (res/wordmark.png, tools/chessonline/make_user_art.py); the plain title if the picture is missing */
        if (A.wordmark) vita2d_draw_texture(A.wordmark, (float)tx, (float)(48 - (int)vita2d_texture_get_height(A.wordmark) / 2));
        else ui_text(F_XBOLD, tx, 74, C_WHITE, 30, "Chess Online");
    } else {
        ui_text(F_XBOLD, 560, 74, C_WHITE, 30, "Chess Online");
    }
    if (!(A.wordstack && A.logo90)) status_line(560, 101);
    ui_round(560, (float)by, 368, (float)bh, 14, C_PANEL_HI);
#define BOX_Y(pct) (by + (bh * (pct) + 50) / 100) /* a line at pct % of the box's height, rounded */
    if (st.state == 2 && st.have_stats) {
        static const char *labels[3] = {"online", "playing", "waiting"};
        int v[3];
        v[0] = st.players;
        v[1] = st.active_games;
        v[2] = st.open_games < 0 ? 0 : st.open_games;
        for (i = 0; i < 3; i++) {
            int cx = 560 + 61 + i * 123;
            snprintf(buf, sizeof buf, "%d", v[i]);
            ui_text_center(F_XBOLD, cx, BOX_Y(53), i == 0 ? C_CYAN_HI : C_WHITE, bnum, buf);
            ui_text_center(F_REG, cx, BOX_Y(84), C_ON_DARK_SOFT, 15, labels[i]);
        }
    } else if (st.state == 1) {
        ui_text_center(F_BOLD, 744, BOX_Y(58), C_ON_DARK_SOFT, 18, "Connecting to the chess server...");
    } else {
        ui_text_center(F_BOLD, 744, BOX_Y(44), C_ON_DARK_SOFT, 18, "The chess server isn't reachable.");
        ui_text_center(F_REG, 744, BOX_Y(74), C_ON_DARK_SOFT, 16, "Play vs computer works offline.");
    }
#undef BOX_Y
    ui_text(F_XBOLD, 560, 240, C_WHITE, 22, "Chess tips");
    snprintf(buf, sizeof buf, "%d lessons", tips_count());
    ui_text_right(F_REG, 928, 240, C_ON_DARK_SOFT, 15, buf);
    for (i = 0; i < 2; i++) {
        tips_draw_thumb(i, TIP_X[i], TIP_Y, TIP_W, TIP_H, active && A.sel == 3 + i);
        ui_text_fit(F_BOLD, (int)TIP_X[i] + 2, TIP_Y + TIP_H + 22, C_WHITE, 16, TIP_W, tips_title(i));
    }
    ui_button(ALL_X, ALL_Y, ALL_W, ALL_H, C_GOLD, C_INK, "See all chess tips", active && A.sel == 5);
    {
        int x = 560;
        x = ui_hint(G_OK, x, 484, "Choose");
        ui_hint(G_TRIANGLE, x, 484, "About / quit");
        ui_text(F_REG, 560, 511, C_ON_DARK_SOFT, 15, "Touch, or use the d-pad.");
    }
    lang_chip_draw(active && A.sel == 7);
}

static void input_home(void)
{
    plat_input *in = &A.in;
    int i;
    if (in->pressed & BTN_TRIANGLE) {
        A.screen = SC_ABOUT;
        return;
    }
    /* the left column runs pill 0, 1, 2, then the stats strip (6), and wraps round */
    if (A.sel < 3 || A.sel == 6) {
        static const int down[7] = {1, 2, 6, 0, 0, 0, 0}, up[7] = {6, 0, 1, 0, 0, 0, 2};
        if (in->pressed & BTN_UP) A.sel = up[A.sel];
        else if (in->pressed & BTN_DOWN) A.sel = down[A.sel];
        else if (in->pressed & BTN_RIGHT) {
            A.last_pill = A.sel;
            A.sel = 3;
        }
    } else if (A.sel == 3) {
        if (in->pressed & BTN_RIGHT) A.sel = 4;
        else if (in->pressed & BTN_LEFT) A.sel = A.last_pill;
        else if (in->pressed & BTN_DOWN) A.sel = 5;
    } else if (A.sel == 4) {
        if (in->pressed & BTN_LEFT) A.sel = 3;
        else if (in->pressed & BTN_DOWN) A.sel = 5;
    } else if (A.sel == 7) { /* the language button, under See all chess tips */
        if (in->pressed & (BTN_UP | BTN_LEFT)) A.sel = 5;
    } else {
        if (in->pressed & BTN_UP) A.sel = 3;
        else if (in->pressed & BTN_LEFT) A.sel = A.last_pill;
        else if (in->pressed & BTN_DOWN) A.sel = 7;
    }
    if (in->pressed & BTN_OK) home_activate(A.sel);
    if (in->touch_up) {
        for (i = 0; i < 3; i++)
            if (ui_hit(in->tx, in->ty, PILL_X, PILL_Y[i], PILL_W, PILL_H + 4)) {
                A.sel = i;
                home_activate(i);
                return;
            }
        for (i = 0; i < 2; i++)
            if (ui_hit(in->tx, in->ty, TIP_X[i], TIP_Y, TIP_W, TIP_H + 30)) {
                A.sel = 3 + i;
                home_activate(3 + i);
                return;
            }
        if (ui_hit(in->tx, in->ty, ALL_X, ALL_Y, ALL_W, ALL_H + 4)) {
            A.sel = 5;
            home_activate(5);
        }
        if (ui_hit(in->tx, in->ty, PILL_X, STRIP_Y, PILL_W, STRIP_H)) {
            A.sel = 6;
            home_activate(6);
        }
        if (lang_chip_hit(in->tx, in->ty)) {
            A.sel = 7;
            home_activate(7);
        }
    }
}

/* ---------- vs computer setup (a dialog over the home screen) ---------- */
#define AX 140
#define AY 50
#define AW 680
#define AH 444
#define LV_Y 166
#define LV_W 68
#define LV_STEP 76
#define LV_X0 180
#define CL_Y 310
#define CL_W 197
#define CL_STEP 209
#define CL_X0 172
#define ST_X 320
#define ST_Y 404
#define ST_W 320

static void draw_ai_setup(void)
{
    char buf[16];
    int i;
    static const int pieces[3] = {UI_BOTH_KINGS, CH_PIECE(CH_WHITE, CH_KING), CH_PIECE(CH_BLACK, CH_KING)};
    draw_home(0);
    ui_dim();
    ui_modal(AX, AY, AW, AH, "Play vs computer", CH_PIECE(CH_BLACK, CH_QUEEN), 0);
    ui_text(F_BOLD, 172, 152, C_INK, 18, "Level");
    ui_text_right(F_REG, 788, 152, C_INK_SOFT, 16, "Runs on your Vita: no connection needed");
    for (i = 0; i < ENGINE_LEVELS; i++) {
        snprintf(buf, sizeof buf, "%d", i + 1);
        ui_choice(LV_X0 + i * LV_STEP, LV_Y, LV_W, 60, ui_level_tint(i), -1, buf, A.ai_level == i + 1,
                  A.ai_sel == 0 && A.ai_level == i + 1);
    }
    snprintf(buf, sizeof buf, "Level %d:", A.ai_level);
    ui_text(F_XBOLD, 172, 262, C_INK, 18, buf);
    ui_text(F_BOLD, 172 + ui_text_w(F_XBOLD, 18, buf) + 8, 262, C_INK_SOFT, 18, local_level_about(A.ai_level));
    ui_text(F_BOLD, 172, CL_Y - 12, C_INK, 18, "Your colour");
    for (i = 0; i < 3; i++)
        ui_choice(CL_X0 + i * CL_STEP, CL_Y, CL_W, 64, C_CYAN, pieces[i], COLOR_NAMES[i], A.ai_color == i,
                  A.ai_sel == 1 && A.ai_color == i);
    ui_button(ST_X, ST_Y, ST_W, 54, C_GOLD, C_INK, "Start the game", A.ai_sel == 2);
    {
        int x = AX + 8;
        x = ui_hint(G_OK, x, 524, "Choose");
        ui_hint(G_BACK, x, 524, "Back");
    }
}

static void start_local(void)
{
    save_settings();
    local_start(A.ai_level, A.ai_color, A.now);
    A.screen = SC_LOCAL;
}

static void input_ai_setup(void)
{
    plat_input *in = &A.in;
    int i;
    if (in->pressed & BTN_BACK) {
        A.screen = SC_HOME;
        return;
    }
    if (in->pressed & BTN_UP) A.ai_sel = (A.ai_sel + 2) % 3;
    if (in->pressed & BTN_DOWN) A.ai_sel = (A.ai_sel + 1) % 3;
    if (in->pressed & (BTN_LEFT | BTN_RIGHT)) {
        int d = (in->pressed & BTN_RIGHT) ? 1 : -1;
        if (A.ai_sel == 0) A.ai_level = (A.ai_level - 1 + d + ENGINE_LEVELS) % ENGINE_LEVELS + 1;
        else if (A.ai_sel == 1) A.ai_color = (A.ai_color + d + 3) % 3;
    }
    if (in->pressed & BTN_OK) {
        if (A.ai_sel == 2) start_local();
        else A.ai_sel++;
    }
    if (in->touch_up) {
        if (ui_modal_close_hit(in->tx, in->ty, AX, AY, AW)) {
            A.screen = SC_HOME;
            return;
        }
        for (i = 0; i < ENGINE_LEVELS; i++)
            if (ui_hit(in->tx, in->ty, LV_X0 + i * LV_STEP, LV_Y, LV_W, 60)) {
                A.ai_level = i + 1;
                A.ai_sel = 0;
            }
        for (i = 0; i < 3; i++)
            if (ui_hit(in->tx, in->ty, CL_X0 + i * CL_STEP, CL_Y, CL_W, 64)) {
                A.ai_color = i;
                A.ai_sel = 1;
            }
        if (ui_hit(in->tx, in->ty, ST_X, ST_Y, ST_W, 58)) start_local();
    }
}

/* ---------- ABOUT ---------- */
static const struct {
    int piece;
    const char *title, *desc; /* desc NULL: the lesson count */
} ABOUT_ROWS[3] = {
    {CH_PIECE(CH_WHITE, CH_KNIGHT), "Online play",
     "Guest games on chess1.coolmathgames.com, the Coolmath Games chess server. This app is not made by or "
     "affiliated with Coolmath Games."},
    {CH_PIECE(CH_BLACK, CH_QUEEN), "Play vs computer", "This app's own engine, running on your Vita: no connection needed."},
    {CH_PIECE(CH_WHITE, CH_BISHOP), "Chess tips", NULL},
};
static const char *ABOUT_CREDITS[6] = {
    "Pieces: merida by Armando Hernandez Marroquin, GPL-2.0-or-later (from Lichess)",
    "Board: maple by the lila authors and pirouetti, AGPL-3.0-or-later (from Lichess)",
    "Highlights: Chessground's open style values, GPL-3.0",
    "Fonts: Figtree and Noto Sans, SIL Open Font License 1.1",
    "Logo, icon and splash made for this app",
    "Built with libcurl, OpenSSL, vita2d and VitaSDK",
};

static void draw_about(void)
{
    char buf[64];
    int i, y0 = 108, gap = 22, lines = 0;
    draw_home(0);
    ui_dim();
    ui_modal(50, 20, 860, 500, "About", -1, 0);
    snprintf(buf, sizeof buf, "%d short lessons written for this app.", tips_count());
    /* a description of three lines or more (some languages) brings the rows closer, above the credits */
    for (i = 0; i < 3; i++) lines += ui_text_wrap(F_REG, 380, 0, 0, 15, 506, 20, ABOUT_ROWS[i].desc ? ABOUT_ROWS[i].desc : buf);
    if (lines > 4) gap = 14;
    if (A.logo140) vita2d_draw_texture(A.logo140, (float)(182 - (int)vita2d_texture_get_width(A.logo140) / 2), 146);
    ui_text_center(F_XBOLD, 182, 324, C_INK, 26, "Chess Online");
    ui_text_center(F_BOLD, 182, 350, C_CYAN_DARK, 15, "Version " APP_VERSION);
    ui_text_center(F_REG, 182, 372, C_INK_SOFT, 14, "A personal PS Vita chess app");
    ui_rect(306, 112, 2, 362, C_FIELD_LINE);
    for (i = 0; i < 3; i++) {
        const char *desc = ABOUT_ROWS[i].desc;
        int n;
        if (!desc) {
            snprintf(buf, sizeof buf, "%d short lessons written for this app.", tips_count());
            desc = buf;
        }
        ui_circle(346, (float)y0 + 20, 20, ui_mix(C_CYAN, C_WHITE, 0.78f));
        ui_piece_scaled(ABOUT_ROWS[i].piece, 346 - 14, (float)y0 + 6, 28);
        ui_text(F_BOLD, 380, y0 + 18, C_INK, 18, ABOUT_ROWS[i].title);
        n = ui_text_wrap(F_REG, 380, y0 + 40, C_INK_SOFT, 15, 506, 20, desc);
        y0 += 40 + (n - 1) * 20 + gap; /* the next row starts 22 px (14 when crowded) under this one's last line */
    }
    ui_text(F_BOLD, 326, 330, C_INK_SOFT, 14, "CREDITS");
    ui_round(326, 340, 560, 140, 14, C_FIELD);
    for (i = 0; i < 6; i++) ui_text_fit(F_REG, 342, 363 + i * 21, C_INK_SOFT, 14, 528, ABOUT_CREDITS[i]);
    {
        int x = ui_hint_c(G_BACK, 84, 506, "Back", C_INK);
        ui_hint_c(G_SQUARE, x, 506, "Quit the app", C_INK);
    }
}

static void input_about(void)
{
    if (A.in.pressed & BTN_SQUARE) {
        plat_log("quit from the About screen");
        net_cmg_close();
        sceKernelExitProcess(0);
    }
    if ((A.in.pressed & (BTN_BACK | BTN_OK | BTN_TRIANGLE)) || A.in.touch_up) A.screen = SC_HOME;
}

static void poll_ime(void)
{
    char text[64];
    int r;
    if (!plat_ime_active()) return;
    r = plat_ime_poll(text, sizeof text);
    if (r > 0 && A.screen == SC_ONLINE) guest_ime_result(text);
}

static void draw_toast_line(void)
{
    if (net_rate_limit_until() > A.now) {
        ui_round(260, 6, 440, 30, 15, C_SUN);
        ui_text_center(F_BOLD, 480, 28, C_INK, 18, "Waiting a moment before the next request");
    }
}

/* ---------- main ---------- */
int main(void)
{
    plat_init();
    plat_log("Chess Online %s starting (text pad %u)", APP_VERSION, co_text_pad_size());
    /* 4x multisampling: GXM smooths the edges of every shape (rounded buttons, circles, rings, lines); without it
     * the curves came out as hard pixel steps. */
#if CO_MSAA
    if (vita2d_init_advanced_with_msaa(POOL_BYTES, SCE_GXM_MULTISAMPLE_4X) < 0) {
        plat_log("gfx: 4x MSAA init failed, falling back to vita2d_init");
        vita2d_init();
    } else {
        plat_log("gfx: 4x MSAA on");
    }
#else
    vita2d_init();
    plat_log("gfx: MSAA off");
#endif
    vita2d_set_clear_color(C_BG_TOP);
    if (ui_load()) plat_log("ui: some resources failed to load");
    lang_load();
    A.ca = plat_read_file("app0:res/cacert.pem", &A.ca_len);
    if (!A.ca) plat_log("net: CA bundle missing");
    if (net_init(A.ca ? A.ca : "", A.ca ? A.ca_len : 0, "ChessOnline/" APP_VERSION " (PS Vita homebrew; personal use)"))
        plat_log("net: init failed");
    load_settings();
    stats_load();
    chess_startpos(&A.startpos);
    A.screen = SC_HOME;
    A.splash = vita2d_load_PNG_file("app0:res/splash.png");
    A.logo56 = vita2d_load_PNG_file("app0:res/logo56.png");
    A.logo140 = vita2d_load_PNG_file("app0:res/logo140.png");
    A.wordmark = vita2d_load_PNG_file("app0:res/wordmark.png");
    if (!A.wordmark) plat_log("ui: res/wordmark.png missing (Home shows the plain title)");
    A.wordstack = vita2d_load_PNG_file("app0:res/wordmark-stack.png");
    A.logo90 = vita2d_load_PNG_file("app0:res/logo90.png");
    if (!A.wordstack || !A.logo90) plat_log("ui: Home's header pictures missing (the one-line title instead)");
    if (!A.logo56 || !A.logo140) plat_log("ui: logo pictures missing");
    A.start = plat_ms();
    guest_start(); /* sign in as a guest: join the lobby in the background */
    update_init(APP_VERSION, A.start); /* 1.0.1: is there a newer release on GitHub? */

    for (;;) {
        int fonts_ready;
        uint64_t f1, f2, f3;
        A.now = plat_ms();
        plat_read_input(&A.in);
        ui_set_ok_is_circle(A.in.ok_is_circle);
        boardui_touch_x = A.in.tx;
        boardui_touch_y = A.in.ty;
        handle_net();
        guest_timers(A.now);
        update_timers(A.now);
        local_update(A.now);
        if (A.screen != SC_ONLINE && guest_showing()) A.screen = SC_ONLINE; /* a match can start any time */
        if (A.splash && A.now - A.start < SPLASH_MS) {
            /* any button or touch skips to the fade */
            if ((A.in.pressed || A.in.touch_up) && A.now - A.start > 300 && A.now - A.start < SPLASH_MS - 350)
                A.start = A.now - (SPLASH_MS - 350);
        } else if (!plat_ime_active()) {
            switch (A.screen) {
            case SC_HOME:
                if (!update_input(&A.in, A.now)) input_home(); /* an update dialog over Home takes the input */
                break;
            case SC_AI_SETUP: input_ai_setup(); break;
            case SC_LOCAL:
                if (local_input(&A.in, A.now)) A.screen = SC_HOME;
                break;
            case SC_ONLINE:
                if (guest_input(&A.in, A.now)) A.screen = SC_HOME;
                break;
            case SC_ABOUT: input_about(); break;
            case SC_TIPS:
                if (tips_input(&A.in)) A.screen = SC_HOME;
                break;
            case SC_STATS:
                if (stats_input(&A.in)) A.screen = SC_HOME;
                break;
            case SC_LANG: {
                int r = lang_picker_input(&A.in);
                if (r == 2) {
                    A.lang_chosen = 1;
                    save_settings();
                }
                if (r) A.screen = SC_HOME;
                break;
            }
            }
        }
        if ((A.screen == SC_LOCAL && local_playing()) || (A.screen == SC_ONLINE && guest_playing())) plat_keep_awake();

        /* while the splash is opaque, rasterize a few font sizes a frame behind it; once it starts to fade (or has
         * failed to load) the rest are done at once, so no text is drawn before its glyphs are ready */
        /* 0.8.0: a language from another font family (the picker) warms two sizes a frame behind a loading screen */
        fonts_ready = ui_warm_step(A.splash ? (A.now - A.start < SPLASH_MS - 350 ? 3 : 1000) : 2);
        if (fonts_ready && !A.names_warmed) {
            uint64_t t0 = plat_ms();
            lang_warm_names();
            A.names_warmed = 1;
            plat_log("lang: the languages' own names ready in %u ms", (unsigned)(plat_ms() - t0));
        }
        /* vita2d_start_drawing resets its one vertex pool to the start every frame. 0.4 let the CPU rewrite
         * vertices the GPU was still reading and hung the GPU once (2026-11-10); 0.4.1-0.5.1 waited for the GPU
         * every frame, which kept the CPU and GPU from working at the same time (52 fps, 42 with 4x MSAA).
         * Instead each frame starts in its own third of the pool: vita2d lets at most 2 frames wait for the screen
         * (displayQueueMaxPendingCount 2, checked in the SDK's vita2d.o), so the swap of frame N+2 returns only
         * once frame N is on screen, finished, and frame N+3 can reuse frame N's region. */
        f1 = sceKernelGetProcessTimeWide();
        vita2d_start_drawing();
        if (A.frames % 3) vita2d_pool_memalign(POOL_REGION * (A.frames % 3), 16); /* skip to this frame's region */
        vita2d_clear_screen();
        if (fonts_ready) {
            ui_background();
            switch (A.screen) {
            case SC_HOME:
                draw_home(!update_showing());
                if (update_showing()) update_draw(A.now);
                break;
            case SC_AI_SETUP: draw_ai_setup(); break;
            case SC_LOCAL: local_draw(A.now); break;
            case SC_ONLINE:
                if (guest_lobby_view()) {
                    draw_home(0);
                    ui_dim();
                }
                guest_draw(A.now);
                break;
            case SC_ABOUT: draw_about(); break;
            case SC_TIPS: tips_draw(A.now); break;
            case SC_STATS: stats_draw(); break;
            case SC_LANG:
                draw_home(0);
                ui_dim();
                lang_picker_draw();
                break;
            }
            draw_toast_line();
        } else if (!A.splash) { /* a new language's fonts warming: the logo and a turning arc, no text */
            float a = (float)(A.now % 1000) / 1000.0f * 6.2832f;
            ui_background();
            if (A.logo140)
                vita2d_draw_texture(A.logo140, (float)(480 - (int)vita2d_texture_get_width(A.logo140) / 2),
                                    (float)(230 - (int)vita2d_texture_get_height(A.logo140) / 2));
            ui_arc(480, 360, 22, 5, a, a + 4.2f, C_CYAN);
        }
        if (A.splash) {
            uint64_t t = A.now - A.start;
            if (t < SPLASH_MS) {
                int a = t + 350 > SPLASH_MS ? (int)(255 * (SPLASH_MS - t) / 350) : 255;
                vita2d_draw_texture_tint(A.splash, 0, 0, RGBA8(255, 255, 255, a));
            }
        }
        vita2d_end_drawing();
        f2 = sceKernelGetProcessTimeWide();
        {
            unsigned used = POOL_BYTES - vita2d_pool_free_space() - POOL_REGION * (A.frames % 3);
            if (used > A.pool_peak + 32768u || (!A.pool_peak && used)) {
                A.pool_peak = used;
                plat_log("gfx: vertex pool peak %u KB of a %u KB region", used / 1024, POOL_REGION / 1024);
            }
        }
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        f3 = sceKernelGetProcessTimeWide();
        A.t_draw += f2 - f1;
        A.t_swap += f3 - f2;
        if (!A.frames++) plat_log("gfx: first frame shown");
        /* the frame rate over three 600-frame windows after the splash (60 fps is 10000 ms) */
        /* per frame on average: draw = building it, swap = handing it to the display queue (waits when 2 are queued) */
        if (A.frames == 120) {
            A.fps_t0 = A.now;
            A.t_draw = A.t_swap = 0;
        } else if (A.frames > 120 && (A.frames - 120) % 600 == 0 && A.fps_logged < 3) {
            unsigned ms = (unsigned)(A.now - A.fps_t0);
            plat_log("gfx: 600 frames in %u ms (%u fps): draw %u us, swap %u us", ms, ms ? 600000u / ms : 0,
                     (unsigned)(A.t_draw / 600), (unsigned)(A.t_swap / 600));
            A.fps_t0 = A.now;
            A.t_draw = A.t_swap = 0;
            A.fps_logged++;
        }
        poll_ime();
        if (A.splash && A.now - A.start >= SPLASH_MS) {
            vita2d_wait_rendering_done();
            vita2d_free_texture(A.splash);
            A.splash = NULL;
            plat_log("gfx: splash done after %u frames", A.frames);
        }
    }
    return 0;
}
