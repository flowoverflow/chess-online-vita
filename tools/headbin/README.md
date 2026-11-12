# headbin: the helper app's package header (GPL-3.0)

The Vita's own installer (the promoter) installs a package only when the package has a header,
`sce_sys/package/head.bin`. Chess Online carries its helper app, Chess Online Updater, inside its own VPK and
installs it with that installer just before an update, so players install one VPK only. This tool makes the helper's
header at build time (`scripts/build.sh` runs it). The app copies the finished file and never runs this code.

- `make_head_bin.py` is derived from VitaShell, rewritten in Python: https://github.com/TheOfficialFloW/VitaShell,
  commit `81af70971ba18b8ce86215b04180f1e3d21cdfc9`, `package_installer.c` (`makeHeadBin`, `fpkg_hmac`).
  Copyright (C) 2015-2018 TheFloW.
- `head-template.bin` is VitaShell's `release/head.bin` at that commit, unchanged (1072 bytes, sha256
  `18f0ee25854fea3b41510b4f91547c3a150b4831dc86a6af4c6b8450ac92967d`).
- `COPYING` is the GNU General Public License, version 3, as VitaShell ships it.

Licence: this folder, and the `head.bin` it makes (packed into the VPK as `res/updater/sce_sys/package/head.bin`), are
under the GNU General Public License, version 3 or later. The rest of Chess Online stays under the MIT licence
(`LICENSE`). The header is a separate data file that the app copies; no GPL code is compiled into the app.

```bash
python3 tools/headbin/make_head_bin.py res/updater/sce_sys/param.sfo res/updater/sce_sys/package/head.bin
```
