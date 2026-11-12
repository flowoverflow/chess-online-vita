/* Chess Online: the app's own updater, Vita side (1.0.1). Our own code; see updateui.h. */
#include "updateui.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/sha.h>
#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include "guest.h"
#include "sfo.h"
#include "ui.h"
#include "unzip.h"
#include "update.h"

#define UPD_DIR CO_DATA_DIR "/update"
#define UPD_NEW UPD_DIR "/new"
#define UPD_JOB UPD_DIR "/job.txt"
#define UPD_RESULT UPD_DIR "/result.txt"
#define UPD_RESULT_SEEN UPD_DIR "/result-seen.txt"

enum { U_NONE, U_OFFER, U_WORKING, U_LAUNCH, U_RESULT };
enum { P_DOWNLOAD = 1, P_CHECK, P_UNPACK, P_DONE, P_FAILED, P_CANCELLED, P_NO_HELPER };
enum { R_INSTALLED = 1, R_FAILED, R_MANUAL };

static struct {
    const char *version;
    int state, sel, result;
    int offer_ready, asked, retries;
    uint64_t retry_at, launch_at;
    upd_release rel;
    volatile int phase, cancel;
    volatile unsigned long long done, total;
    char result_ver[32];
    char vpk_path[192];
} U;

/* ---------- files under ux0:data/ChessOnline/update ---------- */
static int under_update_dir(const char *p)
{
    return !strncmp(p, UPD_DIR "/", strlen(UPD_DIR) + 1) && !strstr(p, "..");
}

/* makes every folder of path's parent chain below ux0:data/ChessOnline */
static void mkdirs_for(const char *path)
{
    char buf[320];
    size_t i;
    snprintf(buf, sizeof buf, "%s", path);
    for (i = strlen(CO_DATA_DIR) + 1; buf[i]; i++)
        if (buf[i] == '/') {
            buf[i] = 0;
            sceIoMkdir(buf, 0777);
            buf[i] = '/';
        }
}

/* removes a folder of the update's own files (only below update/), one entry at a time */
static void remove_tree(const char *path)
{
    if (!under_update_dir(path)) return;
    for (;;) {
        SceUID d = sceIoDopen(path);
        SceIoDirent e;
        char child[320];
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

static int sha256_file(const char *path, char hex[65], unsigned long long *size)
{
    static unsigned char buf[64 * 1024]; /* one worker at a time */
    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256_CTX c;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    int n, i;
    if (fd < 0) return -1;
    SHA256_Init(&c);
    *size = 0;
    while ((n = sceIoRead(fd, buf, sizeof buf)) > 0) {
        SHA256_Update(&c, buf, (size_t)n);
        *size += (unsigned)n;
    }
    sceIoClose(fd);
    if (n < 0) return -1;
    SHA256_Final(md, &c);
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) snprintf(hex + 2 * i, 3, "%02x", md[i]);
    return 0;
}

/* ---------- the helper app, carried inside this VPK (one VPK for the player, the user's wish 2026-11-12) ----------
 * Observed 2026-11-12: the system refuses writes into ux0:app/CHESSONL1 while any of Chess Online's programs runs
 * (0x8001000D, also after sceAppMgrLoadExec into a second program of the app). So a second app does the copy. Its
 * files ride along in app0:res/updater/, with the package header the installer needs (sce_sys/package/head.bin,
 * made at build time by tools/chessonline/headbin, a separate GPL-3.0 tool after VitaShell). Before an update this
 * app installs it with the system's installer (the promoter, as VitaShell does: PromotePkgWithRif) and starts it,
 * and on its next start it removes it again, so no second bubble stays on the home screen. The promoter module loads only after the PAF module (0x8002D003 without it): pkgj
 * (BSD-2-Clause, src/vita.cpp) does the same, and the PAF arguments below are the values it uses. */
#define HELPER_SRC "app0:res/updater"
#define HELPER_TMP UPD_DIR "/helper" /* the staged helper package */
#define HELPER_APP "ux0:app/" UPD_HELPER_TITLE_ID
static const char *const HELPER_FILES[] = {
    "eboot.bin", "sce_sys/param.sfo", "sce_sys/icon0.png", "sce_sys/livearea/contents/bg.png",
    "sce_sys/livearea/contents/startup.png", "sce_sys/livearea/contents/template.xml", "res/logo140.png",
    "sce_sys/package/head.bin"};
static pthread_mutex_t promoter_lock = PTHREAD_MUTEX_INITIALIZER;
static int promoter_ready;

static int promoter_up(void)
{
    uint32_t paf_args[6] = {0x00400000, 0x0000ea60, 0x00040000, 0, 1, 0};
    int paf_res = 0, r = 0;
    SceSysmoduleOpt opt;
    pthread_mutex_lock(&promoter_lock);
    if (!promoter_ready) {
        memset(&opt, 0, sizeof opt);
        opt.result = &paf_res;
        plat_log("update: loading the PAF and installer modules");
        r = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof paf_args, paf_args, &opt);
        if (r < 0) plat_log("update: the PAF module won't load (0x%08x, 0x%08x)", (unsigned)r, (unsigned)paf_res);
        if (r >= 0) {
            r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
            if (r < 0) plat_log("update: the system installer module won't load (0x%08x)", (unsigned)r);
        }
        if (r >= 0) {
            r = scePromoterUtilityInit();
            if (r < 0) plat_log("update: scePromoterUtilityInit 0x%08x", (unsigned)r);
        }
        if (r >= 0) {
            promoter_ready = 1;
            plat_log("update: the system installer is ready");
        }
    }
    pthread_mutex_unlock(&promoter_lock);
    return promoter_ready ? 0 : -1;
}

