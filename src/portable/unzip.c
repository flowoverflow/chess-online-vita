/* Chess Online: a small in-memory zip reader (1.0.1). Our own code; see unzip.h. */
#include "unzip.h"

#include <string.h>
#include <zlib.h>

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

#define SIG_EOCD 0x06054b50UL
#define SIG_CDIR 0x02014b50UL
#define SIG_LOCAL 0x04034b50UL

int unz_open(unz_archive *z, const void *data, size_t len)
{
    const unsigned char *d = (const unsigned char *)data;
    size_t i, lo;
    memset(z, 0, sizeof *z);
    if (len < 22) return -1;
    /* the end record is the last 22 bytes plus a comment of at most 65535 */
    lo = len > 22 + 65535 ? len - 22 - 65535 : 0;
    for (i = len - 22 + 1; i-- > lo;) {
        unsigned long cd_size, cd_off;
        if (rd32(d + i) != SIG_EOCD) continue;
        if (i + 22 + rd16(d + i + 20) != len) continue; /* the comment must end the file */
        if (rd16(d + i + 4) || rd16(d + i + 6) || rd16(d + i + 8) != rd16(d + i + 10)) return -1; /* multi-disk */
        cd_size = rd32(d + i + 12);
        cd_off = rd32(d + i + 16);
        if (cd_off == 0xFFFFFFFFUL || cd_size == 0xFFFFFFFFUL || rd16(d + i + 10) == 0xFFFF) return -1; /* ZIP64 */
        if (cd_off > i || cd_size > i - cd_off) return -1;
        z->data = d;
        z->len = len;
        z->cd_off = cd_off;
        z->cd_end = cd_off + cd_size;
        z->count = rd16(d + i + 10);
        return 0;
    }
    return -1;
}

int unz_next(const unz_archive *z, size_t *pos, unz_entry *e)
{
    const unsigned char *p;
    unsigned name_len, extra_len, comment_len, flags;
    if (*pos >= z->cd_end) return 0;
    if (z->cd_end - *pos < 46) return -1;
    p = z->data + *pos;
    if (rd32(p) != SIG_CDIR) return -1;
    flags = rd16(p + 8);
    name_len = rd16(p + 28);
    extra_len = rd16(p + 30);
    comment_len = rd16(p + 32);
    if (46 + (size_t)name_len + extra_len + comment_len > z->cd_end - *pos) return -1;
    if (name_len == 0 || name_len >= sizeof e->name) return -1;
    memset(e, 0, sizeof *e);
    memcpy(e->name, p + 46, name_len);
    if (memchr(e->name, 0, name_len)) return -1;
    e->method = rd16(p + 10);
    e->crc = rd32(p + 16);
    e->csize = rd32(p + 20);
    e->usize = rd32(p + 24);
    e->local_off = rd32(p + 42);
    e->is_dir = e->name[name_len - 1] == '/';
    if (flags & 1) return -1; /* encrypted */
    if (e->csize == 0xFFFFFFFFUL || e->usize == 0xFFFFFFFFUL || e->local_off == 0xFFFFFFFFUL) return -1;
    *pos += 46 + (size_t)name_len + extra_len + comment_len;
    return 1;
}

int unz_find(const unz_archive *z, const char *name, unz_entry *e)
{
    size_t pos = z->cd_off;
    int r;
    while ((r = unz_next(z, &pos, e)) == 1)
        if (!strcmp(e->name, name)) return 1;
    return r;
}

int unz_extract(const unz_archive *z, const unz_entry *e, void *out, size_t out_len)
{
    const unsigned char *p, *src;
    size_t data_off;
    if (out_len < e->usize || e->local_off > z->len || z->len - e->local_off < 30) return -1;
    p = z->data + e->local_off;
    if (rd32(p) != SIG_LOCAL) return -1;
    data_off = e->local_off + 30 + rd16(p + 26) + rd16(p + 28);
    if (data_off > z->len || z->len - data_off < e->csize) return -1;
    src = z->data + data_off;
    if (e->method == 0) {
        if (e->csize != e->usize) return -1;
        if (e->usize) memcpy(out, src, e->usize);
    } else if (e->method == 8) {
        z_stream s;
        int r;
        memset(&s, 0, sizeof s);
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return -1;
        s.next_in = (Bytef *)src;
        s.avail_in = (uInt)e->csize;
        s.next_out = (Bytef *)out;
        s.avail_out = (uInt)e->usize;
        r = inflate(&s, Z_FINISH);
        inflateEnd(&s);
        if (r != Z_STREAM_END || s.total_out != e->usize) return -1;
    } else {
        return -1;
    }
    if (crc32(crc32(0L, Z_NULL, 0), (const Bytef *)out, (uInt)e->usize) != e->crc) return -2;
    return 0;
}
