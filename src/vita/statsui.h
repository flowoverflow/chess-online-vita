/* Chess Online: your statistics, kept on the Vita: the games file, the statistics screen (four tabs: Overview, Vs
 * computer, Online, Recent games) and the "Your stats" strip on the home screen. Our own code; the figures are
 * worked out in src/portable/gamelog.c. The design is the mock-ups' layout B (docs/chessonline.md). */
#ifndef CO_STATSUI_H
#define CO_STATSUI_H

#include <stddef.h>

#include "gamelog.h"
#include "platform.h"

#define CO_GAMES_FILE CO_DATA_DIR "/games.txt"
#define CO_GAMES_CLEARED_FILE CO_DATA_DIR "/games-cleared.txt" /* what "Clear statistics" put aside last */

void stats_load(void); /* at start */
/* adds a finished game (the date and time are filled in here) and writes it to the games file; `line` gets the
 * result card's line. Returns the colour to draw that line in. */
unsigned stats_record(gl_game *g, char *line, size_t n);
void stats_unrecord_last(void); /* a takeback after a computer game ended: that game goes on */

void stats_open(void);
int stats_input(const plat_input *in); /* 1: back to the home screen */
void stats_draw(void);
/* the "Your stats" strip with your record, under the home screen's three pills */
void stats_draw_strip(float x, float y, float w, float h, int selected);

#endif
