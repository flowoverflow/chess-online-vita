#!/bin/bash
# Chess Online: build the VPK with the regular VitaSDK 2026.08.
# Usage: bash scripts/build.sh
set -euo pipefail
export VITASDK="${VITASDK:-$HOME/vitasdk-2026.08}"
export PATH="$VITASDK/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT"
OUT="$ROOT/build/vita"

# CA bundle: public root certificates from Ubuntu's ca-certificates (Mozilla's list).
# Lichess and chess1.coolmathgames.com are served by Google Trust Services today; Let's Encrypt's roots are a fallback.
# 1.0.1: api.github.com and github.com chain to USERTrust ECC (Sectigo), the release downloads to ISRG Root X1.
CA="$SRC/res/cacert.pem"
: > "$CA"
for c in GTS_Root_R1 GTS_Root_R2 GTS_Root_R3 GTS_Root_R4 ISRG_Root_X1 ISRG_Root_X2 USERTrust_ECC_Certification_Authority USERTrust_RSA_Certification_Authority; do
  f="/etc/ssl/certs/$c.pem"
  if [ -f "$f" ]; then
    printf '# %s\n' "$c" >> "$CA"
    cat "$f" >> "$CA"
  fi
done
grep -c "BEGIN CERTIFICATE" "$CA" | sed 's/^/CA roots: /'

# 1.0.1: the updater's helper app rides inside this VPK (res/updater/, installed by the app itself when an update
# runs), so the player installs one VPK only. Built first; its files are copied in as the VPK carries them.
mkdir -p "$OUT"
bash "$ROOT/scripts/build-updater.sh" > "$OUT.updater.log" 2>&1 || { tail -20 "$OUT.updater.log"; exit 1; }
U="$ROOT/build/updater"
H="$SRC/res/updater"
mkdir -p "$H/sce_sys/livearea/contents" "$H/res"
cp "$U/eboot.bin" "$H/eboot.bin"
cp "$U/ChessOnlineUpdater.vpk_param.sfo" "$H/sce_sys/param.sfo"
cp "$SRC/updater/res/sce_sys/icon0.png" "$H/sce_sys/icon0.png"
for f in bg.png startup.png template.xml; do cp "$SRC/updater/res/sce_sys/livearea/contents/$f" "$H/sce_sys/livearea/contents/$f"; done
cp "$SRC/res/logo140.png" "$H/res/logo140.png"
# its package header, so the app can install it with the system installer (a separate GPL-3.0 tool, after VitaShell)
python3 "$ROOT/tools/headbin/make_head_bin.py" "$H/sce_sys/param.sfo" "$H/sce_sys/package/head.bin"
sha256sum "$H/eboot.bin" | sed 's/^/helper app: /'

build() {
  rm -f "$OUT/chessonline.vpk" "$OUT/eboot.bin"
  cmake -S "$SRC" -B "$OUT" -DCMAKE_BUILD_TYPE=Release -DCO_TEXT_PAD="$1" -DCO_VERSION="${CO_VERSION:-}" > "$OUT.configure.log" 2>&1 || { cat "$OUT.configure.log"; exit 1; }
  set +e
  cmake --build "$OUT" -j"$(nproc)" > "$OUT.build.log" 2>&1
  local rc=$?
  set -e
  grep -E " error|error:|warning: |overlaps" "$OUT.build.log" | grep -v "format-truncation\|GNU-stack" || true
  return $rc
}

mkdir -p "$OUT"
PAD=1
if ! build "$PAD"; then
  if grep -q "segment 1 overlaps" "$OUT.build.log"; then
    # text segment end = VirtAddr + MemSiz of the R E LOAD header; pad it past the next 64 KB boundary
    read -r VADDR MEMSZ < <(arm-vita-eabi-readelf -lW "$OUT/chessonline" | awk '$1=="LOAD" && / R E / {print $3, $6; exit}')
    END=$(( VADDR + MEMSZ ))
    GAP=$(( (65536 - END % 65536) % 65536 ))
    PAD=$(( GAP + 1024 ))
    echo "text segment ends $GAP bytes below a 64 KB boundary: rebuilding with a $PAD-byte pad"
    build "$PAD" || { tail -20 "$OUT.build.log"; exit 1; }
  else
    tail -30 "$OUT.build.log"
    exit 1
  fi
fi
test -f "$OUT/chessonline.vpk"
echo "text pad: $PAD"
sha256sum "$OUT/eboot.bin" "$OUT/chessonline.vpk"
