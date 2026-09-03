#!/bin/bash
# Exxos Theme — RESTORE
#
# Puts back everything captured by exxos-theme-save.sh.
# Usage:  ./exxos-theme-restore.sh [snapshot-folder-name]
#         (with no argument, uses the newest snapshot)
#
# Stops plasmashell first: it rewrites appletsrc on exit, so restoring
# underneath a running plasmashell would be silently undone.

set -u
BASE="$(cd "$(dirname "$0")" && pwd)"
SNAP="${1:-$(ls -dt "$BASE"/snapshot-* 2>/dev/null | head -1)}"
[ -z "${SNAP:-}" ] && { echo "No snapshot found in $BASE" >&2; exit 1; }
[ -d "$SNAP" ] || SNAP="$BASE/$SNAP"
[ -d "$SNAP" ] || { echo "Not a snapshot: $SNAP" >&2; exit 1; }

echo "Restoring from: $SNAP"
[ -f "$SNAP/MANIFEST.txt" ] && sed -n '1,12p' "$SNAP/MANIFEST.txt"
printf "\nThis overwrites your current theme configuration. Continue? [y/N] "
read -r a; case "$a" in y|Y) ;; *) echo "Aborted."; exit 0;; esac

echo "Stopping plasmashell..."
qdbus org.kde.plasmashell /MainApplication quit 2>/dev/null || true
sleep 3

put() { # put() <snapshot-subdir>
    [ -d "$SNAP/$1" ] || return 0
    (cd "$SNAP/$1" && find . -mindepth 1 -maxdepth 1 -print0) | while IFS= read -r -d '' e; do
        cp -r "$SNAP/$1/${e#./}" "$HOME/" && echo "  restored: ${e#./}"
    done
}
put config
put local
mkdir -p "$HOME/theme-work/bin"
[ -d "$SNAP/scripts" ] && { cp "$SNAP"/scripts/*.sh "$HOME/theme-work/bin/" 2>/dev/null; chmod +x "$HOME"/theme-work/bin/*.sh 2>/dev/null; echo "  restored: theme-work/bin scripts"; }

FFP=$(ls -d "$HOME"/.mozilla/firefox/*.default-release 2>/dev/null | head -1)
[ -n "${FFP:-}" ] && [ -f "$SNAP/firefox/user.js" ] && { cp "$SNAP/firefox/user.js" "$FFP/"; echo "  restored: firefox user.js"; }

echo "Clearing Plasma caches (stale caches keep old icon tints)..."
rm -f "$HOME"/.cache/plasma-svgelements* "$HOME"/.cache/plasma_theme_*.kcache "$HOME"/.cache/icon-cache.kcache 2>/dev/null
rm -rf "$HOME"/.cache/plasmashell 2>/dev/null
find "$HOME/.cache" -name '*.qmlc' -delete 2>/dev/null
kbuildsycoca5 --noincremental >/dev/null 2>&1

echo "Restarting plasmashell and reconfiguring KWin..."
setsid plasmashell >/dev/null 2>&1 < /dev/null &
qdbus org.kde.KWin /KWin reconfigure 2>/dev/null || true
dbus-send --session --type=signal /KGlobalSettings org.kde.KGlobalSettings.notifyChange int32:0 int32:0 2>/dev/null || true

echo
echo "Done. Browser settings are NOT restored automatically (they hold personal data)."
echo "With Chrome closed, run:  $SNAP/scripts/reapply-browser-settings.sh"
echo "Log out and back in for the widget style, numlock and brightness autostart."
