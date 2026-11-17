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

mkdir -p "$STAGE/licenses"
for d in "$BUILD"/_deps/*-src; do
  [ -d "$d" ] || continue
  n=$(basename "$d" -src)
  for f in "$d"/LICENSE "$d"/LICENSE.txt "$d"/COPYING; do
    if [ -f "$f" ]; then cp "$f" "$STAGE/licenses/$n-$(basename "$f")"; fi
  done
done
for f in "$BUILD"/_deps/openmpt-src/include/*/LICENSE; do
  if [ -f "$f" ]; then cp "$f" "$STAGE/licenses/openmpt-$(basename "$(dirname "$f")")-LICENSE"; fi
done

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
    if [ ! -f "$BUILD/CMakeCache.txt" ] || [ ! -r "$BUILD/CMakeCache.txt" ]; then
      echo "package.sh: cannot read $BUILD/CMakeCache.txt" >&2
      exit 1
    fi
    if ! STEAM_ENABLED=$(awk '
      /^PP_STEAM:/ {
        count++
        if (!match($0, /^PP_STEAM:[^=]+=/)) exit 1
        value = substr($0, RLENGTH + 1)
        sub(/\r$/, "", value)
        upper = toupper(value)
        enabled = !(value == "" || upper ~ /^(0|OFF|NO|FALSE|N|IGNORE)$/ || value == "NOTFOUND" || value ~ /-NOTFOUND$/)
      }
      END {
        if (count != 1) exit 1
        print enabled
      }
    ' "$BUILD/CMakeCache.txt"); then
      echo "package.sh: expected exactly one PP_STEAM cache entry in $BUILD/CMakeCache.txt" >&2
      exit 1
    fi
    if [ "$STEAM_ENABLED" = 1 ]; then
      for f in steam_api64.dll steam_appid.txt; do
        if [ ! -f "$(dirname "$PP")/$f" ]; then
          echo "package.sh: Steam build is missing $f beside $PP" >&2
          exit 1
        fi
        cp "$(dirname "$PP")/$f" "$STAGE/"
      done
    fi
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
    rm -f "$OUT/$NAME.zip" "$OUT/$NAME.dmg"
    zip -qry "$OUT/$NAME.zip" "$NAME"
    echo "$OUT/$NAME.zip"
    DISK=$(mktemp -d)/PegglePort
    mkdir -p "$DISK"
    cp -R "$STAGE/PegglePort.app" "$DISK/"
    ln -s /Applications "$DISK/Applications"
    cp "$STAGE/README.md" "$STAGE/FAQ.md" "$DISK/"
    [ -f "$STAGE/LICENSE" ] && cp "$STAGE/LICENSE" "$DISK/"
    hdiutil create -quiet -volname PegglePort -srcfolder "$DISK" -ov -format UDZO "$OUT/$NAME.dmg"
    echo "$OUT/$NAME.dmg"
    ;;
esac
