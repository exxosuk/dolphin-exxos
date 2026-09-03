#!/bin/bash
# Rebuild the local plasmoid overrides from the CURRENTLY INSTALLED Plasma,
# plus the small set of Exxos changes kept as patches.
#
# WHY THIS EXISTS
# A plasmoid in ~/.local/share/plasma/plasmoids/ shadows the system one
# ENTIRELY -- Plasma resolves the whole package from one location, so you
# cannot override a single file. The previous overrides were full copies of
# Plasma 5.20's QML: 4738 lines shadowed to change 80. After a Plasma upgrade
# those stale copies still win, and the failure is SILENT -- no error, just a
# start menu or panel widget that misbehaves against the new Plasma.
#
# This script instead copies the system plasmoid as it is TODAY and reapplies
# only the 80 lines. Run it after every Plasma upgrade.
#
#   ./rebuild-overrides.sh            # rebuild
#   ./rebuild-overrides.sh --check    # report drift, change nothing
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SYS=/usr/share/plasma/plasmoids
LOC="$HOME/.local/share/plasma/plasmoids"
CHECK=0; [ "${1:-}" = "--check" ] && CHECK=1

declare -A FILES=(
  [org.kde.plasma.icon]="contents/ui/main.qml"
  [org.kde.plasma.kicker]="contents/ui/MenuRepresentation.qml contents/ui/ItemListView.qml contents/ui/ItemListDelegate.qml"
  [org.kde.plasma.taskmanager]="contents/ui/code/layout.js"
)

fail=0
for p in "${!FILES[@]}"; do
    if [ ! -d "$SYS/$p" ]; then
        echo "!! $p is not installed system-wide -- skipping"; fail=1; continue
    fi
    if [ "$CHECK" = 1 ]; then
        echo "== $p"
        for f in ${FILES[$p]}; do
            pf="$HERE/patches/${p}__$(basename "$f").patch"
            if patch --dry-run -p1 -d "$SYS/$p" -i "$pf" >/dev/null 2>&1; then
                echo "   applies cleanly : $(basename "$f")"
            else
                echo "   WOULD FAIL      : $(basename "$f")   <-- upstream changed this file"
                fail=1
            fi
        done
        continue
    fi

    # back up whatever is there now
    if [ -d "$LOC/$p" ]; then
        b="$HERE/superseded/$(date +%Y%m%d-%H%M%S)/$p"
        mkdir -p "$(dirname "$b")"; mv "$LOC/$p" "$b"
        echo "== $p  (previous override saved to $b)"
    else
        echo "== $p"
    fi

    mkdir -p "$LOC"
    cp -a "$SYS/$p" "$LOC/$p"
    for f in ${FILES[$p]}; do
        pf="$HERE/patches/${p}__$(basename "$f").patch"
        if patch -p1 -d "$LOC/$p" -i "$pf" >/dev/null 2>&1; then
            echo "   patched : $(basename "$f")"
        else
            echo "   FAILED  : $(basename "$f")  -- upstream QML changed; reapply by hand"
            fail=1
        fi
    done
done

echo
if [ "$fail" = 0 ]; then
    echo "All patches applied against $(plasmashell --version 2>/dev/null)."
    [ "$CHECK" = 0 ] && echo "Log out and back in, or run:  kquitapp5 plasmashell && kstart5 plasmashell"
else
    echo "Some patches did not apply. The affected widget will use STOCK behaviour"
    echo "until it is reapplied by hand -- which is the safe failure, not a broken panel."
fi
exit $fail
