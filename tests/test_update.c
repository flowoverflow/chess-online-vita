/* Chess Online: host tests for the updater's portable part (1.0.1): versions, GitHub's release answer, package
 * names, the zip reader and param.sfo. Usage: test_update good.zip bad.zip [app.vpk]
 * good.zip and bad.zip are written by scripts' Python step (host-tests.sh); app.vpk, when given, is a real build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sfo.h"
#include "unzip.h"
#include "update.h"

static int checks, failed;
#define CHECK(c)                                                                \
    do {                                                                        \
        checks++;                                                               \
        if (!(c)) {                                                             \
            failed++;                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                 \
        }                                                                       \
    } while (0)

static unsigned char *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static void test_versions(void)
{
    CHECK(upd_version_cmp("1.0.1", "1.0.0") > 0);
    CHECK(upd_version_cmp("v1.0.1", "1.0.1") == 0);
    CHECK(upd_version_cmp("1.0.10", "1.0.9") > 0);
    CHECK(upd_version_cmp("1.1", "1.0.9") > 0);
    CHECK(upd_version_cmp("1.0", "1.0.0") == 0);
    CHECK(upd_version_cmp("2.0.0", "10.0.0") < 0);
    CHECK(upd_version_cmp("1.0.1-rc1", "1.0.1") < 0);
    CHECK(upd_version_cmp("1.0.1", "1.0.1-rc1") > 0);
    CHECK(upd_version_cmp("0.9.9", "v1.0.0") < 0);
}

/* shaped like api.github.com's answer of 2026-11-12 for v1.0.0, with the helper app's asset listed first */
static const char *RELEASE =
    "{\"url\":\"https://api.github.com/repos/flowoverflow/chess-online-vita/releases/1\",\"tag_name\":\"v1.0.1\","
    "\"name\":\"Chess Online 1.0.1\",\"draft\":false,\"prerelease\":false,\"assets\":["
    "{\"name\":\"ChessOnlineUpdater-1.0.vpk\",\"size\":400000,\"digest\":\"sha256:"
    "1111111111111111111111111111111111111111111111111111111111111111\","
    "\"browser_download_url\":\"https://github.com/flowoverflow/chess-online-vita/releases/download/v1.0.1/"
    "ChessOnlineUpdater-1.0.vpk\"},"
    "{\"name\":\"ChessOnline-1.0.1.vpk\",\"size\":5194932,\"digest\":\"sha256:"
    "3A07376A70884AB3C5974EB62ADA9E50E3F6E3042228E49406532AD4355274A0\","
    "\"content_type\":\"application/octet-stream\","
    "\"browser_download_url\":\"https://github.com/flowoverflow/chess-online-vita/releases/download/v1.0.1/"
    "ChessOnline-1.0.1.vpk\"}],\"body\":\"notes \\u00e9\"}";

static void test_release(void)
{
    upd_release r;
    char buf[2048];
    CHECK(upd_parse_release(RELEASE, strlen(RELEASE), &r) == 0);
    CHECK(!strcmp(r.tag, "v1.0.1"));
    CHECK(!strcmp(r.version, "1.0.1"));
    CHECK(!strcmp(r.asset, "ChessOnline-1.0.1.vpk")); /* not the helper's ChessOnlineUpdater-... */
    CHECK(r.size == 5194932ULL);
    CHECK(!strcmp(r.sha256, "3a07376a70884ab3c5974eb62ada9e50e3f6e3042228e49406532ad4355274a0"));
    CHECK(strstr(r.url, "/releases/download/v1.0.1/ChessOnline-1.0.1.vpk") != NULL);

    CHECK(upd_parse_release("not json", 8, &r) == -1);
    CHECK(upd_parse_release("{\"assets\":[]}", 13, &r) == -1); /* no tag */
    snprintf(buf, sizeof buf, "{\"tag_name\":\"v2.0.0\",\"assets\":[]}");
    CHECK(upd_parse_release(buf, strlen(buf), &r) == -2);
    snprintf(buf, sizeof buf, "{\"tag_name\":\"v2.0.0\",\"draft\":true,\"assets\":[]}");
    CHECK(upd_parse_release(buf, strlen(buf), &r) == -1);
    snprintf(buf, sizeof buf, "{\"tag_name\":\"v2.0.0\",\"prerelease\":true,\"assets\":[]}");
    CHECK(upd_parse_release(buf, strlen(buf), &r) == -1);
    /* an asset without a digest, or with a URL off github.com, is refused */
    snprintf(buf, sizeof buf,
             "{\"tag_name\":\"v2.0.0\",\"assets\":[{\"name\":\"ChessOnline-2.0.0.vpk\",\"size\":10,"
             "\"browser_download_url\":\"https://github.com/x/y/releases/download/v2.0.0/ChessOnline-2.0.0.vpk\"}]}");
    CHECK(upd_parse_release(buf, strlen(buf), &r) == -3);
    snprintf(buf, sizeof buf,
             "{\"tag_name\":\"v2.0.0\",\"assets\":[{\"name\":\"ChessOnline-2.0.0.vpk\",\"size\":10,\"digest\":\"sha256:"
             "3a07376a70884ab3c5974eb62ada9e50e3f6e3042228e49406532ad4355274a0\","
             "\"browser_download_url\":\"http://example.com/ChessOnline-2.0.0.vpk\"}]}");
    CHECK(upd_parse_release(buf, strlen(buf), &r) == -3);
    snprintf(buf, sizeof buf,
             "{\"tag_name\":\"v2.0.0\",\"assets\":[{\"name\":\"ChessOnline-2.0.0.vpk\",\"size\":10,\"digest\":\"md5:abc\","
             "\"browser_download_url\":\"https://github.com/x/y/releases/download/v2.0.0/ChessOnline-2.0.0.vpk\"}]}");
    CHECK(upd_parse_release(buf, strlen(buf), &r) == -3);
}

