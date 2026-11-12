/* Chess Online: drawing helpers on vita2d. Our own code; see ui.h.
 * Board highlight colours follow Chessground's open style values (lichess-org/chessground, GPL-3.0). */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "i18n.h"
#include "platform.h"

/* vita2d caches a glyph by its glyph number alone and scales it from the size it was first drawn at, so text in
 * one size drawn after another came out smeared, with neighbouring glyphs bleeding in (checked in the SDK's
 * vita2d_font.o, 2026-11-10). One vita2d_font per weight and size keeps every glyph at its own size, drawn 1:1. */
#define FONT_MAX_SIZE 72
/* 0.8.0: a font family per script, three weights each. Figtree for the Latin languages; Noto Sans for Russian (its
 * Bold stands in for ExtraBold); Noto Sans JP / KR / SC / TC, cut down to the app's characters, for the CJK languages
 * (tools/chessonline/make_cjk_fonts.py). A language draws everything, Latin letters too, in its family. Families other
 * than Figtree are read the first time they are needed and kept. */
enum { FAM_LATIN, FAM_CYR, FAM_JA, FAM_KO, FAM_SC, FAM_TC, FAM_N };
static const char *const FAM_FILES[FAM_N][3] = {
    {"app0:res/Figtree-Regular.ttf", "app0:res/Figtree-Bold.ttf", "app0:res/Figtree-ExtraBold.ttf"},
    {"app0:res/fonts/NotoSans-Regular.ttf", "app0:res/fonts/NotoSans-Bold.ttf", "app0:res/fonts/NotoSans-Bold.ttf"},
    {"app0:res/fonts/NotoSansJP-Regular.ttf", "app0:res/fonts/NotoSansJP-Bold.ttf", "app0:res/fonts/NotoSansJP-ExtraBold.ttf"},
    {"app0:res/fonts/NotoSansKR-Regular.ttf", "app0:res/fonts/NotoSansKR-Bold.ttf", "app0:res/fonts/NotoSansKR-ExtraBold.ttf"},
    {"app0:res/fonts/NotoSansSC-Regular.ttf", "app0:res/fonts/NotoSansSC-Bold.ttf", "app0:res/fonts/NotoSansSC-ExtraBold.ttf"},
    {"app0:res/fonts/NotoSansTC-Regular.ttf", "app0:res/fonts/NotoSansTC-Bold.ttf", "app0:res/fonts/NotoSansTC-ExtraBold.ttf"},
};
static char *font_data[FAM_N][3];
static size_t font_len[FAM_N][3];
static unsigned char font_tried[FAM_N][3];
static vita2d_font *fonts[FAM_N][3][FONT_MAX_SIZE + 1];
static unsigned char font_failed[FAM_N][3][FONT_MAX_SIZE + 1];
static int font_count, font_warmed, fam_forced = -1, warmed_fam = -1;

static int fam_of(int lang)
{
    switch (lang) {
    case I18N_RU: return FAM_CYR;
    case I18N_JA: return FAM_JA;
    case I18N_KO: return FAM_KO;
    case I18N_ZH_HANS: return FAM_SC;
    case I18N_ZH_HANT: return FAM_TC;
    default: return FAM_LATIN;
    }
}

static int fam_now(void)
{
    return fam_forced >= 0 ? fam_forced : fam_of(i18n_get());
}

/* the file for a family's weight, read once; 0 when it is missing (the caller falls back to Figtree) */
static int fam_ready(int fam, int w)
{
    if (font_data[fam][w]) return 1;
    if (font_tried[fam][w]) return 0;
    font_tried[fam][w] = 1;
    if (w == 2 && !strcmp(FAM_FILES[fam][2], FAM_FILES[fam][1]) && fam_ready(fam, 1)) { /* one file for two weights */
        font_data[fam][2] = font_data[fam][1];
        font_len[fam][2] = font_len[fam][1];
        return 1;
    }
    font_data[fam][w] = plat_read_file(FAM_FILES[fam][w], &font_len[fam][w]);
    if (!font_data[fam][w]) plat_log("ui: font %s missing", FAM_FILES[fam][w]);
    else if (fam != FAM_LATIN) plat_log("ui: font %s read (%u bytes)", FAM_FILES[fam][w], (unsigned)font_len[fam][w]);
    return font_data[fam][w] != NULL;
}
static vita2d_texture *board_tex;
static vita2d_texture *pieces[3][16]; /* 64 px, 96 px, and (0.8.0) 62 px for the framed board */

static const char *PIECE_NAMES = "PNBRQK";

