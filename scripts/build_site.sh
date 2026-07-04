#!/usr/bin/env bash
# Build the Lazy-100 wasm console and assemble the static site into an output dir.
# Used both locally (default out: build/site) and by CI (out: site — see deploy_pages.yaml).
#
#   scripts/build_site.sh [out_dir]
#
# Then serve it with any static server, e.g.:
#   python -m http.server -d build/site 8000   ->  http://localhost:8000
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/build/site}"
WASM_DIR="$ROOT/build/wasm/wasm32/release"
export XMAKE_ROOT="${XMAKE_ROOT:-y}"

echo "==> building wasm console"
( cd "$ROOT" && xmake f -p wasm -y && xmake build -y lazy100 )

echo "==> assembling site -> $OUT"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$ROOT"/web/site/* "$OUT"/   # home (index) + the carts/ subpage
cp "$WASM_DIR"/lazy100.js "$WASM_DIR"/lazy100.wasm "$OUT"/

echo "==> done. site at: $OUT"
echo "    serve with:  python -m http.server -d \"$OUT\" 8000   ->  http://localhost:8000"
