/* Chess Online: Vita platform helpers. Our own code; see platform.h. */
#include "platform.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <psp2/system_param.h>
#include <psp2/touch.h>

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static int log_fd = -1;
static int ok_is_circle;

uint64_t plat_ms(void)
{
    return (uint64_t)sceKernelGetProcessTimeWide() / 1000u;
}

void plat_init(void)
{
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    SceCommonDialogConfigParam cfg;
    SceIoStat st;
    int enter = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    memset(&init, 0, sizeof init);
    memset(&boot, 0, sizeof boot);
    sceAppUtilInit(&init, &boot);
    sceCommonDialogConfigParamInit(&cfg);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int *)&cfg.language);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int *)&cfg.enterButtonAssign);
    sceCommonDialogSetConfigParam(&cfg);
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter) >= 0)
        ok_is_circle = enter == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;

    sceIoMkdir(CO_DATA_DIR, 0777);
    if (sceIoGetstat(CO_LOG_FILE, &st) >= 0 && st.st_size > 512 * 1024) {
        sceIoRemove(CO_DATA_DIR "/log.prev.txt");
        sceIoRename(CO_LOG_FILE, CO_DATA_DIR "/log.prev.txt");
    }
    log_fd = sceIoOpen(CO_LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
}

void plat_log(const char *fmt, ...)
{
    char line[512];
    int n;
    va_list ap;
    SceDateTime t;
    if (log_fd < 0) return;
    sceRtcGetCurrentClockLocalTime(&t);
    n = snprintf(line, sizeof line, "%02d:%02d:%02d.%03d ", t.hour, t.minute, t.second, t.microsecond / 1000);
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    pthread_mutex_lock(&log_lock);
    sceIoWrite(log_fd, line, (SceSize)n);
    pthread_mutex_unlock(&log_lock);
}

char *plat_read_file(const char *path, size_t *len)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    SceOff size;
    char *buf;
    int got;
    if (fd < 0) return NULL;
    size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size < 0 || size > 8 * 1024 * 1024) {
        sceIoClose(fd);
        return NULL;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    got = sceIoRead(fd, buf, (SceSize)size);
    sceIoClose(fd);
    if (got != (int)size) {
        free(buf);
        return NULL;
    }
    buf[size] = 0;
    if (len) *len = (size_t)size;
    return buf;
}

int plat_write_file(const char *path, const void *data, size_t len)
{
    char tmp[256];
    SceUID fd;
    int w;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    fd = sceIoOpen(tmp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return -1;
    w = sceIoWrite(fd, data, (SceSize)len);
    sceIoClose(fd);
    if (w != (int)len) {
        sceIoRemove(tmp);
        return -1;
    }
    sceIoRemove(path);
    return sceIoRename(tmp, path) < 0 ? -1 : 0;
}

int plat_append_file(const char *path, const void *data, size_t len)
{
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    int w;
    if (fd < 0) return -1;
    w = sceIoWrite(fd, data, (SceSize)len);
    sceIoClose(fd);
    return w == (int)len ? 0 : -1;
}

int plat_rename_file(const char *from, const char *to)
{
    sceIoRemove(to);
    return sceIoRename(from, to) < 0 ? -1 : 0;
}

int plat_remove_file(const char *path)
{
    return sceIoRemove(path) < 0 ? -1 : 0;
}

int plat_system_lang(void)
{
    int l = -1;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &l) < 0) return -1;
    return l;
}

void plat_local_time(int *year, int *month, int *day, int *hour, int *minute)
{
    SceDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    *year = t.year;
    *month = t.month;
    *day = t.day;
    *hour = t.hour;
    *minute = t.minute;
}

