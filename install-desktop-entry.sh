#!/bin/bash
# Make "Dolphin" everywhere on the desktop mean the Exxos build.
#
#   ./install-desktop-entry.sh          install
#   ./install-desktop-entry.sh --undo   remove
#
# WHY.  /usr/share/applications/org.kde.dolphin.desktop says `Exec=dolphin`,
# which is the packaged /usr/bin/dolphin. So the menu, the panel, the desktop
# icons and every "open containing folder" in every other application start the
# STOCK file manager -- the patched build only ever ran when it was typed into
# a terminal by hand. On a machine this theme is copied to, none of the work
# would be visible at all.
#
# A user-level copy of the same desktop id shadows the system one, so nothing
# root-owned is touched and apt cannot overwrite it. Removing the copy restores
# the packaged behaviour exactly.
#
# The launcher script itself falls back to /usr/bin/dolphin if the build is
# missing, so this cannot leave the desktop without a file manager.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SYS=/usr/share/applications/org.kde.dolphin.desktop
DEST="$HOME/.local/share/applications/org.kde.dolphin.desktop"

if [ "$1" = "--undo" ]; then
    rm -f "$DEST"
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
    kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
    echo "Removed. The packaged Dolphin is the file manager again."
    exit 0
fi

[ -f "$SYS" ] || { echo "No packaged Dolphin desktop entry at $SYS" >&2; exit 1; }
[ -x "$HERE/dolphin-exxos" ] || { echo "Launcher missing: $HERE/dolphin-exxos" >&2; exit 1; }

mkdir -p "$(dirname "$DEST")"
# Rewrite every Exec line -- the file has several, one per desktop action
# (New Window, New Tab), and leaving those pointing at the stock binary would
# start a second, different Dolphin from the taskbar menu.
sed -e "s|^Exec=dolphin\b|Exec=$HERE/dolphin-exxos|" \
    -e "s|^Exec=/usr/bin/dolphin\b|Exec=$HERE/dolphin-exxos|" \
    "$SYS" > "$DEST"

if grep -q "^Exec=dolphin\b\|^Exec=/usr/bin/dolphin\b" "$DEST"; then
    echo "WARNING: some Exec lines still point at the packaged binary:" >&2
    grep -n "^Exec=" "$DEST" >&2
fi

update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
kbuildsycoca5 --noincremental >/dev/null 2>&1 || true

echo "Installed: $DEST"
grep -n "^Exec=" "$DEST"
echo
echo "Undo with: $0 --undo"