static int exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

static int copy_file(const char *from, const char *to)
{
    static unsigned char buf[64 * 1024];
    SceUID a = sceIoOpen(from, SCE_O_RDONLY, 0), b;
    int n, ok = 1;
    if (a < 0) return -1;
    b = sceIoOpen(to, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (b < 0) {
        sceIoClose(a);
        return -1;
    }
    while ((n = sceIoRead(a, buf, sizeof buf)) > 0)
        if (sceIoWrite(b, buf, (SceSize)n) != n) {
            ok = 0;
            break;
        }
    if (n < 0) ok = 0;
    sceIoClose(a);
    sceIoClose(b);
    return ok ? 0 : -1;
}

static int same_file(const char *x, const char *y)
{
    size_t lx = 0, ly = 0;
    char *a = plat_read_file(x, &lx), *b = plat_read_file(y, &ly);
    int same = a && b && lx == ly && !memcmp(a, b, lx);
    free(a);
    free(b);
    return same;
}

static int stage_helper(void)
{
    unsigned i;
    sceIoMkdir(UPD_DIR, 0777);
    sceIoMkdir(HELPER_TMP, 0777);
    for (i = 0; i < sizeof HELPER_FILES / sizeof HELPER_FILES[0]; i++) {
        char from[160], to[160], dir[160];
        char *slash;
        snprintf(from, sizeof from, HELPER_SRC "/%s", HELPER_FILES[i]);
        snprintf(to, sizeof to, HELPER_TMP "/%s", HELPER_FILES[i]);
        snprintf(dir, sizeof dir, "%s", to); /* the folders on the way */
        for (slash = dir + strlen(HELPER_TMP) + 1; (slash = strchr(slash, '/')) != NULL; slash++) {
            *slash = 0;
            sceIoMkdir(dir, 0777);
            *slash = '/';
        }
        if (copy_file(from, to)) {
            plat_log("update: can not stage the helper file %s", HELPER_FILES[i]);
            return -1;
        }
    }
    return 0;
}

/* waits for the promoter's job; returns its result */
static int wait_promoter(const char *what)
{
    int state = 1, res = 0, i;
    for (i = 0; i < 600 && state; i++) { /* at most a minute */
        sceKernelDelayThread(100 * 1000);
        if (scePromoterUtilityGetState(&state) < 0) break;
    }
    scePromoterUtilityGetResult(&res);
    plat_log("update: %s: result 0x%08x", what, (unsigned)res);
    return res;
}

/* 0 when the helper app that matches this version's is installed, installing it when it is not */
static int ensure_helper(void)
{
    int attempt;
    if (exists(HELPER_APP "/eboot.bin") && same_file(HELPER_APP "/eboot.bin", HELPER_SRC "/eboot.bin")) {
        plat_log("update: the helper app is already installed");
        return 0;
    }
    if (promoter_up()) return -1;
    if (exists(HELPER_APP "/eboot.bin")) { /* another version of the helper: replace it */
        int r;
        plat_log("update: removing the other helper app");
        r = scePromoterUtilityDeletePkg(UPD_HELPER_TITLE_ID);
        plat_log("update: removing the other helper app: 0x%08x", (unsigned)r);
        if (r >= 0) wait_promoter("removing the other helper app");
    }
    remove_tree(HELPER_TMP); /* a helper staged by an earlier, interrupted try */
    /* as VitaShell installs a package; the plain package call after it (both answers logged) */
    for (attempt = 0; attempt < 2; attempt++) {
        int r;
        if (stage_helper()) {
            remove_tree(HELPER_TMP);
            return -1;
        }
        plat_log("update: the helper is staged in " HELPER_TMP "; installing it (way %d)", attempt);
        if (attempt == 0) r = scePromoterUtilityPromotePkgWithRif(HELPER_TMP, 1);
        else r = scePromoterUtilityPromotePkg(HELPER_TMP, 1);
        plat_log("update: installing the helper app (way %d) returned 0x%08x", attempt, (unsigned)r);
        if (r >= 0) wait_promoter("installing the helper app");
        remove_tree(HELPER_TMP);
        if (exists(HELPER_APP "/eboot.bin") && same_file(HELPER_APP "/eboot.bin", HELPER_SRC "/eboot.bin")) {
            plat_log("update: the helper app is installed (way %d)", attempt);
            return 0;
        }
    }
    plat_log("update: the helper app could not be installed");
    return -1;
}

/* the next start after an update: the downloaded package, the unpacked copy, the backup and the staged helper go
 * (about 10 MB); the logs, job-done.txt and result-seen.txt stay */
static void cleanup_after_update(void)
{
    SceUID d = sceIoDopen(UPD_DIR);
    SceIoDirent e;
    remove_tree(UPD_NEW);
    remove_tree(UPD_DIR "/backup");
    remove_tree(HELPER_TMP);
    if (d < 0) return;
    memset(&e, 0, sizeof e);
    while (sceIoDread(d, &e) > 0) {
        size_t n = strlen(e.d_name);
        if (!SCE_S_ISDIR(e.d_stat.st_mode) && n > 4 && !strcmp(e.d_name + n - 4, ".vpk")) {
            char path[256];
            snprintf(path, sizeof path, UPD_DIR "/%s", e.d_name);
            sceIoRemove(path);
            plat_log("update: removed the downloaded %s", e.d_name);
        }
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
}

/* the next start after an update: the helper has done its job, so its bubble goes again */
static void *remove_helper(void *arg)
{
    int r;
    (void)arg;
    if (!exists(HELPER_APP "/eboot.bin") || promoter_up()) return NULL;
    /* VitaShell closes the other apps before it deletes one (sceAppMgrDestroyOtherApp); without it, deleting sent Chess
     * Online to the background until it was opened again (2026-11-12) */
    r = sceAppMgrDestroyOtherApp();
    plat_log("update: removing the helper app (closing other apps first: 0x%08x)", (unsigned)r);
    r = scePromoterUtilityDeletePkg(UPD_HELPER_TITLE_ID);
    plat_log("update: removing the helper app: 0x%08x", (unsigned)r);
    if (r >= 0) wait_promoter("removing the helper app");
    return NULL;
}

static void start_thread(void *(*fn)(void *))
{
    pthread_attr_t at;
    pthread_t th;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 512 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, fn, NULL)) plat_log("update: can not start a thread");
    pthread_attr_destroy(&at);
}