int ui_load(void)
{
    int i, c, t, bad = 0;
    char path[96];
    for (i = 0; i < 3; i++)
        if (!fam_ready(FAM_LATIN, i)) bad = 1;
    board_tex = vita2d_load_PNG_file("app0:res/board.png");
    if (!board_tex) bad = 1;
    else vita2d_texture_set_filters(board_tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    for (c = 0; c < 2; c++) {
        for (t = 1; t <= 6; t++) {
            int big;
            for (big = 0; big < 3; big++) {
                static const char *const DIRS[3] = {"pieces", "pieces96", "pieces62"};
                snprintf(path, sizeof path, "app0:res/%s/%c%c.png", DIRS[big], c ? 'b' : 'w', PIECE_NAMES[t - 1]);
                pieces[big][CH_PIECE(c, t)] = vita2d_load_PNG_file(path);
                if (!pieces[big][CH_PIECE(c, t)]) {
                    plat_log("ui: %s missing", path);
                    bad = 1;
                } else {
                    vita2d_texture_set_filters(pieces[big][CH_PIECE(c, t)], SCE_GXM_TEXTURE_FILTER_LINEAR,
                                               SCE_GXM_TEXTURE_FILTER_LINEAR);
                }
            }
        }
    }
    return bad ? -1 : 0;
}

unsigned ui_mix(unsigned a, unsigned b, float t)
{
    unsigned out = 0;
    int sh;
    for (sh = 0; sh < 32; sh += 8) {
        float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        out |= ((unsigned)(x + (y - x) * t) & 255u) << sh;
    }
    return out;
}

unsigned ui_alpha(unsigned c, int a)
{
    return (c & 0x00FFFFFFu) | ((unsigned)(a & 255) << 24);
}

static vita2d_color_vertex *verts(int n)
{
    return (vita2d_color_vertex *)vita2d_pool_memalign((unsigned)(n * (int)sizeof(vita2d_color_vertex)),
                                                       sizeof(vita2d_color_vertex));
}

void ui_poly(const float *xy, int n, unsigned c)
{
    vita2d_color_vertex *v = verts(n);
    int i;
    if (!v) return;
    for (i = 0; i < n; i++) {
        v[i].x = xy[2 * i];
        v[i].y = xy[2 * i + 1];
        v[i].z = 0.5f;
        v[i].color = c;
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_FAN, v, (size_t)n);
}

void ui_rect(float x, float y, float w, float h, unsigned c)
{
    vita2d_draw_rectangle(x, y, w, h, c);
}

/* the outline of a rounded rectangle into xy; returns the number of points. Segments per corner grow with the
 * radius (6 up to radius 12, then r/2, at most 16), so big corners such as the pills' stay round instead of faceted */
enum { ROUND_SEG_MAX = 16, ROUND_PTS = 4 * (ROUND_SEG_MAX + 1) };
static int round_points(float x, float y, float w, float h, float r, float *xy)
{
    int k = 0, corner, i, seg;
    static const float cx[4] = {1, 0, 0, 1}, cy[4] = {1, 1, 0, 0};
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    seg = r < 12 ? 6 : (int)(r / 2);
    if (seg > ROUND_SEG_MAX) seg = ROUND_SEG_MAX;
    for (corner = 0; corner < 4; corner++) {
        float ox = x + (cx[corner] ? w - r : r), oy = y + (cy[corner] ? h - r : r);
        for (i = 0; i <= seg; i++) {
            float a = (float)M_PI / 2 * (corner + (float)i / seg);
            xy[k++] = ox + cosf(a) * r;
            xy[k++] = oy + sinf(a) * r;
        }
    }
    return k / 2;
}

void ui_round(float x, float y, float w, float h, float r, unsigned c)
{
    float xy[2 * ROUND_PTS];
    ui_poly(xy, round_points(x, y, w, h, r, xy), c);
}

/* keeps the part of a convex polygon on one side of the line x = edge (Sutherland-Hodgman) */
static int clip_x(const float *in, int n, float *out, float edge, int keep_right)
{
    int i, k = 0;
    for (i = 0; i < n; i++) {
        const float *a = &in[2 * i], *b = &in[2 * ((i + 1) % n)];
        int ain = keep_right ? a[0] >= edge : a[0] <= edge, bin = keep_right ? b[0] >= edge : b[0] <= edge;
        if (ain) {
            out[k++] = a[0];
            out[k++] = a[1];
        }
        if (ain != bin) {
            out[k++] = edge;
            out[k++] = a[1] + (b[1] - a[1]) * (edge - a[0]) / (b[0] - a[0]);
        }
    }
    return k / 2;
}

void ui_round_band(float x, float y, float w, float h, float r, float x0, float x1, unsigned c)
{
    float a[2 * ROUND_PTS], b[2 * (ROUND_PTS + 2)], d[2 * (ROUND_PTS + 4)];
    int n = round_points(x, y, w, h, r, a);
    if (x1 <= x0) return;
    n = clip_x(a, n, b, x0, 1);
    n = clip_x(b, n, d, x1, 0);
    if (n >= 3) ui_poly(d, n, c);
}

void ui_arc(float x, float y, float r, float thick, float a0, float a1, unsigned c)
{
    int seg = (int)((a1 - a0) / (2 * (float)M_PI) * 72) + 2, i;
    vita2d_color_vertex *v;
    if (a1 <= a0) return;
    v = verts(2 * (seg + 1));
    if (!v) return;
    for (i = 0; i <= seg; i++) {
        float a = a0 + (a1 - a0) * i / seg;
        v[2 * i].x = x + cosf(a) * r;
        v[2 * i].y = y + sinf(a) * r;
        v[2 * i + 1].x = x + cosf(a) * (r - thick);
        v[2 * i + 1].y = y + sinf(a) * (r - thick);
        v[2 * i].z = v[2 * i + 1].z = 0.5f;
        v[2 * i].color = v[2 * i + 1].color = c;
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, (size_t)(2 * (seg + 1)));
}

unsigned ui_level_tint(int i)
{
    float t = (float)i / 7;
    return t < 0.5f ? ui_mix(C_GREEN, C_AMBER, t * 2) : ui_mix(C_AMBER, C_RED, (t - 0.5f) * 2);
}

void ui_card(float x, float y, float w, float h, float r, unsigned c)
{
    ui_round(x + 2, y + 6, w, h, r, C_SHADOW);
    ui_round(x, y, w, h, r, c);
}

void ui_circle(float x, float y, float r, unsigned c)
{
    vita2d_draw_fill_circle(x, y, r, c);
}

void ui_ring(float x, float y, float r, float thick, unsigned c)
{
    enum { SEG = 40 };
    vita2d_color_vertex *v = verts(2 * (SEG + 1));
    int i;
    if (!v) return;
    for (i = 0; i <= SEG; i++) {
        float a = 2 * (float)M_PI * i / SEG;
        v[2 * i].x = x + cosf(a) * r;
        v[2 * i].y = y + sinf(a) * r;
        v[2 * i + 1].x = x + cosf(a) * (r - thick);
        v[2 * i + 1].y = y + sinf(a) * (r - thick);
        v[2 * i].z = v[2 * i + 1].z = 0.5f;
        v[2 * i].color = v[2 * i + 1].color = c;
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 2 * (SEG + 1));
}

void ui_background(void)
{
    vita2d_color_vertex *v = verts(4);
    if (!v) return;
    v[0].x = 0; v[0].y = 0; v[0].color = C_BG_TOP;
    v[1].x = SCREEN_W; v[1].y = 0; v[1].color = C_BG_TOP;
    v[2].x = 0; v[2].y = SCREEN_H; v[2].color = C_BG_BOTTOM;
    v[3].x = SCREEN_W; v[3].y = SCREEN_H; v[3].color = C_BG_BOTTOM;
    v[0].z = v[1].z = v[2].z = v[3].z = 0.5f;
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
}

void ui_dim(void)
{
    ui_rect(0, 0, SCREEN_W, SCREEN_H, C_DIM);
}

void ui_line(float x0, float y0, float x1, float y1, float t, unsigned c)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy), nx, ny;
    if (l < 0.01f) return;
    nx = -dy / l * t / 2;
    ny = dx / l * t / 2;
    {
        float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
        ui_poly(xy, 4, c);
    }
}

static vita2d_font *font_at(int f, int size)
{
    int fam = fam_now();
    if (f < 0 || f > 2 || size < 1) return NULL;
    if (!fam_ready(fam, f)) fam = FAM_LATIN;
    if (!font_data[fam][f]) return NULL;
    if (size > FONT_MAX_SIZE) size = FONT_MAX_SIZE;
    if (!fonts[fam][f][size] && !font_failed[fam][f][size]) {
        fonts[fam][f][size] = vita2d_load_font_mem(font_data[fam][f], (unsigned)font_len[fam][f]);
        if (fonts[fam][f][size]) font_count++;
        else font_failed[fam][f][size] = 1;
        if (!fonts[fam][f][size] || (font_warmed && fam_forced < 0)) /* a size missing from WARM shows up here */
            plat_log("ui: font %d/%d at %d px %s (%d loaded)", fam, f, size, fonts[fam][f][size] ? "loaded" : "FAILED",
                     font_count);
    }
    return fonts[fam][f][size];
}

