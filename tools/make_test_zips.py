"""Chess Online: test zips for tests/test_update.c (1.0.1). Our own code, Python standard library only.

Usage: python3 make_test_zips.py OUTDIR
Writes OUTDIR/good.zip (a folder entry, stored and deflated files, an empty file and a small param.sfo with
TITLE_ID CHESSONL1 and APP_VER 01.01) and OUTDIR/bad.zip (the same with one byte of the deflated eboot.bin changed).
The random bytes come from a fixed seed, so the files are the same every run."""
import os
import random
import struct
import sys
import zipfile


def sfo(entries):
    """a param.sfo with UTF-8 string entries, in the format's public layout"""
    keys = b""
    data = b""
    index = b""
    for key, value in entries:
        raw = value.encode() + b"\0"
        size = (len(raw) + 3) // 4 * 4
        index += struct.pack("<HHIII", len(keys), 0x0204, len(raw), size, len(data))
        keys += key.encode() + b"\0"
        data += raw + b"\0" * (size - len(raw))
    keys += b"\0" * ((-len(keys)) % 4)
    key_tab = 20 + len(index)
    data_tab = key_tab + len(keys)
    return struct.pack("<4sIIII", b"\0PSF", 0x0101, key_tab, data_tab, len(entries)) + index + keys + data


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(20261112)
    eboot = bytes(rnd.randrange(256) for _ in range(20000)) + b"chess" * 6000
    good = os.path.join(out, "good.zip")
    with zipfile.ZipFile(good, "w") as z:
        z.writestr(zipfile.ZipInfo("res/"), b"")
        z.writestr("eboot.bin", eboot, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("sce_sys/param.sfo", sfo([("APP_VER", "01.01"), ("TITLE_ID", "CHESSONL1")]),
                   compress_type=zipfile.ZIP_STORED)
        z.writestr("res/a.txt", b"a" * 4000, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("res/empty.txt", b"", compress_type=zipfile.ZIP_STORED)
    data = bytearray(open(good, "rb").read())
    with zipfile.ZipFile(good) as z:
        info = z.getinfo("eboot.bin")
    # the deflated data starts after the 30-byte local header, its name and its extra field
    name_len, extra_len = struct.unpack_from("<HH", data, info.header_offset + 26)
    start = info.header_offset + 30 + name_len + extra_len
    data[start + info.compress_size // 2] ^= 0x5A
    open(os.path.join(out, "bad.zip"), "wb").write(bytes(data))


if __name__ == "__main__":
    main()
