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

# Stamp the build id into the pages (asset ?v= queries + window.LZ_BUILD) and publish it as
# version.json, so browsers bust their cached JS/CSS/wasm on every deploy and the running page
# can detect that a newer deploy exists (the "site updated - refresh" bar in site.js).
BUILD="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || date +%s)"
sed -i "s/__LZ_BUILD__/${BUILD}/g" "$OUT"/index.html "$OUT"/carts/index.html
printf '{"build":"%s"}\n' "$BUILD" > "$OUT"/version.json
echo "==> build id: $BUILD"

echo "==> done. site at: $OUT"
echo "    serve with:  python -m http.server -d \"$OUT\" 8000   ->  http://localhost:8000"