/* text in another language's family (the picker draws each language's own name in its own letters) */
int ui_text_in(int lang, int font, int x, int y, unsigned c, int size, const char *s)
{
    int w;
    fam_forced = fam_of(lang);
    w = ui_text_raw(font, x, y, c, size, s);
    fam_forced = -1;
    return w;
}

int ui_text_w_in(int lang, int font, int size, const char *s)
{
    int w;
    fam_forced = fam_of(lang);
    w = ui_text_w_raw(font, size, s);
    fam_forced = -1;
    return w;
}

/* Every weight and size the app draws, from a scan of its ui_text calls (2026-11-10; XBOLD 16 is ui_button's small
 * size; REG 14 is About's, 2026-11-11). Measuring the printable ASCII range rasterizes each glyph into its atlas at start, so a glyph is never
 * written into an atlas while a frame that reads it is being drawn (0.4.2's first try showed one garbled frame). */
static const unsigned char WARM[][2] = {
    {F_REG, 13}, {F_REG, 14}, {F_REG, 15}, {F_REG, 16}, {F_REG, 17}, {F_REG, 18}, {F_REG, 19}, {F_REG, 20},
    {F_BOLD, 12}, {F_BOLD, 13}, {F_BOLD, 14}, {F_BOLD, 15}, {F_BOLD, 16}, {F_BOLD, 17}, {F_BOLD, 18}, {F_BOLD, 19},
    {F_BOLD, 20}, {F_BOLD, 21},
    {F_XBOLD, 14}, {F_XBOLD, 15}, {F_XBOLD, 16}, {F_XBOLD, 18}, {F_XBOLD, 19}, {F_XBOLD, 20}, {F_XBOLD, 22},
    {F_XBOLD, 24}, {F_XBOLD, 25}, {F_XBOLD, 26}, {F_XBOLD, 28}, {F_XBOLD, 30}, {F_XBOLD, 32}, {F_XBOLD, 34},
    {F_XBOLD, 36}, {F_XBOLD, 40}, {F_XBOLD, 46}, {F_XBOLD, 64},
}; /* REG 13, XBOLD 14 / 24 / 46: the statistics screen (0.6.0); BOLD 12: the board's letters in its frame (0.8.0) */

static int warm_next, warm_frames;
static unsigned warm_ms;

/* the glyph-use entries of a language (i18n_warm, sorted by language): first index and count */
static int warm_entries(int lang, int *first)
{
    int i, n = 0;
    *first = 0;
    for (i = 0; i < i18n_nwarm; i++)
        if (i18n_warm[i].lang == lang && i18n_warm[i].chars[0]) {
            if (!n) *first = i;
            n++;
        }
    return n;
}

/* Warms the family of the language in use; a language from another family starts it over (the main loop shows a
 * short loading screen meanwhile). Every size gets printable ASCII. Figtree also gets every Latin language's letters
 * up to 40 px (the bigger sizes draw only digits and codes). Noto Sans and the CJK fonts get exactly the characters
 * their language draws at each weight and size, recorded over every screen of the mock-ups (i18n_warm, 0.8.0): all
 * ~460 CJK characters at every size up to 20 px took 6.5 s on the Vita and would overflow a 512 x 512 atlas above
 * that. A character the list misses is rasterized when it is first drawn. */
int ui_warm_step(int n)
{
    char ascii[96];
    int i, lang = i18n_get(), fam = fam_of(lang), first, nent = 0, nwarm = (int)(sizeof WARM / sizeof *WARM);
    uint64_t t0;
    if (font_warmed && warmed_fam == fam) return 1;
    if (warmed_fam != fam) {
        font_warmed = 0;
        warm_next = warm_frames = 0;
        warm_ms = 0;
        warmed_fam = fam;
    }
    if (fam != FAM_LATIN) nent = warm_entries(lang, &first);
    t0 = plat_ms();
    for (i = 0; i < 95; i++) ascii[i] = (char)(32 + i);
    ascii[95] = 0;
    for (; n > 0 && warm_next < nwarm + nent; n--, warm_next++) {
        if (warm_next < nwarm) {
            ui_text_w_raw(WARM[warm_next][0], WARM[warm_next][1], ascii);
            if (fam == FAM_LATIN && WARM[warm_next][1] <= 40) ui_text_w_raw(WARM[warm_next][0], WARM[warm_next][1], i18n_glyphs);
            if (fam != FAM_LATIN && !nent && WARM[warm_next][1] <= 20) /* no list for this language: every character */
                ui_text_w_raw(WARM[warm_next][0], WARM[warm_next][1], i18n_glyphs_lang[lang]);
        } else {
            const i18n_warm_entry *e = &i18n_warm[first + warm_next - nwarm];
            ui_text_w_raw(e->weight, e->size, e->chars);
        }
    }
    warm_ms += (unsigned)(plat_ms() - t0);
    warm_frames++;
    if (warm_next < nwarm + nent) return 0;
    font_warmed = 1;
    plat_log("ui: %s fonts ready (family %d): %d sizes loaded in all, %u ms over %d frames", i18n_code(lang), fam,
             font_count, warm_ms, warm_frames);
    return 1;
}

/* raw: the text as it is. The other text functions translate it once, as they draw it, into the language in use
 * (i18n.c; the same way the mock-ups do), and then use these. */
int ui_text_raw(int font, int x, int y, unsigned c, int size, const char *s)
{
    vita2d_font *ft = font_at(font, size);
    if (!ft || !s) return 0;
    return vita2d_font_draw_text(ft, x, y, c, (unsigned)size, s);
}

int ui_text_w_raw(int font, int size, const char *s)
{
    vita2d_font *ft = font_at(font, size);
    if (!ft || !s || !*s) return 0;
    return vita2d_font_text_width(ft, (unsigned)size, s);
}

int ui_text(int font, int x, int y, unsigned c, int size, const char *s)
{
    return ui_text_raw(font, x, y, c, size, tr(s));
}

int ui_text_w(int font, int size, const char *s)
{
    return ui_text_w_raw(font, size, tr(s));
}

void ui_text_center(int font, int cx, int y, unsigned c, int size, const char *s)
{
    s = tr(s);
    ui_text_raw(font, cx - ui_text_w_raw(font, size, s) / 2, y, c, size, s);
}

void ui_text_right(int font, int rx, int y, unsigned c, int size, const char *s)
{
    s = tr(s);
    ui_text_raw(font, rx - ui_text_w_raw(font, size, s), y, c, size, s);
}

