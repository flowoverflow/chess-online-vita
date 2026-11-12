/* Chess Online: the language button at the bottom right of Home and the language picker (18 languages since
 * 0.8.0, i18n.h). Our own code; the design is the mock-ups' (mockups/lang.js). */
#ifndef CO_LANGUI_H
#define CO_LANGUI_H

#include "platform.h"

void lang_load(void);                        /* the flag pictures (app0:res/flags) */
void lang_warm_names(void);                  /* each language's own name in its own font, before the picker shows */
void lang_chip_draw(int selected);           /* Home's button: the flag and the language's tag */
int lang_chip_hit(int x, int y);
void lang_picker_open(void);
/* 0: still open, 1: closed, 2: a language was chosen (now in use; the caller saves it) */
int lang_picker_input(const plat_input *in);
void lang_picker_draw(void);

#endif
