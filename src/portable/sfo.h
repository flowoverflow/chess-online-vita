/* Chess Online: reads a string from a param.sfo (the system's app parameter file) held in memory (1.0.1), to check
 * a package's title ID before installing it. Our own code after the format's public description: a 20-byte header
 * ("\0PSF", version, key-table offset, data-table offset, entry count) and 16-byte entries (key offset, format,
 * length, maximum length, data offset). */
#ifndef CO_SFO_H
#define CO_SFO_H

#include <stddef.h>

/* 0 ok (out gets the UTF-8 string), -1 not an SFO or no such string key */
int sfo_get_str(const void *data, size_t len, const char *key, char *out, size_t n);

#endif