void plat_read_input(plat_input *in)
{
    SceCtrlData pad;
    SceTouchData touch;
    unsigned b = 0, prev = in->held;
    int was = in->touching;
    memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons & SCE_CTRL_UP) b |= BTN_UP;
    if (pad.buttons & SCE_CTRL_DOWN) b |= BTN_DOWN;
    if (pad.buttons & SCE_CTRL_LEFT) b |= BTN_LEFT;
    if (pad.buttons & SCE_CTRL_RIGHT) b |= BTN_RIGHT;
    if (pad.buttons & SCE_CTRL_CROSS) b |= ok_is_circle ? BTN_BACK : BTN_OK;
    if (pad.buttons & SCE_CTRL_CIRCLE) b |= ok_is_circle ? BTN_OK : BTN_BACK;
    if (pad.buttons & SCE_CTRL_TRIANGLE) b |= BTN_TRIANGLE;
    if (pad.buttons & SCE_CTRL_SQUARE) b |= BTN_SQUARE;
    if (pad.buttons & SCE_CTRL_LTRIGGER) b |= BTN_L;
    if (pad.buttons & SCE_CTRL_RTRIGGER) b |= BTN_R;
    if (pad.buttons & SCE_CTRL_START) b |= BTN_START;
    if (pad.buttons & SCE_CTRL_SELECT) b |= BTN_SELECT;
    /* the left stick acts as a d-pad */
    if (pad.ly < 40) b |= BTN_UP;
    if (pad.ly > 215) b |= BTN_DOWN;
    if (pad.lx < 40) b |= BTN_LEFT;
    if (pad.lx > 215) b |= BTN_RIGHT;
    in->pressed = b & ~prev;
    in->held = b;
    in->ok_is_circle = ok_is_circle;

    memset(&touch, 0, sizeof touch);
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    in->touching = touch.reportNum > 0;
    if (in->touching) {
        in->tx = touch.report[0].x / 2;
        in->ty = touch.report[0].y / 2;
    }
    in->touch_down = in->touching && !was;
    in->touch_up = !in->touching && was;
    if (in->touch_down) {
        in->down_x = in->tx;
        in->down_y = in->ty;
    }
}

void plat_keep_awake(void)
{
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
}

/* ---- on-screen keyboard ---- */
static SceWChar16 ime_title[64];
static SceWChar16 ime_init[128];
static SceWChar16 ime_buf[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static int ime_on;

/* UTF-8 to the keyboard's UTF-16: titles are translated (0.7.0), so they hold accented letters */
static void to_utf16(const char *s, SceWChar16 *out, size_t n)
{
    size_t k = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && k + 1 < n) {
        unsigned c = *p++;
        if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            c = ((c & 0x0F) << 12) | ((p[0] & 0x3Fu) << 6) | (p[1] & 0x3Fu);
            p += 2;
        } else if (c >= 0xC0 && c < 0xE0 && (p[0] & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (p[0] & 0x3Fu);
            p += 1;
        } else if (c >= 0x80) {
            c = '?';
        }
        out[k++] = (SceWChar16)c;
    }
    out[k] = 0;
}

int plat_ime_start(const char *title, const char *initial, int max_len, int password)
{
    SceImeDialogParam p;
    if (ime_on) return -1;
    to_utf16(title, ime_title, sizeof ime_title / sizeof *ime_title);
    to_utf16(initial ? initial : "", ime_init, sizeof ime_init / sizeof *ime_init);
    memset(ime_buf, 0, sizeof ime_buf);
    sceImeDialogParamInit(&p);
    p.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    p.languagesForced = SCE_TRUE;
    p.type = SCE_IME_TYPE_BASIC_LATIN;
    p.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION | SCE_IME_OPTION_NO_ASSISTANCE;
    p.textBoxMode = password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    p.title = ime_title;
    p.maxTextLength = (SceUInt32)max_len;
    p.initialText = ime_init;
    p.inputTextBuffer = ime_buf;
    if (sceImeDialogInit(&p) < 0) return -1;
    ime_on = 1;
    return 0;
}

int plat_ime_active(void)
{
    return ime_on;
}

int plat_ime_poll(char *out, size_t n)
{
    SceImeDialogResult r;
    size_t k = 0;
    int i;
    if (!ime_on) return -1;
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return 0;
    memset(&r, 0, sizeof r);
    sceImeDialogGetResult(&r);
    sceImeDialogTerm();
    ime_on = 0;
    if (r.button != SCE_IME_DIALOG_BUTTON_ENTER) return -1;
    for (i = 0; ime_buf[i] && k + 1 < n; i++) {
        unsigned c = ime_buf[i];
        if (c >= 0x20 && c < 0x7f) out[k++] = (char)c; /* game codes are ASCII */
    }
    out[k] = 0;
    return 1;
}
