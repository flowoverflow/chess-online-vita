/* Chess Online: the update check's portable part (1.0.1). Our own code.
 * Reads GitHub's "latest release" answer for the app's public repo, compares versions, and checks the names in a
 * downloaded package before anything is written. The Vita side is src/vita/updateui.c. */
#ifndef CO_UPDATE_H
#define CO_UPDATE_H

#include <stddef.h>

#define UPD_REPO "flowoverflow/chess-online-vita"
#define UPD_API_URL "https://api.github.com/repos/" UPD_REPO "/releases/latest"
#define UPD_ASSET_PREFIX "ChessOnline-" /* the app's VPK; "ChessOnlineUpdater-..." is the helper app's */
#define UPD_TITLE_ID "CHESSONL1"
#define UPD_HELPER_TITLE_ID "CHESSUPD1"

typedef struct {
    char tag[32];             /* as the release names it: "v1.0.1" */
    char version[32];         /* the tag without its leading v: "1.0.1" */
    char asset[128];          /* "ChessOnline-1.0.1.vpk" */
    char url[512];            /* its browser_download_url (https://github.com/...) */
    unsigned long long size;  /* bytes */
    char sha256[65];          /* lowercase hex from the asset's "sha256:..." digest */
} upd_release;

/* Compares dotted versions ("1.0.10" > "1.0.9"); a leading v is ignored, missing parts count as 0, and a suffix
 * such as "-rc1" makes a version older than the same one without it. <0, 0 or >0. */
int upd_version_cmp(const char *a, const char *b);

/* Parses releases/latest. 0 ok; -1 not JSON or no tag; -2 no ChessOnline-*.vpk asset; -3 the asset has no sha256
 * digest, a size or an https URL on github.com. A draft or prerelease is refused as -1. */
int upd_parse_release(const char *json, size_t len, upd_release *r);

/* 1 when a package entry's name is safe to write under a folder: relative, no "..", "." or empty parts, no
 * backslash, colon or control character, at most 200 bytes. */
int upd_safe_path(const char *name);

#endif
