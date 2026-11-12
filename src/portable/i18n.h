/* Chess Online: the app in 18 languages. Texts are looked up by their English as they are drawn, the same way the
 * mock-ups do it: a row of the table (i18n_table.c, written from mockups/i18n.js) matches exactly, or a row with
 * {placeholders} matches with the numbers, time controls, times or texts filled in. Our own code (portable C99,
 * host-tested). */
#ifndef CO_I18N_H
#define CO_I18N_H

/* the six of 0.7.0, then (0.8.0) the twelve other languages the Vita itself offers, in tools/chessonline/make_i18n.py's
 * APP_LANGS order */
enum {
    I18N_EN, I18N_ES, I18N_FR, I18N_DE, I18N_IT, I18N_PT,
    I18N_NL, I18N_PL, I18N_TR, I18N_SV, I18N_DA, I18N_NB, I18N_FI, I18N_RU, I18N_JA, I18N_KO, I18N_ZH_HANS, I18N_ZH_HANT,
    I18N_LANGS
};

extern const char *const i18n_rows[][I18N_LANGS];
extern const int i18n_nrows;
extern const char i18n_glyphs[];                  /* every character above ASCII the Latin languages use (UTF-8) */
extern const char *const i18n_glyphs_lang[I18N_LANGS]; /* each language's characters above ASCII (UTF-8) */
/* the characters above ASCII a non-Latin language draws at one weight (0 regular, 1 bold, 2 extra bold) and size,
 * recorded over every screen of the mock-ups (mockups/glyphuse.json), for its font's warm-up (0.8.0) */
typedef struct {
    unsigned char lang, weight, size;
    const char *chars;
} i18n_warm_entry;
extern const i18n_warm_entry i18n_warm[];
extern const int i18n_nwarm;

void i18n_set(int lang);
int i18n_get(void);
const char *i18n_code(int lang);         /* "en", "es", ... */
int i18n_from_code(const char *code);    /* -1 when it isn't one of the 18 */

/* s in the language in use, or s itself (always in English, and for texts that have no row). Not thread-safe:
 * call it from the drawing thread. The result stays valid until a few hundred other texts have been looked up. */
const char *tr(const char *s);

#endif
