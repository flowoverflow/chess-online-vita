/* Chess Online: "Chess tips": a gallery of our own short lessons and a step-by-step viewer on the board. */
#ifndef CO_TIPS_H
#define CO_TIPS_H

#include <stdint.h>

#include "platform.h"

void tips_open(int lesson);                       /* -1: the gallery; else straight into that lesson */
int tips_input(const plat_input *in);             /* 1: back to the home screen */
void tips_draw(uint64_t now);
/* a 16:9 card for lesson i (also used on the home screen) */
void tips_draw_thumb(int i, float x, float y, float w, float h, int selected);
int tips_count(void);
const char *tips_title(int i);

#endif
