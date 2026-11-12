/* Chess Online: "Play vs computer" with our own engine, on the console (works offline). Our own code. */
#ifndef CO_LOCALGAME_H
#define CO_LOCALGAME_H

#include <stdint.h>

#include "platform.h"

/* level 1..8; color_choice 0 random, 1 white, 2 black */
void local_start(int level, int color_choice, uint64_t now);
int local_input(const plat_input *in, uint64_t now); /* 1: back to the home screen */
void local_update(uint64_t now);                     /* collects the computer's move */
void local_draw(uint64_t now);
int local_playing(void);
/* level 1..8 in a few words ("A casual player"), for the setup dialog and the statistics */
const char *local_level_about(int level);

#endif
