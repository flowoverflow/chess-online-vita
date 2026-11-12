/* Chess Online Updater (CHESSUPD1) 1.0: the helper that installs Chess Online's updates (1.0.1). Our own code.
 *
 * Chess Online downloads a release, checks it (size, sha256, title ID, every file's CRC-32), unpacks it into
 * ux0:data/ChessOnline/update/new/, writes update/job.txt, starts this app and closes. Its own folder is locked while
 * it runs, so this helper does the copy:
 *   1. waits until ux0:app/CHESSONL1/ can be written (Chess Online has closed), at most 15 s;
 *   2. checks that new/sce_sys/param.sfo is Chess Online's (CHESSONL1);
 *   3. for each file that differs: backs the old one up into update/backup/, writes the new one as a .part file,
 *      reads it back and compares it, then puts it in place;
 *   4. on any failure puts every backed-up file back (and takes away files the update added);
 *   5. writes update/result.txt ("ok 1.0.1" or "failed <why>"), renames job.txt to job-done.txt, and reopens Chess
 *      Online, which shows the result in the player's own language.
 * On screen only the logo and a progress bar: no text to translate. Started with no job, it just opens Chess Online.
 * This app must be unsafe homebrew (it writes another app's folder); Chess Online itself stays a safe one. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <vita2d.h>

#include "sfo.h"

#define CHESS_ID "CHESSONL1"
#define APP_DIR "ux0:app/" CHESS_ID
#define DATA_DIR "ux0:data/ChessOnline"
#define UPD_DIR DATA_DIR "/update"
#define NEW_DIR UPD_DIR "/new"
#define BACKUP_DIR UPD_DIR "/backup"
#define JOB UPD_DIR "/job.txt"
#define JOB_DONE UPD_DIR "/job-done.txt"
#define RESULT UPD_DIR "/result.txt"
#define LOG UPD_DIR "/updater-log.txt"
#define MAX_FILES 1024
#define REL_MAX 200

static char rel[MAX_FILES][REL_MAX];
static unsigned char state_of[MAX_FILES]; /* 0 untouched, 1 replaced (old one backed up), 2 added (no old one) */
static int nfiles;
static unsigned char buf_a[128 * 1024], buf_b[128 * 1024];

static void logf_(const char *fmt, ...)
{
    char line[400];
    int n;
    va_list ap;
    SceDateTime t;
    SceUID fd;
    sceRtcGetCurrentClockLocalTime(&t);
    n = snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d ", t.year, t.month, t.day, t.hour, t.minute, t.second);
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    fd = sceIoOpen(LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, (SceSize)n);
        sceIoClose(fd);
    }
}

static int write_text(const char *path, const char *text)
{
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    int w;
    if (fd < 0) return -1;
    w = sceIoWrite(fd, text, (SceSize)strlen(text));
    sceIoClose(fd);
    return w == (int)strlen(text) ? 0 : -1;
}

static char *read_small(const char *path, size_t *len)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    SceOff size;
    char *b;
    if (fd < 0) return NULL;
    size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size < 0 || size > 1024 * 1024) {
        sceIoClose(fd);
        return NULL;
    }
    b = (char *)malloc((size_t)size + 1);
    if (b && sceIoRead(fd, b, (SceSize)size) != (int)size) {
        free(b);
        b = NULL;
    }
    sceIoClose(fd);
    if (b) b[size] = 0;
    if (len) *len = (size_t)size;
    return b;
}

static int exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

/* makes the folders of path's parent chain below the given root ("ux0:app/CHESSONL1" or the update folder) */
static void mkdirs_for(const char *root, const char *path)
{
    char b[400];
    size_t i;
    snprintf(b, sizeof b, "%s", path);
    for (i = strlen(root) + 1; b[i]; i++)
        if (b[i] == '/') {
            b[i] = 0;
            sceIoMkdir(b, 0777);
            b[i] = '/';
        }
}

