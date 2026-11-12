#!/bin/bash
# Chess Online Updater (CHESSUPD1): build its VPK with the regular VitaSDK 2026.08 (1.0.1).
# Usage: bash scripts/build-updater.sh
set -euo pipefail
export VITASDK="${VITASDK:-$HOME/vitasdk-2026.08}"
export PATH="$VITASDK/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/updater"
OUT="$ROOT/build/updater"
mkdir -p "$OUT"
rm -f "$OUT/ChessOnlineUpdater.vpk" "$OUT/eboot.bin"
cmake -S "$SRC" -B "$OUT" -DCMAKE_BUILD_TYPE=Release > "$OUT.configure.log" 2>&1 || { cat "$OUT.configure.log"; exit 1; }
cmake --build "$OUT" -j"$(nproc)" > "$OUT.build.log" 2>&1 || { tail -30 "$OUT.build.log"; exit 1; }
grep -E " error|error:|warning: " "$OUT.build.log" | grep -v "GNU-stack" || true
test -f "$OUT/ChessOnlineUpdater.vpk"
sha256sum "$OUT/eboot.bin" "$OUT/ChessOnlineUpdater.vpk"
