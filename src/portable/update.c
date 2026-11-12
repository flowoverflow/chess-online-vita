/* Chess Online: the update check's portable part (1.0.1). Our own code; see update.h. */
#include "update.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "json.h"

static const char *skip_v(const char *s)
{
    return (s[0] == 'v' || s[0] == 'V') && isdigit((unsigned char)s[1]) ? s + 1 : s;
}

int upd_version_cmp(const char *a, const char *b)
{
    int i;
    a = skip_v(a);
    b = skip_v(b);
    for (i = 0; i < 4; i++) {
        unsigned long x = 0, y = 0;
        while (isdigit((unsigned char)*a)) x = x * 10 + (unsigned long)(*a++ - '0');
        while (isdigit((unsigned char)*b)) y = y * 10 + (unsigned long)(*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!isdigit((unsigned char)*a) && !isdigit((unsigned char)*b)) break;
    }
    /* what is left is a suffix ("-rc1"): a version with one is older than the same version without */
    if (*a && !*b) return -1;
    if (!*a && *b) return 1;
    return strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0 ? 1 : 0;
}

static int ends_with(const char *s, const char *end)
{
    size_t n = strlen(s), m = strlen(end);
    return n >= m && !strcmp(s + n - m, end);
}

static int hex64(const char *s)
{
    int i;
    for (i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i])) return 0;
    return s[64] == 0;
}

int upd_parse_release(const char *json, size_t len, upd_release *r)
{
    jval *root = json_parse(json, len);
    const jval *assets, *a;
    const char *tag;
    int rc = -2;
    memset(r, 0, sizeof *r);
    if (!root) return -1;
    tag = json_str(root, "tag_name", "");
    if (!tag[0] || strlen(tag) >= sizeof r->tag || json_bool(root, "draft", 0) || json_bool(root, "prerelease", 0)) {
        json_free(root);
        return -1;
    }
    snprintf(r->tag, sizeof r->tag, "%s", tag);
    snprintf(r->version, sizeof r->version, "%s", skip_v(tag));
    assets = json_get(root, "assets");
    for (a = assets && assets->t == J_ARR ? assets->child : NULL; a; a = a->next) {
        const char *name = json_str(a, "name", ""), *url = json_str(a, "browser_download_url", "");
        const char *digest = json_str(a, "digest", "");
        double size = json_num(a, "size", 0);
        if (strncmp(name, UPD_ASSET_PREFIX, strlen(UPD_ASSET_PREFIX)) || !ends_with(name, ".vpk")) continue;
        rc = -3;
        if (strlen(name) >= sizeof r->asset || strlen(url) >= sizeof r->url) break;
        if (strncmp(url, "https://github.com/", 19) || size <= 0 || size > 512.0 * 1024 * 1024) break;
        if (strncmp(digest, "sha256:", 7) || !hex64(digest + 7)) break;
        snprintf(r->asset, sizeof r->asset, "%s", name);
        snprintf(r->url, sizeof r->url, "%s", url);
        r->size = (unsigned long long)size;
        {
            int i;
            for (i = 0; i < 64; i++) r->sha256[i] = (char)tolower((unsigned char)digest[7 + i]);
            r->sha256[64] = 0;
        }
        rc = 0;
        break;
    }
    json_free(root);
    return rc;
}

int upd_safe_path(const char *name)
{
    const char *p = name, *part = name;
    size_t n = strlen(name);
    if (!n || n > 200 || name[0] == '/') return 0;
    for (;; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == 0) {
            size_t len = (size_t)(p - part);
            /* an empty part is allowed only as a folder entry's trailing slash */
            if (len == 0 && !(c == 0 && p > name && p[-1] == '/')) return 0;
            if ((len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.')) return 0;
            if (c == 0) break;
            part = p + 1;
        } else if (c == '\\' || c == ':' || c < 0x20 || c == 0x7f) {
            return 0;
        }
    }
    return 1;
}
