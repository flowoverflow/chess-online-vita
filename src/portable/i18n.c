/* Chess Online: the app in 18 languages (six until 0.7.0). Our own code; see i18n.h.
 * Placeholders: {n} {a} {b} {w} {d} {l} {p} {h} {m} are numbers, {tc} a time control (5+8), {t} a time (21:40) and
 * {s} any text, which is looked up again (mockups/i18n.js documents the same rules). */
#include "i18n.h"

#include <stdint.h>
#include <string.h>

static const char *CODES[I18N_LANGS] = {"en", "es", "fr", "de", "it", "pt", "nl", "pl", "tr", "sv", "da", "nb", "fi",
                                         "ru", "ja", "ko", "zh-Hans", "zh-Hant"};
static int lang;

void i18n_set(int l)
{
    lang = l >= 0 && l < I18N_LANGS ? l : I18N_EN;
}

int i18n_get(void)
{
    return lang;
}

const char *i18n_code(int l)
{
    return l >= 0 && l < I18N_LANGS ? CODES[l] : CODES[0];
}

int i18n_from_code(const char *code)
{
    int i;
    for (i = 0; i < I18N_LANGS; i++)
        if (code && !strcmp(code, CODES[i])) return i;
    return -1;
}

static uint32_t hash(const char *s, int l)
{
    uint32_t h = 2166136261u ^ (uint32_t)l;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

/* ---------- the rows: exact texts in a hash table, the rest as patterns, longest first ---------- */
enum { EXACT_SLOTS = 2048, MAX_PATTERNS = 512 };
static short exact[EXACT_SLOTS]; /* row + 1, 0 empty */
static short patterns[MAX_PATTERNS];
static int npatterns, built;

static void build(void)
{
    int i, j;
    for (i = 0; i < i18n_nrows; i++) {
        const char *en = i18n_rows[i][0];
        if (strchr(en, '{')) {
            if (npatterns < MAX_PATTERNS) patterns[npatterns++] = (short)i;
            continue;
        }
        for (j = (int)(hash(en, 0) & (EXACT_SLOTS - 1)); exact[j]; j = (j + 1) & (EXACT_SLOTS - 1)) {
        }
        exact[j] = (short)(i + 1);
    }
    for (i = 1; i < npatterns; i++) { /* insertion sort: the longest English first, as the mock-ups try them */
        short p = patterns[i];
        size_t lp = strlen(i18n_rows[p][0]);
        for (j = i; j > 0 && strlen(i18n_rows[patterns[j - 1]][0]) < lp; j--) patterns[j] = patterns[j - 1];
        patterns[j] = p;
    }
    built = 1;
}

static int find_exact(const char *s)
{
    int j;
    for (j = (int)(hash(s, 0) & (EXACT_SLOTS - 1)); exact[j]; j = (j + 1) & (EXACT_SLOTS - 1))
        if (!strcmp(i18n_rows[exact[j] - 1][0], s)) return exact[j] - 1;
    return -1;
}

/* ---------- matching a row with placeholders ---------- */
enum { MAX_CAPS = 8 };
typedef struct {
    char name[4];
    const char *at;
    int len;
} cap;

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* reads "{name}" at p; returns its length or 0 */
static int placeholder(const char *p, char *name)
{
    int k = 1;
    if (*p != '{') return 0;
    while (p[k] >= 'a' && p[k] <= 'z' && k < 4) k++;
    if (p[k] != '}' || k == 1) return 0;
    memcpy(name, p + 1, (size_t)(k - 1));
    name[k - 1] = 0;
    return k + 1;
}

/* how many characters of s the placeholder's kind accepts at most, and whether a length is acceptable */
static int kind_ok(const char *name, const char *s, int len)
{
    int i;
    if (!strcmp(name, "s")) return len > 0;
    if (!strcmp(name, "tc")) { /* digits '+' digits */
        int plus = -1;
        for (i = 0; i < len; i++) {
            if (s[i] == '+' && plus < 0 && i > 0) plus = i;
            else if (!is_digit(s[i])) return 0;
        }
        return plus > 0 && plus < len - 1;
    }
    if (!strcmp(name, "t")) { /* 1-2 digits ':' 2 digits */
        return (len == 4 || len == 5) && s[len - 3] == ':' && is_digit(s[0]) && is_digit(s[len - 1]) &&
               is_digit(s[len - 2]) && (len == 4 || is_digit(s[1]));
    }
    for (i = 0; i < len; i++)
        if (!is_digit(s[i])) return 0;
    return len > 0;
}

static int match(const char *p, const char *s, cap *caps, int ncap)
{
    char name[4];
    int pl = placeholder(p, name);
    if (pl) {
        int max = (int)strlen(s), len;
        if (ncap >= MAX_CAPS) return -1;
        /* {s} as short as possible, numbers and times as long as possible (then shorter on a later mismatch) */
        if (!strcmp(name, "s")) {
            for (len = 1; len <= max; len++) {
                int r;
                if (!kind_ok(name, s, len)) continue;
                r = match(p + pl, s + len, caps, ncap + 1);
                if (r >= 0) {
                    strcpy(caps[ncap].name, name);
                    caps[ncap].at = s;
                    caps[ncap].len = len;
                    return r;
                }
            }
            return -1;
        }
        for (len = max; len >= 1; len--) {
            int r;
            if (!kind_ok(name, s, len)) continue;
            r = match(p + pl, s + len, caps, ncap + 1);
            if (r >= 0) {
                strcpy(caps[ncap].name, name);
                caps[ncap].at = s;
                caps[ncap].len = len;
                return r;
            }
        }
        return -1;
    }
    if (!*p) return *s ? -1 : ncap;
    if (*p != *s) return -1;
    return match(p + 1, s + 1, caps, ncap);
}

/* ---------- the cache, and the filled-in texts ---------- */
enum { CACHE_SLOTS = 512, KEY_MAX = 200, VAL_MAX = 360, RING = 4 };
typedef struct {
    uint32_t h;
    signed char lang, same; /* same: no row matched, the text is its own translation */
    char key[KEY_MAX];
    char val[VAL_MAX];
} slot;
static slot cache[CACHE_SLOTS];
static char ring[RING][VAL_MAX];
static int ring_next;

static void fill(const char *tmpl, const cap *caps, int ncap, char *out, size_t n)
{
    size_t k = 0;
    const char *p = tmpl;
    while (*p && k + 1 < n) {
        char name[4];
        int pl = placeholder(p, name), i;
        if (!pl) {
            out[k++] = *p++;
            continue;
        }
        for (i = 0; i < ncap; i++)
            if (!strcmp(caps[i].name, name)) break;
        if (i < ncap) {
            char sub[VAL_MAX];
            const char *v = sub;
            size_t l = (size_t)caps[i].len < sizeof sub - 1 ? (size_t)caps[i].len : sizeof sub - 1;
            memcpy(sub, caps[i].at, l);
            sub[l] = 0;
            if (!strcmp(name, "s")) v = tr(sub); /* "A club beginner" inside a longer text */
            l = strlen(v);
            if (l > n - 1 - k) l = n - 1 - k;
            memcpy(out + k, v, l);
            k += l;
        }
        p += pl;
    }
    out[k] = 0;
}

/* returns the translation for s into out (no cache), or NULL when no row matches */
static const char *translate(const char *s, char *out, size_t n)
{
    int r = find_exact(s), i;
    if (r >= 0) return i18n_rows[r][lang];
    for (i = 0; i < npatterns; i++) {
        cap caps[MAX_CAPS];
        int nc = match(i18n_rows[patterns[i]][0], s, caps, 0);
        if (nc >= 0) {
            fill(i18n_rows[patterns[i]][lang], caps, nc, out, n);
            return out;
        }
    }
    return NULL;
}

const char *tr(const char *s)
{
    uint32_t h;
    slot *c;
    char tmp[VAL_MAX];
    const char *t;
    size_t len;
    if (!s || !*s || lang == I18N_EN) return s;
    if (!built) build();
    len = strlen(s);
    h = hash(s, lang);
    c = &cache[h & (CACHE_SLOTS - 1)];
    if (len < KEY_MAX && c->h == h && c->lang == lang && !strcmp(c->key, s)) return c->same ? s : c->val;
    t = translate(s, tmp, sizeof tmp);
    if (t && t != tmp) return t; /* a row's own text: no copy needed */
    if (len >= KEY_MAX) { /* too long to keep: a short-lived copy */
        if (!t) return s;
        t = strcpy(ring[ring_next = (ring_next + 1) % RING], tmp);
        return t;
    }
    c->h = h;
    c->lang = (signed char)lang;
    strcpy(c->key, s);
    c->same = !t;
    if (t) strcpy(c->val, tmp);
    return c->same ? s : c->val;
}
