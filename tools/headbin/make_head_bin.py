"""Chess Online: makes the package header (sce_sys/package/head.bin) for its helper app, so Chess Online can install
the helper with the Vita's own installer (scePromoterUtilityPromotePkgWithRif) and players install one VPK only.

This file is part of a separate build tool under the GNU General Public License, version 3 or (at your option) any
later version: see COPYING next to it. It is derived from VitaShell (https://github.com/TheOfficialFloW/VitaShell,
commit 81af70971ba18b8ce86215b04180f1e3d21cdfc9, package_installer.c, makeHeadBin and fpkg_hmac),
Copyright (C) 2015-2018 TheFloW, rewritten here in Python. head-template.bin is VitaShell's release/head.bin, unchanged.
Only this tool and its output (the helper's head.bin) are GPL-3.0; Chess Online's own code stays MIT, and the app only
copies the finished head.bin.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

Usage: python3 make_head_bin.py PARAM_SFO OUT_HEAD_BIN [TEMPLATE]
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def sfo_strings(data):
    """the UTF-8 string entries of a param.sfo"""
    if data[:4] != b"\0PSF":
        raise ValueError("not a param.sfo")
    key_tab, data_tab, count = struct.unpack_from("<III", data, 8)
    out = {}
    for i in range(count):
        key_off, fmt, length, _max, data_off = struct.unpack_from("<HHIII", data, 20 + 16 * i)
        key = data[key_tab + key_off:data.index(b"\0", key_tab + key_off)].decode()
        if fmt in (0x0204, 0x0004):
            out[key] = data[data_tab + data_off:data_tab + data_off + length].split(b"\0")[0].decode()
    return out


def digest(data):
    """the header's 16-byte check value over data (VitaShell's fpkg_hmac)"""
    h = hashlib.sha1(data).digest()
    buf = bytearray(64)
    buf[0:8] = h[4:12]
    buf[8:16] = h[4:12]
    buf[16:20] = h[12:16]
    buf[20:24] = bytes((h[16], h[1], h[2], h[3]))
    buf[24:32] = buf[16:24]
    return hashlib.sha1(bytes(buf)).digest()[:16]


def make(param_sfo, template):
    strings = sfo_strings(param_sfo)
    title = strings.get("TITLE_ID", "")
    if len(title) != 9 or title != title.upper():
        raise ValueError("TITLE_ID must be 9 upper-case characters, not %r" % title)
    content = strings.get("CONTENT_ID", "") or "EP9000-%s_00-0000000000000000" % title
    head = bytearray(template)
    field = content.encode()[:48]
    head[0x30:0x30 + 48] = field + b"\0" * (48 - len(field))  # as strncpy into 48 bytes
    be32 = lambda off: struct.unpack_from(">I", head, off)[0]
    # the header's own check, then the package information's, then one over everything
    n = be32(0xD0)
    head[n:n + 16] = digest(bytes(head[:n]))
    off, n, out = be32(0x08), be32(0x10), be32(0xD4)
    head[out:out + 16] = digest(bytes(head[off:off + n - 64]))
    n = be32(0xE8)
    head[n:n + 16] = digest(bytes(head[:n]))
    return bytes(head), title, content


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    template = open(sys.argv[3] if len(sys.argv) == 4 else os.path.join(HERE, "head-template.bin"), "rb").read()
    head, title, content = make(open(sys.argv[1], "rb").read(), template)
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
    open(sys.argv[2], "wb").write(head)
    print("head.bin for %s (%s): %d bytes, sha256 %s" % (title, content, len(head), hashlib.sha256(head).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