/* ---------- the worker: download, check, unpack ---------- */
static void *fail(const char *why)
{
    plat_log("update: %s", why);
    U.phase = P_FAILED;
    return NULL;
}

static void *worker(void *arg)
{
    char part[200], msg[200], hex[65], title[16];
    unsigned long long size = 0;
    int http = 0, rc = 0, ok;
    size_t len = 0, pos;
    unsigned char *vpk, *sfo;
    unz_archive z;
    unz_entry e;
    unsigned files = 0;
    (void)arg;
    sceIoMkdir(UPD_DIR, 0777);
    snprintf(U.vpk_path, sizeof U.vpk_path, UPD_DIR "/%s", U.rel.asset);
    snprintf(part, sizeof part, "%s.part", U.vpk_path);
    if (net_download(U.rel.url, part, &U.done, &U.total, &U.cancel, &http, &rc)) {
        sceIoRemove(part);
        if (U.cancel) {
            plat_log("update: download cancelled");
            U.phase = P_CANCELLED;
            return NULL;
        }
        snprintf(msg, sizeof msg, "download failed (HTTP %d, %s)", http, net_error_text(rc));
        return fail(msg);
    }

    U.phase = P_CHECK;
    if (sha256_file(part, hex, &size)) return fail("can't read the download");
    if (size != U.rel.size || strcmp(hex, U.rel.sha256)) {
        sceIoRemove(part);
        snprintf(msg, sizeof msg, "the download doesn't match the release (%llu bytes, sha256 %.16s...)", size, hex);
        return fail(msg);
    }
    if (plat_rename_file(part, U.vpk_path)) return fail("can't keep the download");
    plat_log("update: %s checked: %llu bytes, sha256 %s (as the release says)", U.rel.asset, size, hex);

    U.phase = P_UNPACK;
    U.done = 0;
    vpk = (unsigned char *)plat_read_file(U.vpk_path, &len);
    if (!vpk) return fail("not enough memory to read the package");
    if (unz_open(&z, vpk, len) || unz_find(&z, "sce_sys/param.sfo", &e) != 1) {
        free(vpk);
        return fail("the download is not a VPK");
    }
    sfo = (unsigned char *)malloc(e.usize + 1);
    ok = sfo && unz_extract(&z, &e, sfo, e.usize) == 0 && sfo_get_str(sfo, e.usize, "TITLE_ID", title, sizeof title) == 0;
    free(sfo);
    if (!ok || strcmp(title, UPD_TITLE_ID)) {
        free(vpk);
        snprintf(msg, sizeof msg, "the package is not Chess Online's (title ID %s)", ok ? title : "missing");
        return fail(msg);
    }
    U.total = z.cd_end - z.cd_off;
    remove_tree(UPD_NEW);
    sceIoMkdir(UPD_NEW, 0777);
    pos = z.cd_off;
    for (;;) {
        char dest[320];
        unsigned char *out;
        int r = unz_next(&z, &pos, &e);
        if (r == 0) break;
        if (r < 0 || !upd_safe_path(e.name)) {
            free(vpk);
            return fail("a damaged or unsafe entry in the package");
        }
        snprintf(dest, sizeof dest, UPD_NEW "/%s", e.name);
        mkdirs_for(dest);
        U.done = pos - z.cd_off;
        if (e.is_dir) continue;
        out = (unsigned char *)malloc(e.usize ? e.usize : 1);
        if (!out || unz_extract(&z, &e, out, e.usize) || plat_write_file(dest, out, e.usize)) {
            free(out);
            free(vpk);
            snprintf(msg, sizeof msg, "can't unpack %s", e.name);
            return fail(msg);
        }
        free(out);
        files++;
    }
    free(vpk);
    snprintf(msg, sizeof msg, "v1 %s\n%s\n", U.rel.version, U.rel.asset);
    if (plat_write_file(UPD_JOB, msg, strlen(msg))) return fail("can't write the job file");
    plat_log("update: %u files unpacked into " UPD_NEW "; the job is written", files);
    U.phase = ensure_helper() ? P_NO_HELPER : P_DONE;
    return NULL;
}