void ui_text_fit(int font, int x, int y, unsigned c, int size, int max_w, const char *s)
{
    char buf[200];
    size_t n;
    s = tr(s);
    if (ui_text_w_raw(font, size, s) <= max_w) {
        ui_text_raw(font, x, y, c, size, s);
        return;
    }
    snprintf(buf, sizeof buf, "%s", s);
    n = strlen(buf);
    while (n > 0) {
        /* drop the last whole character: its continuation bytes, then its lead byte (1.0.0: 0.8.0 dropped one byte and
         * then only continuation bytes, so it left a lead byte behind and drew a box for it, as in a cut Russian
         * "чёрн□..." on Home; a cut CJK character always did) */
        while (n > 0 && ((unsigned char)buf[n - 1] & 0xC0) == 0x80) n--;
        if (n > 0) n--;
        buf[n] = 0;
        if (n + 3 < sizeof buf) {
            char tmp[204];
            snprintf(tmp, sizeof tmp, "%s...", buf);
            if (ui_text_w_raw(font, size, tmp) <= max_w) {
                ui_text_raw(font, x, y, c, size, tmp);
                return;
            }
        }
    }
}

/* ---------- line breaks (0.8.0) ----------
 * Chinese and Japanese write no spaces, so each of their characters is a place a line may break, except before a
 * closing mark (、。」 and the like), which stays with the character before it, and after an opening mark (「（),
 * which stays with the one after. Korean and the other languages break at their spaces. As the mock-ups'
 * wrap_pieces() in ui.js. */
static unsigned utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = s[0];
    int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3, i;
    if (n) c &= 0x3Fu >> n;
    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) { /* broken UTF-8: take one byte */
            *p += 1;
            return 0xFFFD;
        }
        c = (c << 6) | (s[i] & 0x3Fu);
    }
    *p += n + 1;
    return c;
}

static int is_cjk(unsigned c)
{
    return (c >= 0x2E80 && c <= 0x2FFF) || (c >= 0x3000 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) ||
           (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFFEF);
}

static int in_list(unsigned c, const char *list)
{
    while (*list) if (utf8_next(&list) == c) return 1;
    return 0;
}

static const char CLOSERS[] = "\xe3\x80\x81\xe3\x80\x82\xef\xbc\x8c\xef\xbc\x8e\xe3\x83\xbb\xef\xbc\x9a\xef\xbc\x9b"
                              "\xef\xbc\x81\xef\xbc\x9f\xef\xbc\x89\xe3\x80\x8d\xe3\x80\x8f\xe3\x80\x91\xe3\x80\x89"
                              "\xe3\x80\x8b\xe3\x80\x95\xe3\x83\xbc\xef\xbd\x9e"  /* 、。，．・：；！？）」』】〉》〕ー～ */
                              "\xe3\x81\x81\xe3\x81\x83\xe3\x81\x85\xe3\x81\x87\xe3\x81\x89\xe3\x81\xa3\xe3\x82\x83"
                              "\xe3\x82\x85\xe3\x82\x87\xe3\x82\xa1\xe3\x82\xa3\xe3\x82\xa5\xe3\x82\xa7\xe3\x82\xa9"
                              "\xe3\x83\x83\xe3\x83\xa3\xe3\x83\xa5\xe3\x83\xa7"; /* small kana */
static const char OPENERS[] = "\xef\xbc\x88\xe3\x80\x8c\xe3\x80\x8e\xe3\x80\x90\xe3\x80\x88\xe3\x80\x8a\xe3\x80\x94";

/* the end of the piece that starts at p (no space at p): one CJK character with its closing marks (and, after an
 * opening mark, the piece after it), or a run of other characters up to a space or a CJK character */
static const char *piece_end(const char *p)
{
    const char *q = p, *r;
    unsigned c = utf8_next(&q);
    if (!is_cjk(c)) {
        for (r = q; *q && *q != ' '; q = r) {
            r = q;
            if (is_cjk(utf8_next(&r))) break;
        }
        return q;
    }
    if (in_list(c, OPENERS) && *q && *q != ' ') q = piece_end(q);
    for (;;) {
        r = q;
        if (!*r || !in_list(utf8_next(&r), CLOSERS)) return q;
        q = r;
    }
}

int ui_text_wrap(int font, int x, int y, unsigned c, int size, int max_w, int line_h, const char *s)
{
    char line[256], cand[256];
    int lines = 0;
    const char *p = tr(s);
    line[0] = 0;
    while (p && *p) {
        const char *w;
        int wl, sp = 0;
        while (*p == ' ') {
            p++;
            sp = 1;
        }
        if (!*p) break;
        w = p;
        p = piece_end(p);
        wl = (int)(p - w);
        snprintf(cand, sizeof cand, "%s%s%.*s", line, line[0] && sp ? " " : "", wl, w);
        if (line[0] && ui_text_w_raw(font, size, cand) > max_w) {
            if (c) ui_text_raw(font, x, y + lines * line_h, c, size, line);
            lines++;
            snprintf(line, sizeof line, "%.*s", wl, w);
        } else {
            snprintf(line, sizeof line, "%s", cand);
        }
    }
    if (line[0]) {
        if (c) ui_text_raw(font, x, y + lines * line_h, c, size, line);
        lines++;
    }
    return lines;
}

static int ok_circle;

void ui_set_ok_is_circle(int on)
{
    ok_circle = on;
}

void ui_glyph(int g, float cx, float cy, float s)
{
    float r = s / 2;
    if (g == G_OK) g = ok_circle ? G_CIRCLE : G_CROSS;
    else if (g == G_BACK) g = ok_circle ? G_CROSS : G_CIRCLE;
    ui_circle(cx, cy, r, RGBA8(52, 60, 84, 255));
    switch (g) {
    case G_CROSS: {
        float d = r * 0.42f, t = r * 0.13f;
        float a[8] = {cx - d - t, cy - d + t, cx - d + t, cy - d - t, cx + d + t, cy + d - t, cx + d - t, cy + d + t};
        float b[8] = {cx + d - t, cy - d - t, cx + d + t, cy - d + t, cx - d + t, cy + d + t, cx - d - t, cy + d - t};
        ui_poly(a, 4, RGBA8(140, 170, 255, 255));
        ui_poly(b, 4, RGBA8(140, 170, 255, 255));
        break;
    }
    case G_CIRCLE:
        ui_ring(cx, cy, r * 0.55f, r * 0.17f, RGBA8(255, 120, 120, 255));
        break;
    case G_TRIANGLE: {
        float h = r * 0.55f;
        float o[6] = {cx, cy - h, cx + h * 0.95f, cy + h * 0.65f, cx - h * 0.95f, cy + h * 0.65f};
        float i2[6] = {cx, cy - h * 0.45f, cx + h * 0.5f, cy + h * 0.38f, cx - h * 0.5f, cy + h * 0.38f};
        ui_poly(o, 3, RGBA8(90, 220, 170, 255));
        ui_poly(i2, 3, RGBA8(52, 60, 84, 255));
        break;
    }
    default: {
        float d = r * 0.45f, t = r * 0.17f;
        ui_rect(cx - d, cy - d, 2 * d, 2 * d, RGBA8(255, 150, 210, 255));
        ui_rect(cx - d + t, cy - d + t, 2 * (d - t), 2 * (d - t), RGBA8(52, 60, 84, 255));
        break;
    }
    }
}