static int copy_file(const char *from, const char *to)
{
    SceUID a = sceIoOpen(from, SCE_O_RDONLY, 0), b;
    int n, ok = 1;
    if (a < 0) return -1;
    b = sceIoOpen(to, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (b < 0) {
        sceIoClose(a);
        return -1;
    }
    while ((n = sceIoRead(a, buf_a, sizeof buf_a)) > 0)
        if (sceIoWrite(b, buf_a, (SceSize)n) != n) {
            ok = 0;
            break;
        }
    if (n < 0) ok = 0;
    sceIoClose(a);
    if (sceIoClose(b) < 0) ok = 0;
    return ok ? 0 : -1;
}

/* 1 when both files hold the same bytes */
static int same_file(const char *x, const char *y)
{
    SceUID a = sceIoOpen(x, SCE_O_RDONLY, 0), b;
    int same = 1;
    if (a < 0) return 0;
    b = sceIoOpen(y, SCE_O_RDONLY, 0);
    if (b < 0) {
        sceIoClose(a);
        return 0;
    }
    for (;;) {
        int n = sceIoRead(a, buf_a, sizeof buf_a), m = sceIoRead(b, buf_b, sizeof buf_b);
        if (n != m || n < 0 || memcmp(buf_a, buf_b, (size_t)(n > 0 ? n : 0))) {
            same = 0;
            break;
        }
        if (n == 0) break;
    }
    sceIoClose(a);
    sceIoClose(b);
    return same;
}

/* new file -> dest through dest.part, read back and compared before it replaces dest */
static int put_file(const char *from, const char *dest)
{
    char part[420];
    snprintf(part, sizeof part, "%s.part", dest);
    if (copy_file(from, part) || !same_file(from, part)) {
        sceIoRemove(part);
        return -1;
    }
    sceIoRemove(dest);
    return sceIoRename(part, dest) < 0 ? -1 : 0;
}

static void remove_tree(const char *path)
{
    if (strncmp(path, UPD_DIR "/", strlen(UPD_DIR) + 1) || strstr(path, "..")) return; /* only our own folders */
    for (;;) {
        SceUID d = sceIoDopen(path);
        SceIoDirent e;
        char child[400];
        int found = 0, dir = 0;
        if (d < 0) break;
        memset(&e, 0, sizeof e);
        while (sceIoDread(d, &e) > 0) {
            if (strcmp(e.d_name, ".") && strcmp(e.d_name, "..")) {
                snprintf(child, sizeof child, "%s/%s", path, e.d_name);
                dir = SCE_S_ISDIR(e.d_stat.st_mode);
                found = 1;
                break;
            }
            memset(&e, 0, sizeof e);
        }
        sceIoDclose(d);
        if (!found) break;
        if (dir) remove_tree(child);
        else if (sceIoRemove(child) < 0) break;
    }
    sceIoRmdir(path);
}

/* every regular file under NEW_DIR, as paths relative to it */
static int list_files(const char *sub)
{
    char path[400];
    SceUID d;
    SceIoDirent e;
    snprintf(path, sizeof path, NEW_DIR "%s%s", sub[0] ? "/" : "", sub);
    d = sceIoDopen(path);
    if (d < 0) return -1;
    memset(&e, 0, sizeof e);
    while (sceIoDread(d, &e) > 0) {
        char r[REL_MAX];
        if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) goto next;
        if (snprintf(r, sizeof r, "%s%s%s", sub, sub[0] ? "/" : "", e.d_name) >= (int)sizeof r) {
            sceIoDclose(d);
            return -1;
        }
        if (SCE_S_ISDIR(e.d_stat.st_mode)) {
            if (list_files(r)) {
                sceIoDclose(d);
                return -1;
            }
        } else {
            if (nfiles >= MAX_FILES) {
                sceIoDclose(d);
                return -1;
            }
            snprintf(rel[nfiles++], REL_MAX, "%s", r);
        }
    next:
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
    return 0;
}

/* puts back what the update changed so far; returns the number of files it could not put back */
static int roll_back(void)
{
    int i, bad = 0;
    for (i = 0; i < nfiles; i++) {
        char dest[400], bak[400];
        snprintf(dest, sizeof dest, APP_DIR "/%s", rel[i]);
        snprintf(bak, sizeof bak, BACKUP_DIR "/%s", rel[i]);
        if (state_of[i] == 1 && put_file(bak, dest)) bad++;
        if (state_of[i] == 2 && sceIoRemove(dest) < 0) bad++;
    }
    return bad;
}

/* Started inside Chess Online's own process (sceAppMgrLoadExec of app0:res/updater/eboot.bin, 1.0.1) it swaps back
 * to the new app0:eboot.bin; installed as its own app (CHESSUPD1) it opens Chess Online and closes. */
static void launch_chess_and_exit(void)
{
    char title[16] = "";
    int r;
    sceAppMgrAppParamGetString(0, 12, title, sizeof title); /* 12: the running app's title ID */
    if (!strcmp(title, CHESS_ID)) {
        logf_("back to app0:eboot.bin");
        r = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
        logf_("sceAppMgrLoadExec app0:eboot.bin: 0x%08x", (unsigned)r);
    } else {
        r = sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=" CHESS_ID);
        logf_("reopening " CHESS_ID ": 0x%08x", (unsigned)r);
    }
    sceKernelExitProcess(0);
}

/* the screen: the logo and a progress bar, no text */
static vita2d_texture *logo;
static void draw(float part)
{
    float w = 560 * (part < 0 ? 0 : part > 1 ? 1 : part);
    vita2d_start_drawing();
    vita2d_clear_screen();
    if (logo)
        vita2d_draw_texture(logo, 480 - vita2d_texture_get_width(logo) / 2.0f, 230 - vita2d_texture_get_height(logo) / 2.0f);
    vita2d_draw_rectangle(200, 380, 560, 14, RGBA8(36, 64, 104, 255));
    vita2d_draw_rectangle(200, 380, w, 14, RGBA8(0, 170, 228, 255));
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

int main(void)
{
    char *job, msg[300], ver[32] = "", title[16];
    unsigned char *sfo;
    size_t sfo_len = 0;
    int i, waited, changed = 0;

    vita2d_init();
    vita2d_set_clear_color(RGBA8(28, 52, 86, 255));
    logo = vita2d_load_PNG_file("app0:res/logo140.png");
    draw(0);

    job = read_small(JOB, NULL);
    if (!job) {
        logf_("no job: opening Chess Online");
        launch_chess_and_exit();
        return 0;
    }
    sscanf(job, "v1 %31s", ver);
    free(job);
    {
        char title[16] = "";
        sceAppMgrAppParamGetString(0, 12, title, sizeof title);
        logf_("job: install Chess Online %s (running as %s)", ver[0] ? ver : "(no version)", title[0] ? title : "?");
    }

    /* 1. Chess Online has closed when its folder can be written */
    for (waited = 0;; waited += 250) {
        SceUID fd = sceIoOpen(APP_DIR "/update.probe", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (fd >= 0) {
            sceIoClose(fd);
            sceIoRemove(APP_DIR "/update.probe");
            break;
        }
        if (waited >= 15000) {
            snprintf(msg, sizeof msg, "failed " APP_DIR " stayed locked (0x%08x)\n", (unsigned)fd);
            goto finish;
        }
        draw(0);
        sceKernelDelayThread(250 * 1000);
    }

    /* 2. the unpacked package must be Chess Online's */
    sfo = (unsigned char *)read_small(NEW_DIR "/sce_sys/param.sfo", &sfo_len);
    if (!sfo || sfo_get_str(sfo, sfo_len, "TITLE_ID", title, sizeof title) || strcmp(title, CHESS_ID) ||
        !exists(NEW_DIR "/eboot.bin")) {
        free(sfo);
        snprintf(msg, sizeof msg, "failed the unpacked update is not Chess Online's\n");
        goto finish;
    }
    free(sfo);
    if (list_files("")) {
        snprintf(msg, sizeof msg, "failed can't list the unpacked update\n");
        goto finish;
    }
    remove_tree(BACKUP_DIR);
    sceIoMkdir(BACKUP_DIR, 0777);

    /* 3. file by file, backing up what is replaced */
    for (i = 0; i < nfiles; i++) {
        char src[400], dest[400], bak[400];
        snprintf(src, sizeof src, NEW_DIR "/%s", rel[i]);
        snprintf(dest, sizeof dest, APP_DIR "/%s", rel[i]);
        snprintf(bak, sizeof bak, BACKUP_DIR "/%s", rel[i]);
        draw((float)i / (float)nfiles);
        if (exists(dest)) {
            if (same_file(src, dest)) continue;
            mkdirs_for(BACKUP_DIR, bak);
            if (copy_file(dest, bak) || !same_file(dest, bak)) {
                snprintf(msg, sizeof msg, "failed can't back up %s\n", rel[i]);
                goto undo;
            }
            state_of[i] = 1;
        } else {
            mkdirs_for(APP_DIR, dest);
            state_of[i] = 2;
        }
        if (put_file(src, dest)) {
            snprintf(msg, sizeof msg, "failed can't write %s\n", rel[i]);
            goto undo;
        }
        changed++;
    }
    draw(1);
    logf_("%d of %d files changed", changed, nfiles);
    snprintf(msg, sizeof msg, "ok %s\n", ver);
    goto finish;

undo:
    logf_("%.*s; putting the old files back", (int)strlen(msg) - 1, msg);
    i = roll_back();
    if (i) {
        size_t n = strlen(msg);
        if (n) msg[n - 1] = 0;
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " (and %d files could not be put back)\n", i);
    }

finish:
    logf_("%.*s", (int)strlen(msg) - 1, msg);
    write_text(RESULT, msg);
    sceIoRemove(JOB_DONE);
    sceIoRename(JOB, JOB_DONE);
    launch_chess_and_exit();
    return 0;
}