static void start_worker(void)
{
    U.phase = P_DOWNLOAD;
    U.cancel = 0;
    U.done = 0;
    U.total = U.rel.size;
    U.state = U_WORKING;
    start_thread(worker);
    plat_log("update: installing %s", U.rel.tag);
}

static void launch_helper(void)
{
    int r;
    net_cmg_close();
    r = sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=" UPD_HELPER_TITLE_ID);
    plat_log("update: starting " UPD_HELPER_TITLE_ID ": 0x%08x", (unsigned)r);
    if (r < 0) {
        guest_start(); /* still here: the lobby connection comes back */
        U.state = U_RESULT;
        U.result = R_MANUAL;
        return;
    }
    sceKernelExitProcess(0);
}

/* ---------- check ---------- */

void update_init(const char *version, uint64_t now)
{
    char *s = plat_read_file(UPD_RESULT, NULL);
    (void)now;
    U.version = version;
    if (s) {
        char word[16] = "", ver[32] = "";
        sscanf(s, "%15s %31s", word, ver);
        plat_log("update: the helper's result: %.150s", s);
        if (!strcmp(word, "ok") && !upd_version_cmp(ver, version)) {
            U.result = R_INSTALLED;
            snprintf(U.result_ver, sizeof U.result_ver, "%s", ver);
            U.state = U_RESULT;
        } else if (!strcmp(word, "failed")) {
            U.result = R_FAILED;
            U.state = U_RESULT;
        }
        free(s);
        plat_rename_file(UPD_RESULT, UPD_RESULT_SEEN);
        start_thread(remove_helper);
        cleanup_after_update();
    }
    net_request_url(NET_TAG_UPDATE, "GET", UPD_API_URL, NULL, 0);
}