static void test_paths(void)
{
    CHECK(upd_safe_path("eboot.bin"));
    CHECK(upd_safe_path("res/fonts/NotoSansJP-Bold.ttf"));
    CHECK(upd_safe_path("res/flags/"));
    CHECK(!upd_safe_path(""));
    CHECK(!upd_safe_path("/eboot.bin"));
    CHECK(!upd_safe_path("../eboot.bin"));
    CHECK(!upd_safe_path("res/../../x"));
    CHECK(!upd_safe_path("res/./x"));
    CHECK(!upd_safe_path("res//x"));
    CHECK(!upd_safe_path("ux0:app/x"));
    CHECK(!upd_safe_path("res\\x"));
    CHECK(!upd_safe_path("res/\x01x"));
}

static void test_good_zip(const char *path)
{
    size_t len = 0, pos;
    unsigned char *d = read_all(path, &len);
    unz_archive z;
    unz_entry e;
    int n = 0, dirs = 0, ok = 1, r;
    CHECK(d != NULL);
    if (!d) return;
    CHECK(unz_open(&z, d, len) == 0);
    CHECK(z.count == 5);
    pos = z.cd_off;
    while ((r = unz_next(&z, &pos, &e)) == 1) {
        n++;
        CHECK(upd_safe_path(e.name));
        if (e.is_dir) {
            dirs++;
            continue;
        }
        {
            unsigned char *out = (unsigned char *)malloc(e.usize + 1);
            if (unz_extract(&z, &e, out, e.usize) != 0) ok = 0;
            if (!strcmp(e.name, "res/a.txt")) CHECK(e.usize == 4000 && out[0] == 'a' && out[3999] == 'a' && e.method == 8);
            if (!strcmp(e.name, "res/empty.txt")) CHECK(e.usize == 0);
            free(out);
        }
    }
    CHECK(r == 0);
    CHECK(n == 5 && dirs == 1 && ok);
    {
        unsigned char sfo[256];
        char s[32];
        CHECK(unz_find(&z, "sce_sys/param.sfo", &e) == 1);
        CHECK(e.usize <= sizeof sfo && unz_extract(&z, &e, sfo, sizeof sfo) == 0);
        CHECK(sfo_get_str(sfo, e.usize, "TITLE_ID", s, sizeof s) == 0 && !strcmp(s, "CHESSONL1"));
        CHECK(sfo_get_str(sfo, e.usize, "APP_VER", s, sizeof s) == 0 && !strcmp(s, "01.01"));
        CHECK(sfo_get_str(sfo, e.usize, "TITLE", s, sizeof s) == -1);
        CHECK(sfo_get_str(sfo, e.usize, "TITLE_ID", s, 4) == -1); /* too small for the string */
        CHECK(sfo_get_str("junk", 4, "TITLE_ID", s, sizeof s) == -1);
    }
    CHECK(unz_find(&z, "missing.bin", &e) == 0);
    CHECK(unz_open(&z, d, 100) == -1); /* cut short: no end record */
    free(d);
}

static void test_bad_zip(const char *path)
{
    size_t len = 0, pos;
    unsigned char *d = read_all(path, &len);
    unz_archive z;
    unz_entry e;
    int bad = 0;
    CHECK(d != NULL);
    if (!d) return;
    CHECK(unz_open(&z, d, len) == 0);
    pos = z.cd_off;
    while (unz_next(&z, &pos, &e) == 1) {
        unsigned char *out = (unsigned char *)malloc(e.usize + 1);
        if (!e.is_dir && unz_extract(&z, &e, out, e.usize) != 0) bad++;
        free(out);
    }
    CHECK(bad == 1); /* the damaged entry, and only it */
    free(d);
}

static void test_real_vpk(const char *path)
{
    size_t len = 0, pos;
    unsigned char *d = read_all(path, &len);
    unz_archive z;
    unz_entry e;
    int n = 0, ok = 1, safe = 1;
    char s[32];
    if (!d) {
        printf("(no %s: the real-VPK check is skipped)\n", path);
        return;
    }
    CHECK(unz_open(&z, d, len) == 0);
    pos = z.cd_off;
    while (unz_next(&z, &pos, &e) == 1) {
        unsigned char *out = (unsigned char *)malloc(e.usize + 1);
        n++;
        if (!upd_safe_path(e.name)) safe = 0;
        if (!e.is_dir && unz_extract(&z, &e, out, e.usize) != 0) ok = 0;
        if (!strcmp(e.name, "sce_sys/param.sfo"))
            CHECK(sfo_get_str(out, e.usize, "TITLE_ID", s, sizeof s) == 0 && !strcmp(s, "CHESSONL1"));
        free(out);
    }
    CHECK(n > 50 && ok && safe);
    free(d);
}

int main(int argc, char **argv)
{
    test_versions();
    test_release();
    test_paths();
    if (argc > 2) {
        test_good_zip(argv[1]);
        test_bad_zip(argv[2]);
    } else {
        printf("FAIL: no test zips given\n");
        failed++;
    }
    if (argc > 3) test_real_vpk(argv[3]);
    printf("update: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
