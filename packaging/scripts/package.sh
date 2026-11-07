#!/usr/bin/env bash
set -euo pipefail

BUILD=${1:?usage: package.sh BUILD_DIR LABEL VERSION [OUT_DIR]}
LABEL=${2:?label}
VERSION=${3:?version}
OUT=${4:-dist}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
NAME="peggleport-${VERSION}-${LABEL}"
STAGE=$(mktemp -d)/"$NAME"
mkdir -p "$STAGE"

find_bin() {
  for c in "$BUILD/$1" "$BUILD/$1.exe" "$BUILD/Release/$1.exe" "$BUILD/Release/$1"; do
    if [ -f "$c" ]; then echo "$c"; return 0; fi
  done
  echo "package.sh: $1 not found under $BUILD" >&2
  return 1
}

PP=$(find_bin peggleport)
LOBBY=$(find_bin pp_lobbyd)
MODHOST=$(find_bin pp_modhost)
cp "$LOBBY" "$STAGE/"
[ "$LABEL" = "macos" ] || cp "$PP" "$MODHOST" "$STAGE/"

cp "$ROOT/README.md" "$ROOT/FAQ.md" "$ROOT/THIRD_PARTY.md" "$STAGE/"
[ -f "$ROOT/LICENSE" ] && cp "$ROOT/LICENSE" "$STAGE/"
cp -R "$ROOT/faq" "$STAGE/faq"

mkdir -p "$STAGE/examples/mods"
cp -R "$ROOT"/examples/mods/* "$STAGE/examples/mods/"
cp -R "$ROOT/examples/plugin" "$STAGE/examples/plugin"
cp "$ROOT/include/peggleport_mod.h" "$STAGE/examples/plugin/"
if [ -d "$BUILD/examples/mods/hello_native" ]; then
  cp -R "$BUILD/examples/mods/hello_native" "$STAGE/examples/mods/hello_native"
fi

case "$LABEL" in
  linux)
    mkdir -p "$STAGE/packaging" "$STAGE/deploy"
    cp -R "$ROOT/packaging/linux" "$STAGE/packaging/"
    cp "$ROOT"/deploy/* "$STAGE/deploy/"
    ;;
  windows)
    cp "$ROOT/packaging/windows/README-WINDOWS.txt" "$STAGE/"
    ;;
  macos)
    bash "$ROOT/packaging/scripts/macos_app.sh" "$PP" "$STAGE" "$VERSION" "$MODHOST"
    ;;
esac

BAD=$(find "$STAGE" \( -iname '*.pak' -o -iname 'game.exe' -o -iname 'popcap*.dll' \) -print || true)
if [ -n "$BAD" ]; then
  echo "package.sh: game files in the staging folder, refusing to archive:" >&2
  echo "$BAD" >&2
  exit 1
fi

cd "$(dirname "$STAGE")"
case "$LABEL" in
  linux)
    tar czf "$OUT/$NAME.tar.gz" "$NAME"
    echo "$OUT/$NAME.tar.gz"
    ;;
  windows)
    rm -f "$OUT/$NAME.zip"
    cmake -E tar cf "$OUT/$NAME.zip" --format=zip -- "$NAME"
    echo "$OUT/$NAME.zip"
    ;;
  macos)
    rm -f "$OUT/$NAME.zip"
    zip -qry "$OUT/$NAME.zip" "$NAME"
    echo "$OUT/$NAME.zip"
    ;;
esac