void update_on_response(const net_msg *m, uint64_t now)
{
    upd_release r;
    int p;
    if (m->status != 200) {
        plat_log("update: the release check got HTTP %d (%s)", m->status, net_error_text(m->curl_rc));
        if (m->status == 0 && U.retries < 3) { /* no network yet, or Wi-Fi still waking up: in 30, 60, 120 s */
            U.retry_at = now + (30000ULL << U.retries);
            U.retries++;
        }
        return;
    }
    p = upd_parse_release(m->data ? m->data : "", m->len, &r);
    if (p) {
        plat_log("update: the latest release can't be used (%d)", p);
        return;
    }
    if (upd_version_cmp(r.version, U.version) <= 0) {
        plat_log("update: up to date (%s; the latest release is %s)", U.version, r.tag);
        return;
    }
    U.rel = r;
    U.offer_ready = 1;
    plat_log("update: %s is available (%s, %llu bytes)", r.tag, r.asset, r.size);
}

void update_timers(uint64_t now)
{
    if (U.retry_at && now >= U.retry_at) {
        U.retry_at = 0;
        net_request_url(NET_TAG_UPDATE, "GET", UPD_API_URL, NULL, 0);
    }
    if (U.state == U_NONE && U.offer_ready && !U.asked) {
        U.state = U_OFFER;
        U.sel = 0;
    }
    if (U.state == U_WORKING) {
        if (U.phase == P_DONE) {
            U.state = U_LAUNCH;
            U.launch_at = now + 1800;
        } else if (U.phase == P_FAILED) {
            U.state = U_RESULT;
            U.result = R_FAILED;
        } else if (U.phase == P_NO_HELPER) { /* the files are ready, but only VitaShell can install them */
            U.state = U_RESULT;
            U.result = R_MANUAL;
        } else if (U.phase == P_CANCELLED) {
            U.state = U_NONE;
            U.asked = 1;
        }
    }
    /* never while an online game is on (a waiting game can be matched while the update downloads) */
    if (U.state == U_LAUNCH && now >= U.launch_at && !guest_playing()) launch_helper();
}

int update_showing(void)
{
    return U.state != U_NONE;
}

/* ---------- the dialogs ---------- */
#define CARD_X 240
#define CARD_Y 140
#define CARD_W 480
#define CARD_H 264
#define TEXT_W 440
#define BTN_Y 312

static int two_hit(int px, int py)
{
    if (ui_hit(px, py, 270, BTN_Y, 190, 58)) return 0;
    if (ui_hit(px, py, 500, BTN_Y, 190, 58)) return 1;
    return -1;
}

static int one_hit(int px, int py)
{
    return ui_hit(px, py, 385, BTN_Y, 190, 58) ? 1 : -1;
}

int update_input(const plat_input *in, uint64_t now)
{
    int hit;
    (void)now;
    switch (U.state) {
    case U_OFFER:
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) U.sel ^= 1;
        hit = PLAT_TAP(in, two_hit);
        if (hit >= 0) U.sel = hit;
        if ((in->pressed & BTN_OK) || hit >= 0) {
            if (U.sel == 0) {
                start_worker();
            } else {
                U.asked = 1;
                U.state = U_NONE;
                plat_log("update: asked later");
            }
        } else if (in->pressed & BTN_BACK) {
            U.asked = 1;
            U.state = U_NONE;
            plat_log("update: asked later");
        }
        return 1;
    case U_WORKING:
        if (U.phase == P_DOWNLOAD && ((in->pressed & BTN_BACK) || PLAT_TAP(in, one_hit) == 1)) U.cancel = 1;
        return 1;
    case U_LAUNCH:
        return 1;
    case U_RESULT:
        if ((in->pressed & (BTN_OK | BTN_BACK)) || PLAT_TAP(in, one_hit) == 1) {
            U.state = U_NONE;
            U.result = 0;
        }
        return 1;
    default:
        return 0;
    }
}

