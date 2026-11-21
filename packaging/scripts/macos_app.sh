#!/usr/bin/env bash
set -euo pipefail

BIN=${1:?usage: macos_app.sh PEGGLEPORT_BINARY [OUT_DIR] [VERSION] [MODHOST_BINARY]}
OUT=${2:-.}
VERSION=${3:-0.0.0}
MODHOST=${4:-$(dirname "$BIN")/pp_modhost}
[ -f "$BIN" ] || { echo "macos_app.sh: $BIN not found" >&2; exit 1; }
[ -f "$MODHOST" ] || { echo "macos_app.sh: $MODHOST not found" >&2; exit 1; }

APP="$OUT/PegglePort.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/peggleport"
cp "$MODHOST" "$APP/Contents/MacOS/pp_modhost"
chmod +x "$APP/Contents/MacOS/peggleport" "$APP/Contents/MacOS/pp_modhost"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$APP/Contents/Resources/tools"
cp "$ROOT"/tools/import_ps3_*.py "$ROOT/tools/requirements-ps3-art.txt" "$APP/Contents/Resources/tools/"

SHORT=$(printf '%s' "$VERSION" | sed -E 's/^v//' | grep -E '^[0-9]+(\.[0-9]+){0,2}$' || echo 0.0.0)

MINOS=11.0
if command -v otool >/dev/null 2>&1; then
  m=$(otool -l "$BIN" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
  [ -n "${m:-}" ] && MINOS=$m
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>peggleport</string>
  <key>CFBundleIdentifier</key><string>io.github.mattsumi.peggleport</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>PegglePort</string>
  <key>CFBundleDisplayName</key><string>PegglePort</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleSignature</key><string>????</string>
  <key>CFBundleShortVersionString</key><string>${SHORT}</string>
  <key>CFBundleVersion</key><string>${SHORT}</string>
  <key>LSMinimumSystemVersion</key><string>${MINOS}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.arcade-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
  <key>CFBundleURLTypes</key>
  <array>
    <dict>
      <key>CFBundleURLName</key><string>PegglePort invite</string>
      <key>CFBundleTypeRole</key><string>Viewer</string>
      <key>CFBundleURLSchemes</key>
      <array><string>peggleport</string><string>discord-1571022918392225882</string></array>
    </dict>
  </array>
</dict>
</plist>
PLIST
printf 'APPL????' > "$APP/Contents/PkgInfo"

if command -v plutil >/dev/null 2>&1; then plutil -lint "$APP/Contents/Info.plist" >/dev/null; fi
if command -v codesign >/dev/null 2>&1; then codesign --force --deep -s - "$APP" >/dev/null 2>&1 || true; fi
if command -v otool >/dev/null 2>&1; then
  ext=$( (otool -L "$BIN"; otool -L "$MODHOST") | grep -v ':$' | awk '{print $1}' | grep -v -E '^(/usr/lib/|/System/)' || true)
  [ -n "$ext" ] && { echo "macos_app.sh: warning: the binary links non-system libraries (the app will not run on other Macs):" >&2; echo "$ext" >&2; }
fi
echo "$APP"