int ui_hint_c(int g, int x, int y, const char *label, unsigned c)
{
    ui_glyph(g, (float)x + 11, (float)y - 7, 22);
    return x + 28 + ui_text(F_BOLD, x + 28, y, c, 17, label) + 22;
}

int ui_hint(int g, int x, int y, const char *label)
{
    return ui_hint_c(g, x, y, label, C_ON_DARK_SOFT);
}

int ui_hit(int px, int py, float x, float y, float w, float h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

void ui_piece(int piece, float x, float y, int big)
{
    vita2d_texture *t = piece > 0 && piece < 16 ? pieces[big ? 1 : 0][piece] : NULL;
    if (t) vita2d_draw_texture(t, x, y);
}

void ui_piece_scaled(int piece, float x, float y, float size)
{
    vita2d_texture *t = piece > 0 && piece < 16 ? pieces[size > 64 ? 1 : 0][piece] : NULL;
    if (size == 62 && piece > 0 && piece < 16 && pieces[2][piece]) { /* the framed board's own size, drawn 1:1 */
        vita2d_draw_texture(pieces[2][piece], x, y);
        return;
    }
    if (t) vita2d_draw_texture_scale(t, x, y, size / vita2d_texture_get_width(t), size / vita2d_texture_get_height(t));
}

void ui_board_area(unsigned c)
{
    /* the big board's frame (0.8.0), so a dimmed board has no bright edge; rounded like the frame itself (draw_board:
     * 16 px frame, radius 20), so its corners don't darken the navy outside the frame (1.0.0, the user saw them) */
    ui_round(8, 8, 528, 528, 20, c);
}

void ui_board_square_xy(const ui_board *b, int sq, int *x, int *y)
{
    int f = CH_FILE(sq), r = CH_RANK(sq);
    if (b->flip) {
        f = 7 - f;
        r = 7 - r;
    }
    *x = b->x + f * b->sq;
    *y = b->y + (7 - r) * b->sq;
}

int ui_board_square_at(const ui_board *b, int x, int y)
{
    int f, r;
    if (x < b->x || y < b->y || x >= b->x + 8 * b->sq || y >= b->y + 8 * b->sq) return -1;
    f = (x - b->x) / b->sq;
    r = 7 - (y - b->y) / b->sq;
    if (b->flip) {
        f = 7 - f;
        r = 7 - r;
    }
    return CH_SQ(f, r);
}

void ui_board_state_init(ui_board_state *s, const chess_pos *pos)
{
    memset(s, 0, sizeof *s);
    s->pos = pos;
    s->last_from = s->last_to = s->selected = s->cursor = s->check_sq = s->drag_from = -1;
}

void ui_draw_board(const ui_board *b, const ui_board_state *s)
{
    int sq, i, x, y;
    float S = (float)b->sq, m = b->frame ? 16 : S >= 40 ? 6 : 3;
    ui_round(b->x - m, b->y - m + 3, 8 * S + 2 * m, 8 * S + 2 * m, m + 4, C_SHADOW);
    ui_round(b->x - m, b->y - m, 8 * S + 2 * m, 8 * S + 2 * m, m + 4, RGBA8(96, 60, 30, 255));
    if (board_tex) vita2d_draw_texture_scale(board_tex, b->x, b->y, 8 * S / vita2d_texture_get_width(board_tex),
                                             8 * S / vita2d_texture_get_height(board_tex));
    else
        for (sq = 0; sq < 64; sq++) {
            ui_board_square_xy(b, sq, &x, &y);
            ui_rect(x, y, S, S, (CH_FILE(sq) + CH_RANK(sq)) & 1 ? RGBA8(240, 217, 181, 255) : RGBA8(181, 136, 99, 255));
        }
    if (b->coords && b->frame) { /* 0.8.0: the letters in the frame, under each file and beside each rank, so no piece
                                  * ever covers them (the user: 0.7.0's knights and bishops hid b1's and c1's) */
        char t[2] = {0, 0};
        unsigned cream = RGBA8(246, 228, 196, 255);
        for (i = 0; i < 8; i++) {
            t[0] = (char)('a' + i);
            ui_board_square_xy(b, CH_SQ(i, b->flip ? 7 : 0), &x, &y);
            ui_text_in(I18N_EN, F_BOLD, x + (int)S / 2 - ui_text_w_in(I18N_EN, F_BOLD, 12, t) / 2, b->y + 8 * (int)S + 13,
                       cream, 12, t);
            t[0] = (char)('1' + i);
            ui_board_square_xy(b, CH_SQ(b->flip ? 7 : 0, i), &x, &y);
            ui_text_in(I18N_EN, F_BOLD, b->x - 8 - ui_text_w_in(I18N_EN, F_BOLD, 12, t) / 2, y + (int)S / 2 + 4, cream, 12, t);
        }
    } else if (b->coords && S >= 40) { /* our own small edge labels: files on the bottom row, ranks on the left column */
        char t[2] = {0, 0};
        unsigned lightc = RGBA8(150, 104, 62, 255), darkc = RGBA8(246, 228, 196, 255);
        for (i = 0; i < 8; i++) {
            t[0] = (char)('a' + i);
            sq = CH_SQ(i, b->flip ? 7 : 0);
            ui_board_square_xy(b, sq, &x, &y);
            ui_text(F_BOLD, x + (int)S - 12, y + (int)S - 4, (CH_FILE(sq) + CH_RANK(sq)) & 1 ? lightc : darkc, 13, t);
            t[0] = (char)('1' + i);
            sq = CH_SQ(b->flip ? 7 : 0, i);
            ui_board_square_xy(b, sq, &x, &y);
            ui_text(F_BOLD, x + 3, y + 14, (CH_FILE(sq) + CH_RANK(sq)) & 1 ? lightc : darkc, 13, t);
        }
    }
    /* last move and selection (Chessground: last-move rgba(155,199,0,.41), selected rgba(20,85,30,.5)) */
    if (s->last_from >= 0) {
        ui_board_square_xy(b, s->last_from, &x, &y);
        ui_rect(x, y, S, S, RGBA8(155, 199, 0, 105));
        ui_board_square_xy(b, s->last_to, &x, &y);
        ui_rect(x, y, S, S, RGBA8(155, 199, 0, 105));
    }
    if (s->selected >= 0) {
        ui_board_square_xy(b, s->selected, &x, &y);
        ui_rect(x, y, S, S, RGBA8(20, 85, 30, 128));
    }
    if (s->check_sq >= 0) { /* a red glow, after Chessground's check gradient */
        ui_board_square_xy(b, s->check_sq, &x, &y);
        for (i = 6; i >= 1; i--) ui_circle(x + S / 2, y + S / 2, S * 0.12f * i, RGBA8(231, 0, 0, 40));
    }
    for (sq = 0; sq < 64; sq++) {
        int pc = s->pos->sq[sq];
        if (!pc || sq == s->drag_from) continue;
        ui_board_square_xy(b, sq, &x, &y);
        if (b->sq == 64) ui_piece(pc, x, y, 0);
        else ui_piece_scaled(pc, x, y, S);
    }
    /* move destinations: dots on empty squares, rings on captures (Chessground move-dest style) */
    for (i = 0; i < s->ndests; i++) {
        int to = s->dests[i].to, dup = 0, k;
        for (k = 0; k < i; k++)
            if (s->dests[k].to == to) dup = 1;
        if (dup) continue;
        ui_board_square_xy(b, to, &x, &y);
        if (s->pos->sq[to] || (s->dests[i].flags & CH_F_EP)) ui_ring(x + S / 2, y + S / 2, S / 2, S * 0.1f, RGBA8(20, 85, 0, 90));
        else ui_circle(x + S / 2, y + S / 2, S * 0.14f, RGBA8(20, 85, 30, 128));
    }
    if (s->cursor >= 0) {
        ui_board_square_xy(b, s->cursor, &x, &y);
        ui_rect(x, y, S, 4, C_GOLD);
        ui_rect(x, y + S - 4, S, 4, C_GOLD);
        ui_rect(x, y, 4, S, C_GOLD);
        ui_rect(x + S - 4, y, 4, S, C_GOLD);
    }
    if (s->drag_from >= 0 && s->pos->sq[s->drag_from])
        ui_piece_scaled(s->pos->sq[s->drag_from], s->drag_x - S / 2, s->drag_y - S / 2, S);
}

void ui_arrow(const ui_board *b, int from, int to, unsigned c)
{
    int x0, y0, x1, y1;
    float S = (float)b->sq, ax, ay, bx, by, dx, dy, l, ux, uy, head, hx, hy, hw;
    ui_board_square_xy(b, from, &x0, &y0);
    ui_board_square_xy(b, to, &x1, &y1);
    ax = x0 + S / 2;
    ay = y0 + S / 2;
    bx = x1 + S / 2;
    by = y1 + S / 2;
    dx = bx - ax;
    dy = by - ay;
    l = sqrtf(dx * dx + dy * dy);
    if (l < 1) return;
    ux = dx / l;
    uy = dy / l;
    head = S * 0.46f;
    hw = S * 0.30f;
    bx -= ux * S * 0.1f;
    by -= uy * S * 0.1f;
    hx = bx - ux * head;
    hy = by - uy * head;
    ui_line(ax + ux * S * 0.22f, ay + uy * S * 0.22f, hx, hy, S * 0.17f, c);
    {
        float tri[6] = {bx, by, hx - uy * hw, hy + ux * hw, hx + uy * hw, hy - ux * hw};
        ui_poly(tri, 3, c);
    }
}

void ui_mark(const ui_board *b, int sq, unsigned c)
{
    int x, y;
    float S = (float)b->sq;
    ui_board_square_xy(b, sq, &x, &y);
    ui_ring(x + S / 2, y + S / 2, S * 0.47f, S * 0.08f, c);
}

/* ---------- widgets ---------- */
static unsigned darker(unsigned c, float t)
{
    return ui_mix(c, RGBA8(0, 0, 0, (c >> 24) & 255), t);
}

void ui_sel_ring(float x, float y, float w, float h, float r, unsigned c, float lip)
{
    ui_round(x - 3, y - 3, w + 6, h + 6 + lip, r + 3, c);
}

void ui_piece_tinted(int piece, float x, float y, float size, unsigned color)
{
    vita2d_texture *t = piece > 0 && piece < 16 ? pieces[size > 64 ? 1 : 0][piece] : NULL;
    if (t)
        vita2d_draw_texture_tint_scale(t, x, y, size / vita2d_texture_get_width(t), size / vita2d_texture_get_height(t),
                                       color);
}

void ui_button(float x, float y, float w, float h, unsigned color, unsigned tc, const char *label, int selected)
{
    float r = h / 2;
    int size = h >= 50 ? 22 : h >= 40 ? 19 : 16;
    if (selected) ui_sel_ring(x, y, w, h, r, color == C_GOLD ? C_CYAN : C_GOLD, 4); /* round the 4 px lip too */
    ui_round(x, y + 4, w, h, r, darker(color, 0.32f));
    ui_round(x, y, w, h, r, color);
    ui_round(x + r * 0.6f, y + 3, w - r * 1.2f, h * 0.38f, h * 0.19f, ui_alpha(C_WHITE, 38));
    ui_text_center(F_XBOLD, (int)(x + w / 2), (int)(y + h / 2 + size * 0.36f), tc, size, label);
}

void ui_menu_pill(float x, float y, float w, float h, const char *label, const char *sub, int piece, int selected)
{
    float r = h / 2, d = h - 14, ps = (float)(int)(d * 0.74f + 0.5f); /* the piece stays inside its disc */
    if (selected) ui_sel_ring(x, y, w, h, r, C_GOLD, 5);
    ui_round(x, y + 5, w, h, r, C_CYAN_DARK);
    ui_round(x, y, w, h, r, selected ? C_CYAN_HI : C_CYAN);
    ui_round(x + r, y + 4, w - 2 * r, h * 0.34f, h * 0.17f, ui_alpha(C_WHITE, 44));
    ui_circle(x + r, y + r, d / 2, C_WHITE);
    ui_piece_scaled(piece, x + r - ps / 2, y + r - ps / 2, ps);
    ui_text(F_XBOLD, (int)(x + h + 6), (int)(y + (sub ? r + 2 : r + 9)), C_WHITE, 25, label);
    if (sub) ui_text_fit(F_BOLD, (int)(x + h + 7), (int)(y + r + 26), ui_alpha(C_WHITE, 225), 16, (int)(w - h - 30), sub);
}

void ui_modal(float x, float y, float w, float h, const char *title, int piece, int close_sel)
{
    float cx = x + w - 40, cy = y + 36;
    ui_round(x + 3, y + 9, w, h, 24, C_SHADOW);
    ui_round(x, y, w, h, 24, C_CARD);
    ui_round(x, y, w, 72, 24, C_PANEL_HI);
    ui_rect(x, y + 40, w, 32, C_PANEL_HI);
    ui_rect(x, y + 72, w, 3, C_CYAN);
    if (piece > 0) { /* a 38 px piece stays inside the 50 px disc */
        ui_circle(x + 46, cy, 25, ui_alpha(C_WHITE, 46));
        ui_piece_scaled(piece, x + 46 - 19, cy - 19, 38);
    }
    ui_text_fit(F_XBOLD, (int)(x + (piece > 0 ? 84 : 28)), (int)(y + 47), C_WHITE, 28, (int)(w - 180), title);
    if (close_sel) ui_circle(cx, cy, 24, C_GOLD);
    ui_circle(cx, cy, 19, ui_alpha(C_WHITE, close_sel ? 90 : 46));
    ui_line(cx - 7, cy - 7, cx + 7, cy + 7, 4, C_WHITE);
    ui_line(cx + 7, cy - 7, cx - 7, cy + 7, 4, C_WHITE);
}

int ui_modal_close_hit(int px, int py, float x, float y, float w)
{
    float dx = px - (x + w - 40), dy = py - (y + 36);
    return dx * dx + dy * dy <= 34 * 34;
}

/* 0.6.1: the slider fits a 50 px row (label, track and an 11 px knob between y+5 and y+45), and its gold box (y+1 to
 * y+49) stays 6 px clear of a slider 50 px above or below. 0.6.0 drew a 62 px box over Random opponent's 46 px rows,
 * so it ran into the other slider. */
void ui_slider(float x, float y, float w, const char *label, const char *val, int v, int lo, int hi, int selected)
{
    float t = hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0, ty = y + 33, kx;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    kx = x + 12 + t * (w - 24);
    if (selected) ui_round(x - 10, y + 1, w + 20, 48, 12, ui_alpha(C_GOLD, 80));
    ui_text(F_BOLD, (int)x, (int)y + 18, C_INK, 18, label);
    ui_text_right(F_XBOLD, (int)(x + w), (int)y + 18, C_CYAN_DARK, 20, val);
    ui_round(x, ty - 4, w, 8, 4, C_FIELD_LINE);
    ui_round(x, ty - 4, kx - x, 8, 4, C_CYAN);
    ui_circle(kx, ty + 1, 11, ui_alpha(C_INK, 40));
    ui_circle(kx, ty, 11, C_WHITE);
    ui_ring(kx, ty, 11, 3, selected ? C_GOLD : C_CYAN);
}

int ui_slider_hit(int px, int py, float x, float y, float w)
{
    return ui_hit(px, py, x - 12, y, w + 24, 50);
}

int ui_slider_value(int px, float x, float w, int lo, int hi)
{
    float t = (px - x - 12) / (w - 24);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return lo + (int)(t * (float)(hi - lo) + 0.5f);
}

void ui_choice(float x, float y, float w, float h, unsigned color, int piece, const char *label, int on, int selected)
{
    ui_choice_tint(x, y, w, h, color, piece, label, on, selected, 0);
}

void ui_choice_tint(float x, float y, float w, float h, unsigned color, int piece, const char *label, int on, int selected,
                    unsigned tint)
{
    float ps = h - 12 > 40 ? 40 : h - 12, tx;
    unsigned bg = on ? color : ui_mix(color, C_WHITE, 0.78f), tc = on ? C_WHITE : C_INK;
    if (selected) ui_sel_ring(x, y, w, h, 12, color == C_GOLD ? C_CYAN : C_GOLD, on ? 3 : 0);
    if (on) ui_round(x, y + 3, w, h, 12, darker(color, 0.3f));
    ui_round(x, y, w, h, 12, bg);
    if (h >= 58 && (piece > 0 || piece == UI_BOTH_KINGS)) {
        /* tall tile: the piece above its label; a square tile centres the pair and uses a 14 px label */
        int sq = w == h;
        float py;
        ps = h * 0.5f > 40 ? 40 : h * 0.5f;
        py = sq ? y + (float)(int)((h - ps - 24) / 2 + 0.5f) : y + 5;
        if (piece == UI_BOTH_KINGS) {
            ui_piece_scaled(CH_PIECE(CH_WHITE, CH_KING), x + w / 2 - ps * 0.78f, py, ps);
            ui_piece_scaled(CH_PIECE(CH_BLACK, CH_KING), x + w / 2 - ps * 0.22f, py, ps);
        } else if (tint) {
            ui_piece_tinted(piece, x + w / 2 - ps / 2, py, ps, tint);
        } else {
            ui_piece_scaled(piece, x + w / 2 - ps / 2, py, ps);
        }
        if (label) ui_text_center(F_BOLD, (int)(x + w / 2), (int)(y + h - (sq ? 11 : 9)), tc, sq ? 14 : 15, label);
        return;
    }
    if (piece == UI_BOTH_KINGS) {
        ui_piece_scaled(CH_PIECE(CH_WHITE, CH_KING), x + 4, y + (h - ps) / 2, ps);
        ui_piece_scaled(CH_PIECE(CH_BLACK, CH_KING), x + 4 + ps * 0.45f, y + (h - ps) / 2, ps);
        tx = x + 8 + ps * 1.45f;
    } else if (piece > 0) {
        ui_piece_scaled(piece, x + 6, y + (h - ps) / 2, ps);
        tx = x + 10 + ps;
    } else {
        tx = -1;
    }
    if (!label) return;
    if (tx < 0) ui_text_center(F_XBOLD, (int)(x + w / 2), (int)(y + h / 2 + 9), tc, h >= 56 ? 26 : 18, label);
    else ui_text_fit(F_BOLD, (int)tx, (int)(y + h / 2 + 6), tc, 16, (int)(x + w - tx - 6), label);
}

void ui_clock(float x, float y, float w, float h, const char *text, int active, int low)
{
    unsigned bg = active ? (low ? C_RED : C_WHITE) : ui_alpha(C_WHITE, 34);
    unsigned tc = active ? (low ? C_WHITE : C_INK) : C_ON_DARK_SOFT;
    ui_round(x, y, w, h, 10, bg);
    ui_text_center(F_XBOLD, (int)(x + w / 2), (int)(y + h / 2 + 12), tc, 32, text);
}

/* ---------- game side panel ---------- */
void ui_player_bar(float y, int color, const char *name, const char *sub, const char *clock, int turn, int low)
{
    float h = 72;
    /* whose turn shows as the brighter panel (online also the white running clock): no gold bar, the user's
     * decision (2026-11-11) */
    ui_round(GP_X, y, GP_W, h, 16, turn ? C_PANEL_HI : C_PANEL);
    ui_circle(GP_X + 38, y + h / 2, 25, color == CH_WHITE ? RGBA8(226, 234, 246, 255) : RGBA8(12, 20, 34, 255));
    ui_piece_scaled(CH_PIECE(color, CH_KING), GP_X + 18, y + h / 2 - 20, 40);
    ui_text_fit(F_XBOLD, GP_X + 76, (int)y + (sub ? 32 : 45), C_WHITE, 22, clock ? 170 : 300, name);
    if (sub) ui_text_fit(F_REG, GP_X + 77, (int)y + 57, C_ON_DARK_SOFT, 16, clock ? 170 : 300, sub);
    if (clock) ui_clock(GP_X + GP_W - 148, y + 10, 136, h - 20, clock, turn, low);
}

void ui_status_strip(float y, const char *text, unsigned color, int dots, unsigned long long now)
{
    ui_round(GP_X, y, GP_W, 42, 21, color);
    ui_text_fit(F_BOLD, GP_X + 20, (int)y + 28, color == C_GOLD || color == C_WHITE ? C_INK : C_WHITE, 19,
                GP_W - (dots ? 90 : 40), text);
    if (dots) {
        int k, ph = (int)((now / 250) % 3);
        for (k = 0; k < 3; k++) ui_circle(GP_X + GP_W - 52 + k * 16, y + 21, k == ph ? 6 : 4, ui_alpha(C_WHITE, 230));
    }
}

void ui_moves_box(float y, float h, const char (*san)[12], int nmoves, const char *empty, const char *note)
{
    char buf[16];
    int rows = (nmoves + 1) / 2, fit = (int)((h - 16) / 28), first, r;
    ui_round(GP_X, y, GP_W, h, 16, C_PANEL);
    if (note) fit--;
    first = rows > fit ? rows - fit : 0;
    for (r = first; r < rows; r++) {
        int ty = (int)y + 30 + (r - first) * 28;
        if (r % 2 == 0) ui_round(GP_X + 8, ty - 21, GP_W - 16, 28, 8, ui_alpha(C_WHITE, 12));
        snprintf(buf, sizeof buf, "%d.", r + 1);
        ui_text_right(F_REG, GP_X + 52, ty, C_ON_DARK_SOFT, 18, buf);
        ui_text(F_BOLD, GP_X + 66, ty, 2 * r == nmoves - 1 ? C_CYAN_HI : C_WHITE, 20, san[2 * r]);
        if (2 * r + 1 < nmoves)
            ui_text(F_BOLD, GP_X + 226, ty, 2 * r + 1 == nmoves - 1 ? C_CYAN_HI : C_WHITE, 20, san[2 * r + 1]);
    }
    if (!nmoves && empty) ui_text(F_REG, GP_X + 20, (int)y + 34, C_ON_DARK_SOFT, 19, empty);
    if (note) {
        ui_round(GP_X + 8, y + h - 40, GP_W - 16, 32, 12, C_GOLD);
        ui_text_fit(F_BOLD, GP_X + 20, (int)(y + h - 18), C_INK, 17, GP_W - 40, note);
    }
}

#define GB_W 120
static float gb_x(int i)
{
    return GP_X + i * (GB_W + 20);
}

void ui_game_buttons(const char *a, const char *b, const char *c)
{
    const char *l[3] = {a, b, c};
    static const unsigned col[3] = {C_CYAN, C_RED, C_PANEL_HI};
    int i;
    for (i = 0; i < 3; i++)
        if (l[i]) ui_button(gb_x(i), GP_BTN_Y, GB_W, GP_BTN_H - 4, col[i], C_WHITE, l[i], 0);
}

int ui_game_button_hit(int px, int py)
{
    int i;
    for (i = 0; i < 3; i++)
        if (ui_hit(px, py, gb_x(i), GP_BTN_Y, GB_W, GP_BTN_H)) return i;
    return -1;
}

/* ---------- dialogs ---------- */
#define MENU_X 280
#define MENU_Y 40
#define MENU_W 400
static float menu_item_y(int i)
{
    return MENU_Y + 92 + i * 54;
}

void ui_menu_draw(const char *title, const char *const *items, int n, int sel)
{
    int i;
    ui_dim();
    ui_modal(MENU_X, MENU_Y, MENU_W, 104 + n * 54, title, -1, 0);
    for (i = 0; i < n; i++) {
        float y = menu_item_y(i);
        ui_round(MENU_X + 20, y, MENU_W - 40, 46, 23, i == sel ? C_CYAN : C_FIELD);
        ui_text_center(F_BOLD, MENU_X + MENU_W / 2, (int)y + 31, i == sel ? C_WHITE : C_INK, 21, items[i]);
    }
}

int ui_menu_hit(int px, int py, int n)
{
    int i;
    if (ui_modal_close_hit(px, py, MENU_X, MENU_Y, MENU_W)) return -2;
    for (i = 0; i < n; i++)
        if (ui_hit(px, py, MENU_X + 20, menu_item_y(i), MENU_W - 40, 46)) return i;
    return -1;
}

void ui_confirm_draw_sel(const char *title, const char *sub, const char *yes, const char *no, int sel)
{
    ui_dim();
    ui_card(240, 150, 480, 240, 24, C_CARD);
    ui_text_center(F_XBOLD, 480, 214, C_INK, 30, title);
    if (sub) ui_text_center(F_REG, 480, 250, C_INK_SOFT, 19, sub);
    ui_button(270, 296, 190, 54, C_RED, C_WHITE, yes, sel == 1);
    ui_button(500, 296, 190, 54, C_CYAN, C_WHITE, no, sel == 0);
}

void ui_confirm_draw(const char *title, const char *sub, const char *yes, const char *no)
{
    ui_confirm_draw_sel(title, sub, yes, no, 1);
}

int ui_confirm_hit(int px, int py)
{
    if (ui_hit(px, py, 270, 296, 190, 58)) return 1;
    if (ui_hit(px, py, 500, 296, 190, 58)) return 0;
    return -1;
}

void ui_result_draw(const char *head, unsigned head_c, const char *sub, const char *b1, const char *b2, int sel,
                    const char *extra, unsigned extra_c)
{
    int sw = ui_text_w(F_BOLD, 18, sub), sx = 272 - sw / 2, eh = extra && extra[0] ? 26 : 0;
    ui_board_area(ui_alpha(C_BG_BOTTOM, 110));
    /* the colour is the card's own top edge: the card in the colour, the white card 10 px lower over it */
    ui_round(88, 166, 372, 224 + (float)eh, 24, C_SHADOW);
    ui_round(86, 160, 372, 224 + (float)eh, 24, head_c);
    ui_round(86, 170, 372, 214 + (float)eh, 24, C_CARD);
    ui_text_center(F_XBOLD, 272, 224, head_c, 40, head);
    ui_text_fit(F_BOLD, sx < 104 ? 104 : sx, 258, C_INK_SOFT, 18, 336, sub);
    ui_button(106, 296, 158, 54, C_GOLD, C_INK, b1, sel == 0);
    ui_button(280, 296, 158, 54, C_CYAN, C_WHITE, b2, sel == 1);
    if (eh) {
        /* the statistics' line: your record in this kind of game, or a first win against a level. 15 px, down to 13 px
         * when it is wider than the card (2026-11-12: "Random games so far: 0 won, 0 drawn, 1 lost" was cut at 15 px) */
        int size = 15, w = ui_text_w(F_BOLD, size, extra);
        while (w > 336 && size > 13) w = ui_text_w(F_BOLD, --size, extra);
        ui_rect(110, 370, 324, 1, C_FIELD_LINE);
        ui_text_fit(F_BOLD, w > 336 ? 104 : 272 - w / 2, 395, extra_c, size, 336, extra);
    }
}

int ui_result_hit(int px, int py)
{
    if (ui_hit(px, py, 106, 296, 158, 58)) return 0;
    if (ui_hit(px, py, 280, 296, 158, 58)) return 1;
    return -1;
}
