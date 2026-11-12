/* Chess Online: host tests for the translations (rows, placeholders, nested texts, the cache). */
#include <stdio.h>
#include <string.h>

#include "i18n.h"

static int fails, checks;
#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(cond)) {                                                                                                 \
            fails++;                                                                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static void is(int lang, const char *en, const char *want)
{
    const char *got;
    i18n_set(lang);
    got = tr(en);
    CHECK(!strcmp(got, want), "%s [%s]: got \"%s\", want \"%s\"", en, i18n_code(lang), got, want);
}

int main(void)
{
    int i, l;
    char buf[64];

    /* the table itself */
    CHECK(i18n_nrows > 300, "%d rows", i18n_nrows);
    for (i = 0; i < i18n_nrows; i++)
        for (l = 0; l < I18N_LANGS; l++) CHECK(i18n_rows[i][l] && i18n_rows[i][l][0], "row %d lang %d empty", i, l);
    CHECK(strstr(i18n_glyphs, "\xc3\xa9") && strstr(i18n_glyphs, "\xc3\x9f") && strstr(i18n_glyphs, "\xc3\xa7"),
          "glyphs include e-acute, sharp s and c-cedilla");
    CHECK(i18n_from_code("de") == I18N_DE && i18n_from_code("xx") == -1 && i18n_from_code(NULL) == -1, "codes");
    CHECK(!strcmp(i18n_code(I18N_PT), "pt"), "pt code");

    /* English and unknown texts pass through */
    i18n_set(I18N_EN);
    CHECK(tr("Start a game") == (const char *)"Start a game" || !strcmp(tr("Start a game"), "Start a game"), "english");
    is(I18N_DE, "e4", "e4");
    is(I18N_DE, "912", "912");
    is(I18N_FR, "4:55", "4:55");
    is(I18N_ES, "", "");

    /* exact rows */
    is(I18N_DE, "Start a game", "Spiel starten");
    is(I18N_FR, "Your stats", "Vos statistiques");
    is(I18N_PT, "Play vs computer", "Contra o computador");
    is(I18N_IT, "Draw", "Patta");

    /* placeholders: numbers, time controls, times, nested texts */
    is(I18N_ES, "Level 3 of 8, you play white", "Nivel 3 de 8, juegas con blancas");
    is(I18N_ES, "Level 12 of 8", "Nivel 12 de 8");
    is(I18N_FR, "Random opponent, 5+8", "Joueur au hasard, 5+8");
    is(I18N_DE, "A friend, 10+0", "Ein Freund, 10+0");
    is(I18N_IT, "Today 21:40", "Oggi 21:40");
    is(I18N_IT, "Yesterday 9:05", "Ieri 9:05");
    is(I18N_DE, "Strongest level beaten: level 5, \"A club beginner\"",
       "St\xc3\xa4rkste besiegte Stufe: 5, \xe2\x80\x9eVereinsanf\xc3\xa4nger\xe2\x80\x9c");
    is(I18N_DE, "47 games on this Vita since 26 Oct", "47 Spiele auf dieser Vita seit dem 26. Okt.");
    is(I18N_ES, "3 games on this Vita since today", "3 partidas en esta Vita desde hoy");
    is(I18N_FR, "1 game on this Vita since yesterday", "1 partie sur cette Vita depuis hier");
    is(I18N_PT, "9 Nov 21:09", "9 nov 21:09");
    is(I18N_DE, "You resigned after 1 move", "Aufgegeben nach 1 Zug");
    is(I18N_DE, "You resigned after 40 moves", "Aufgegeben nach 40 Z\xc3\xbcgen");
    is(I18N_ES, "Level 8 so far: 0 won, 0 drawn, 1 lost", "Nivel 8: 0 ganadas, 0 tablas, 1 perdidas");
    is(I18N_FR, "That game is no longer available (resume refused)",
       "Cette partie n'est plus disponible (reprise refus\xc3\xa9" "e)");
    is(I18N_IT, "Can't connect: Couldn't resolve host", "Impossibile connettersi: Couldn't resolve host");
    /* a {n} placeholder takes digits only */
    is(I18N_DE, "of games won", "of games won");
    is(I18N_DE, "x won", "x won");
    is(I18N_DE, "21 won", "21 gewonnen");

    /* the cache gives each text its own answer */
    for (i = 0; i < 2000; i++) {
        char want[64];
        snprintf(buf, sizeof buf, "Reconnecting in %d s", i % 97);
        snprintf(want, sizeof want, "Riconnessione tra %d s", i % 97);
        i18n_set(I18N_IT);
        if (strcmp(tr(buf), want)) {
            CHECK(0, "cache: %s -> %s", buf, tr(buf));
            break;
        }
    }
    CHECK(1, "cache loop");
    /* switching language switches the answer for the same text */
    is(I18N_ES, "Resign", "Rendirse");
    is(I18N_FR, "Resign", "Abandonner");
    is(I18N_EN, "Resign", "Resign");

    /* 0.8.0: the twelve other languages the Vita offers */
    CHECK(i18n_from_code("zh-Hant") == I18N_ZH_HANT && i18n_from_code("nb") == I18N_NB && i18n_from_code("zh") == -1,
          "new codes");
    CHECK(!strcmp(i18n_code(I18N_JA), "ja") && !strcmp(i18n_code(I18N_ZH_HANS), "zh-Hans"), "new code names");
    for (i = 0; i < I18N_LANGS; i++) CHECK(i18n_glyphs_lang[i] != NULL, "glyph list %d", i);
    CHECK(i18n_glyphs_lang[I18N_EN][0] == 0 && strlen(i18n_glyphs_lang[I18N_JA]) > 600, "glyph list sizes");
    is(I18N_NL, "Start a game", "Start een partij");
    is(I18N_RU, "Start a game", "\xd0\x9d\xd0\xb0\xd1\x87\xd0\xb0\xd1\x82\xd1\x8c \xd0\xbf\xd0\xb0\xd1\x80\xd1\x82\xd0\xb8\xd1\x8e");
    is(I18N_JA, "12 Nov", "11\xe6\x9c\x88" "12\xe6\x97\xa5");                       /* 11月12日 */
    is(I18N_KO, "12 Nov 21:40", "11\xec\x9b\x94 12\xec\x9d\xbc 21:40");             /* 11월 12일 21:40 */
    is(I18N_FI, "3 Dec", "3.12.");
    is(I18N_DA, "3 May", "3. maj");
    is(I18N_NB, "3 May", "3. mai");
    is(I18N_PL, "Level 3 of 8, you play white", "Poziom 3 z 8, grasz bia\xc5\x82ymi");
    is(I18N_TR, "Level 3 of 8", "Seviye 3/8");
    /* a date inside a sentence, translated again (Japanese: 11月から... 47局) */
    is(I18N_JA, "47 games on this Vita since 26 Oct",
       "10\xe6\x9c\x88" "26\xe6\x97\xa5\xe3\x81\x8b\xe3\x82\x89\xe3\x81\x93\xe3\x81\xaeVita\xe3\x81\xa7" "47\xe5\xb1\x80");
    is(I18N_ZH_HANT, "Draw", "\xe5\x92\x8c\xe5\xb1\x80");                              /* 和局 */
    is(I18N_SV, "Resign", "Ge upp");
    /* the warm-up list: sorted by language (ui.c takes each language's entries as one run), weights 0-2, sizes the
     * app draws, and every character one its language uses */
    CHECK(i18n_nwarm >= 5 * 20, "warm entries: %d", i18n_nwarm);
    for (i = 0; i < i18n_nwarm; i++) {
        const i18n_warm_entry *e = &i18n_warm[i];
        CHECK(e->lang < I18N_LANGS && e->weight <= 2 && e->size >= 10 && e->size <= 72, "warm entry %d", i);
        if (i) CHECK(i18n_warm[i - 1].lang <= e->lang, "warm entries sorted at %d", i);
        CHECK(e->lang == I18N_RU || e->lang >= I18N_JA, "warm entry %d is a non-Latin language", i);
    }

    printf("i18n: %d checks, %d failed (%d rows)\n", checks, fails, i18n_nrows);
    return fails != 0;
}