/* one line, centred, shrunk a little when the language's text is wider than the card, cut only as a last resort */
static void fit_center(int font, int y, unsigned c, int size, int min, const char *t)
{
    int s = size;
    while (s > min && ui_text_w(font, s, t) > TEXT_W) s--;
    if (ui_text_w(font, s, t) > TEXT_W) ui_text_fit(font, CARD_X + 20, y, c, s, TEXT_W, t);
    else ui_text_center(font, 480, y, c, s, t);
}

static void card(const char *title)
{
    ui_dim();
    ui_card(CARD_X, CARD_Y, CARD_W, CARD_H, 24, C_CARD);
    fit_center(F_XBOLD, CARD_Y + 64, C_INK, 30, 22, title);
}

static void bar(float part)
{
    float w = 400 * (part < 0 ? 0 : part > 1 ? 1 : part);
    ui_round(280, CARD_Y + 96, 400, 16, 8, C_FIELD_LINE);
    if (w >= 16) ui_round(280, CARD_Y + 96, w, 16, 8, C_CYAN);
}

void update_draw(uint64_t now)
{
    char buf[160];
    (void)now;
    switch (U.state) {
    case U_OFFER:
        card("Update available");
        snprintf(buf, sizeof buf, "Version %s is available.", U.rel.version);
        fit_center(F_REG, CARD_Y + 104, C_INK_SOFT, 19, 14, buf);
        fit_center(F_REG, CARD_Y + 130, C_INK_SOFT, 19, 14, "Install it now?");
        ui_button(270, BTN_Y, 190, 54, C_GOLD, C_INK, "Install", U.sel == 0);
        ui_button(500, BTN_Y, 190, 54, C_CYAN, C_WHITE, "Ask me later", U.sel == 1);
        break;
    case U_WORKING:
    case U_LAUNCH: {
        int ph = U.state == U_LAUNCH ? P_DONE : U.phase;
        unsigned long long done = U.done, total = U.total;
        card(ph == P_DOWNLOAD ? "Downloading the update" : ph == P_CHECK ? "Checking the update" : "Unpacking the update");
        bar(ph == P_CHECK || ph == P_DONE ? 1.0f : total ? (float)done / (float)total : 0.0f);
        if (ph == P_DOWNLOAD) {
            snprintf(buf, sizeof buf, "%u of %u MB", (unsigned)((done + 524288) / 1048576),
                     (unsigned)((total + 524288) / 1048576));
            fit_center(F_BOLD, CARD_Y + 146, C_INK_SOFT, 18, 14, buf);
            ui_button(385, BTN_Y, 190, 54, C_INK_SOFT, C_WHITE, "Cancel", 1);
        } else if (ph == P_DONE) {
            const char *t = "Chess Online closes and reopens to finish.";
            if (ui_text_w(F_REG, 15, t) <= TEXT_W) fit_center(F_REG, CARD_Y + 150, C_INK_SOFT, 19, 15, t);
            else ui_text_wrap(F_REG, CARD_X + 20, CARD_Y + 146, C_INK_SOFT, 17, TEXT_W, 22, t);
        }
        break;
    }
    case U_RESULT:
        if (U.result == R_INSTALLED) {
            card("Update installed");
            snprintf(buf, sizeof buf, "Chess Online is now version %s.", U.result_ver);
            fit_center(F_REG, CARD_Y + 116, C_INK_SOFT, 19, 14, buf);
        } else if (U.result == R_MANUAL) {
            const char *p = U.vpk_path;
            int s = 16;
            card("Update downloaded");
            fit_center(F_REG, CARD_Y + 104, C_INK_SOFT, 19, 14, "Install it with VitaShell:");
            while (s > 12 && ui_text_w_raw(F_BOLD, s, p) > TEXT_W) s--;
            ui_text_raw(F_BOLD, 480 - ui_text_w_raw(F_BOLD, s, p) / 2, CARD_Y + 134, C_INK, s, p);
        } else {
            card("Update not installed");
            fit_center(F_REG, CARD_Y + 116, C_INK_SOFT, 19, 14, "Your version was kept. Try again later.");
        }
        ui_button(385, BTN_Y, 190, 54, C_GOLD, C_INK, "Close", 1);
        break;
    default:
        break;
    }
}
