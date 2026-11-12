/* Chess Online: param.sfo string lookup (1.0.1). Our own code; see sfo.h. */
#include "sfo.h"

#include <string.h>

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

int sfo_get_str(const void *data, size_t len, const char *key, char *out, size_t n)
{
    const unsigned char *d = (const unsigned char *)data;
    unsigned long key_tab, data_tab, count, i;
    if (n) out[0] = 0;
    if (len < 20 || memcmp(d, "\0PSF", 4)) return -1;
    key_tab = rd32(d + 8);
    data_tab = rd32(d + 12);
    count = rd32(d + 16);
    if (key_tab > len || data_tab > len || count > (len - 20) / 16) return -1;
    for (i = 0; i < count; i++) {
        const unsigned char *e = d + 20 + i * 16;
        unsigned long koff = key_tab + rd16(e), fmt = rd16(e + 2), plen = rd32(e + 4), doff = data_tab + rd32(e + 12);
        size_t k;
        if (koff >= len) return -1;
        for (k = koff; k < len && d[k]; k++) {}
        if (k >= len) return -1;
        if (strcmp((const char *)d + koff, key)) continue;
        if (fmt != 0x0204 && fmt != 0x0004) return -1; /* not a UTF-8 string */
        if (doff > len || plen > len - doff || plen == 0 || plen >= n) return -1;
        memcpy(out, d + doff, plen);
        out[plen] = 0; /* plen counts the string's own NUL for 0x0204 */
        return 0;
    }
    return -1;
}
