#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")/../.." && pwd)
PREFIX=${PREFIX:-$HOME/.local}
DEST=$PREFIX/opt/peggleport
APPS=${XDG_DATA_HOME:-$HOME/.local/share}/applications

if [ "${1:-}" = "--uninstall" ]; then
  rm -rf "$DEST" "$PREFIX/bin/peggleport" "$PREFIX/bin/pp_lobbyd" "$APPS/peggleport.desktop"
  command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
  echo "removed"
  exit 0
fi

mkdir -p "$DEST" "$PREFIX/bin" "$APPS"
cp "$HERE/peggleport" "$HERE/pp_lobbyd" "$HERE/pp_modhost" "$DEST/"
mkdir -p "$DEST/tools"
cp "$HERE"/tools/import_ps3_*.py "$HERE/tools/requirements-ps3-art.txt" "$DEST/tools/"
ln -sf "$DEST/peggleport" "$PREFIX/bin/peggleport"
ln -sf "$DEST/pp_lobbyd" "$PREFIX/bin/pp_lobbyd"
sed "s|^Exec=peggleport|Exec=$DEST/peggleport|" "$HERE/packaging/linux/peggleport.desktop" > "$APPS/peggleport.desktop"
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
if command -v xdg-mime >/dev/null; then xdg-mime default peggleport.desktop x-scheme-handler/peggleport || true; fi
echo "installed to $DEST, you will find PegglePort in your application menu"
