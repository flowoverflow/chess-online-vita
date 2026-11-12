/* Chess Online: the language button and picker. Our own code; see langui.h. Coordinates are the mock-ups'
 * (mockups/lang.js). 0.8.0: 18 languages, three columns of six rows. */
#include "langui.h"

#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "i18n.h"
#include "ui.h"

/* in the enum's order (i18n.h): the language's own name (never translated, drawn in its own letters), its English
 * name (a row of the translations), the flag picture and the tag on Home's button. English shows the UK flag and
 * Portuguese Brazil's (the app's English is British, its Portuguese Brazilian; the US and Portugal flags are in
 * res/flags too); Chinese shows China's flag for Simplified and Taiwan's for Traditional, whose texts use Taiwan's
 * terms (the user's choice, 2026-11-12). */
static const struct {
    const char *native, *english, *flag, *tag;
} LANG[I18N_LANGS] = {
    {"English", "English", "gb", "EN"},
    {"Espa\xc3\xb1ol", "Spanish", "es", "ES"},
    {"Fran\xc3\xa7" "ais", "French", "fr", "FR"},
    {"Deutsch", "German", "de", "DE"},
    {"Italiano", "Italian", "it", "IT"},
    {"Portugu\xc3\xaas", "Portuguese", "br", "PT"},
    {"Nederlands", "Dutch", "nl", "NL"},
    {"Polski", "Polish", "pl", "PL"},
    {"T\xc3\xbcrk\xc3\xa7" "e", "Turkish", "tr", "TR"},
    {"Svenska", "Swedish", "se", "SV"},
    {"Dansk", "Danish", "dk", "DA"},
    {"Norsk", "Norwegian", "no", "NO"},
    {"Suomi", "Finnish", "fi", "FI"},
    {"\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9", "Russian", "ru", "RU"},            /* Русский */
    {"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "Japanese", "jp", "JA"},                               /* 日本語 */
    {"\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", "Korean", "kr", "KO"},                                 /* 한국어 */
    {"\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87", "Chinese (Simplified)", "cn", "ZH"},       /* 简体中文 */
    {"\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87", "Chinese (Traditional)", "tw", "ZH"},      /* 繁體中文 */
};
/* the picker's order: by own name, Latin alphabet first, then Cyrillic, then the CJK languages, read down each column */
static const unsigned char ORDER[I18N_LANGS] = {
    I18N_DA, I18N_DE, I18N_EN, I18N_ES, I18N_FR, I18N_IT, I18N_NL, I18N_NB, I18N_PL,
    I18N_PT, I18N_FI, I18N_SV, I18N_TR, I18N_RU, I18N_JA, I18N_KO, I18N_ZH_HANS, I18N_ZH_HANT,
};

static vita2d_texture *flag42[I18N_LANGS];
static int sel; /* a place in ORDER */

#define LB_X 832
#define LB_Y 466
#define LB_W 96
#define LB_H 44
#define LQ_X 40
#define LQ_Y 18
#define LQ_W 880
#define LQ_H 508
#define LQ_TW 264
#define LQ_TH 48
#define LQ_GX 14
#define LQ_GY 8
#define LQ_ROWS 6

void lang_load(void)
{
    char path[64];
    int i, missing = 0;
    for (i = 0; i < I18N_LANGS; i++) {
        snprintf(path, sizeof path, "app0:res/flags/%s-42.png", LANG[i].flag);
        flag42[i] = vita2d_load_PNG_file(path);
        missing += !flag42[i];
    }
    if (missing) plat_log("lang: %d flag pictures missing", missing);
}

void lang_warm_names(void)
{
    int i;
    /* each language's own name at the picker's two sizes, in its own font: new fonts' atlases, filled before the
     * picker first draws them (no glyph written into an atlas a frame in flight reads) */
    for (i = 0; i < I18N_LANGS; i++) {
        ui_text_w_in(i, F_XBOLD, 18, LANG[i].native);
        ui_text_w_in(i, F_XBOLD, 19, LANG[i].native);
    }
}

static void flag(vita2d_texture *t, float x, float y, float w, float h)
{
    if (t) vita2d_draw_texture(t, x, y);
    else ui_round(x, y, w, h, 4, ui_alpha(C_WHITE, 60));
}

/* ---------- the button on Home ---------- */
void lang_chip_draw(int selected)
{
    int l = i18n_get();
    if (selected) ui_sel_ring(LB_X, LB_Y, LB_W, LB_H, LB_H / 2, C_GOLD, 0);
    ui_round(LB_X, LB_Y, LB_W, LB_H, LB_H / 2, C_PANEL_HI);
    flag(flag42[l], LB_X + 14, LB_Y + 8, 42, 28);
    ui_text_in(I18N_EN, F_XBOLD, LB_X + 64, LB_Y + 29, C_WHITE, 16, LANG[l].tag); /* Latin letters: Figtree */
}

int lang_chip_hit(int x, int y)
{
    return ui_hit(x, y, LB_X, LB_Y, LB_W, LB_H);
}

/* ---------- the picker ---------- */
static void row_xy(int i, int *x, int *y)
{
    *x = LQ_X + 30 + (i / LQ_ROWS) * (LQ_TW + LQ_GX);
    *y = LQ_Y + 88 + (i % LQ_ROWS) * (LQ_TH + LQ_GY);
}

void lang_picker_open(void)
{
    int i;
    sel = 0;
    for (i = 0; i < I18N_LANGS; i++)
        if (ORDER[i] == i18n_get()) sel = i;
}

static void check_mark(float cx, float cy)
{
    ui_circle(cx, cy, 11, C_WHITE);
    ui_line(cx - 5, cy, cx - 1, cy + 4, 3, C_CYAN_DARK);
    ui_line(cx - 1, cy + 4, cx + 6, cy - 4, 3, C_CYAN_DARK);
}

void lang_picker_draw(void)
{
    int i, x, y, cur = i18n_get();
    ui_modal(LQ_X, LQ_Y, LQ_W, LQ_H, "Language", -1, 0);
    for (i = 0; i < I18N_LANGS; i++) {
        int l = ORDER[i], on = l == cur;
        row_xy(i, &x, &y);
        if (sel == i) ui_sel_ring((float)x, (float)y, LQ_TW, LQ_TH, 12, C_GOLD, on ? 3 : 0);
        if (on) ui_round((float)x, (float)y + 3, LQ_TW, LQ_TH, 12, ui_mix(C_CYAN, RGBA8(0, 0, 0, 255), 0.3f));
        ui_round((float)x, (float)y, LQ_TW, LQ_TH, 12, on ? C_CYAN : C_FIELD);
        flag(flag42[l], (float)x + 12, (float)y + 10, 42, 28);
        /* the language's own name in its own letters; under it its name in the language in use (not for the one in
         * use, which has the check mark instead) */
        if (on) {
            ui_text_in(l, F_XBOLD, x + 66, y + 31, C_WHITE, 19, LANG[l].native);
            check_mark((float)(x + LQ_TW - 22), (float)y + LQ_TH / 2.0f);
        } else {
            ui_text_in(l, F_XBOLD, x + 66, y + 22, C_INK, 18, LANG[l].native);
            ui_text_fit(F_REG, x + 66, y + 40, C_INK_SOFT, 13, LQ_TW - 66 - 12, LANG[l].english);
        }
    }
    ui_text_center(F_REG, 480, LQ_Y + 444, C_INK_SOFT, 15, "Your choice is kept on this Vita.");
    ui_text_center(F_REG, 480, LQ_Y + 466, C_INK_SOFT, 15, "Messages from the chess server stay in English.");
    x = ui_hint_c(G_OK, LQ_X + 30, LQ_Y + 494, "Choose", C_INK);
    ui_hint_c(G_BACK, x, LQ_Y + 494, "Back", C_INK);
}

static int choose(int i)
{
    i18n_set(ORDER[i]);
    plat_log("lang: %s chosen", i18n_code(ORDER[i]));
    return 2;
}

int lang_picker_input(const plat_input *in)
{
    int i;
    if (in->pressed & BTN_BACK) return 1;
    /* columns of six: up and down move in a column, left and right across */
    if ((in->pressed & BTN_UP) && sel % LQ_ROWS) sel--;
    if ((in->pressed & BTN_DOWN) && sel % LQ_ROWS < LQ_ROWS - 1 && sel + 1 < I18N_LANGS) sel++;
    if ((in->pressed & BTN_LEFT) && sel >= LQ_ROWS) sel -= LQ_ROWS;
    if ((in->pressed & BTN_RIGHT) && sel + LQ_ROWS < I18N_LANGS) sel += LQ_ROWS;
    if (in->pressed & BTN_OK) return choose(sel);
    if (in->touch_up) {
        if (ui_modal_close_hit(in->tx, in->ty, LQ_X, LQ_Y, LQ_W)) return 1;
        for (i = 0; i < I18N_LANGS; i++) {
            int x, y;
            row_xy(i, &x, &y);
            if (ui_hit(in->tx, in->ty, (float)x, (float)y, LQ_TW, LQ_TH)) {
                sel = i;
                return choose(i);
            }
        }
    }
    return 0;
}
