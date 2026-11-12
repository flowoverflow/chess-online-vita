/* Chess Online: a small reader for zip files held in memory (a VPK is a zip), for the updater (1.0.1). Our own code
 * after PKWARE's public APPNOTE: the end record, the central directory, local headers, stored and deflated entries
 * (zlib's raw inflate), CRC-32 checked. No ZIP64, no encryption, no multi-disk archives: those are refused. */
#ifndef CO_UNZIP_H
#define CO_UNZIP_H

#include <stddef.h>

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t cd_off, cd_end; /* the central directory */
    unsigned count;        /* entries */
} unz_archive;

typedef struct {
    char name[256];
    unsigned method;       /* 0 stored, 8 deflated */
    unsigned long crc;
    size_t csize, usize;
    size_t local_off;      /* the entry's local header */
    int is_dir;            /* the name ends with '/' */
} unz_entry;

/* 0 ok, -1 not a zip this reader takes */
int unz_open(unz_archive *z, const void *data, size_t len);
/* Walks the central directory: set *pos = z->cd_off first. 1 an entry was read, 0 the end, -1 a damaged entry. */
int unz_next(const unz_archive *z, size_t *pos, unz_entry *e);
/* Finds an entry by its exact name. 1 found, 0 not, -1 damaged. */
int unz_find(const unz_archive *z, const char *name, unz_entry *e);
/* Writes the entry's e->usize bytes to out. 0 ok, -1 damaged or unsupported, -2 CRC mismatch. */
int unz_extract(const unz_archive *z, const unz_entry *e, void *out, size_t out_len);

#endif
