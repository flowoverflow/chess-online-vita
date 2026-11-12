/* Chess Online: Vita platform helpers (time, log, files, input, on-screen keyboard). Our own code.
 * Files use sceIo directly: newlib's stdio in this SDK has no locks, and the app has several threads. */
#ifndef CO_PLATFORM_H
#define CO_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define CO_DATA_DIR "ux0:data/ChessOnline"
#define CO_SETTINGS_FILE CO_DATA_DIR "/settings.txt"
#define CO_LOG_FILE CO_DATA_DIR "/log.txt"

uint64_t plat_ms(void);
void plat_init(void);

void plat_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Whole-file read (malloc'd, NUL-terminated) and atomic write (temp file + rename). */
char *plat_read_file(const char *path, size_t *len);
int plat_write_file(const char *path, const void *data, size_t len);
int plat_append_file(const char *path, const void *data, size_t len); /* creates the file if needed */
int plat_rename_file(const char *from, const char *to);              /* replaces `to` */
int plat_remove_file(const char *path);
/* the console's local date and time */
void plat_local_time(int *year, int *month, int *day, int *hour, int *minute);
/* the console's language setting (SCE_SYSTEM_PARAM_LANG_*), or -1 */
int plat_system_lang(void);

/* Buttons: edge-triggered "pressed this frame" and held masks, with the system's confirm button
 * (cross or circle) mapped to BTN_OK and the other to BTN_BACK. */
enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_OK = 1 << 4, BTN_BACK = 1 << 5, BTN_TRIANGLE = 1 << 6, BTN_SQUARE = 1 << 7,
    BTN_L = 1 << 8, BTN_R = 1 << 9, BTN_START = 1 << 10, BTN_SELECT = 1 << 11
};

typedef struct {
    unsigned held, pressed;
    int touching, touch_down, touch_up; /* front panel, screen pixels */
    int tx, ty, down_x, down_y;
    int ok_is_circle;
} plat_input;

void plat_read_input(plat_input *in);
/* A button pressed by touch: the touch ended on the same target it began on, by the dialog's own hit test (1.0.0).
 * A drag begun elsewhere never presses a button, as when a result card appeared under a piece being dragged and
 * the release pressed its Rematch button. -1 when there is no such tap. */
#define PLAT_TAP(in, hit, ...)                                                                                    \
    ((in)->touch_up && hit((in)->down_x, (in)->down_y, ##__VA_ARGS__) == hit((in)->tx, (in)->ty, ##__VA_ARGS__) \
         ? hit((in)->tx, (in)->ty, ##__VA_ARGS__)                                                               \
         : -1)
void plat_keep_awake(void);

/* On-screen keyboard. Start it, then call plat_ime_poll once per frame (after drawing) until it returns
 * 1 (text in out) or -1 (cancelled). */
int plat_ime_start(const char *title, const char *initial, int max_len, int password);
int plat_ime_poll(char *out, size_t n);
int plat_ime_active(void);

#endif
