#!/bin/bash
# Put the Exxos desktop back together on MX 23. Run as chris, NOT with sudo,
# after the upgrade has rebooted.
#
#   ~/theme-work/mx23/rebuild-exxos.sh
#
# Each stage is independent and says plainly whether it worked. Nothing here
# is destructive: the pre-upgrade copies stay where they are.
TW="$(cd "$(dirname "$0")/.." && pwd)"
ok()   { echo "  [ok]   $*"; }
warn() { echo "  [--]   $*"; }
fail() { echo "  [FAIL] $*"; }

[ "$(id -u)" != 0 ] || { echo "run this as chris, not with sudo"; exit 1; }

echo "=== Where we are ==="
echo "  Debian    $(cat /etc/debian_version)"
echo "  Plasma    $(plasmashell --version 2>/dev/null | awk '{print $2}')"
echo "  KF5       $(dpkg-query -W -f='${Version}' libkf5coreaddons5 2>/dev/null)"
echo "  Qt        $(dpkg-query -W -f='${Version}' libqt5core5a 2>/dev/null)"
echo "  Dolphin   $(dpkg-query -W -f='${Version}' dolphin 2>/dev/null)"
echo "  style     $(dpkg -l qt5-style-plugins 2>/dev/null | grep -q '^ii' && echo 'qt5-style-plugins present (plastique)' || echo 'qt5-style-plugins MISSING — plastique will not load')"

echo
echo "=== 1/5  Aurorae / KWin cache ==="
# Always. A stale cache is the single most common "titlebar went blank" cause.
rm -rf "$HOME/.cache/kwin" && ok "kwin cache cleared"

echo
echo "=== 2/5  Plasmoid overrides, rebuilt against the NEW Plasma ==="
if [ -x "$TW/plasmoid-patches/rebuild-overrides.sh" ]; then
    if "$TW/plasmoid-patches/rebuild-overrides.sh"; then
        ok "overrides rebuilt"
    else
        fail "rebuild-overrides.sh — those widgets keep stock behaviour, which is safe"
    fi
else
    fail "rebuild-overrides.sh not found"
fi

echo
echo "=== 3/5  Build environment for bookworm ==="
# devstage was staged from bullseye .debs and is wrong now.
if [ -d "$TW/devstage" ]; then
    mv "$TW/devstage" "$TW/devstage.bullseye" && warn "old bullseye devstage -> devstage.bullseye"
fi
if [ -x "$TW/dolphin-src/resolve-deps.sh" ]; then
    "$TW/dolphin-src/resolve-deps.sh" && ok "bookworm build env staged" \
        || fail "resolve-deps.sh — stages 4 and 5 cannot run until this works"
else
    fail "resolve-deps.sh not found"
fi

echo
echo "=== 4/5  computer:/ worker ==="
if [ -x "$TW/kio-computer/build.sh" ]; then
    if "$TW/kio-computer/build.sh"; then
        ok "computer.so built — install it with:  $TW/kio-computer/install.sh"
    else
        fail "computer.so — SlaveBase is deprecated in KF5 5.103 but still present,"
        echo "         so expect warnings, not errors. Read the output above."
    fi
else
    fail "kio-computer/build.sh not found"
fi

echo
echo "=== 5/5  Dolphin ==="
NEW_DOLPHIN="$(dpkg-query -W -f='${Version}' dolphin 2>/dev/null)"
echo "  MX 23 ships Dolphin $NEW_DOLPHIN; the patch series is against 20.12.2."
cat <<'EOF'
  This is the one part that cannot be scripted — the rebase needs a human
  when hunks conflict. The recipe, with every hook point listed, is in
  DOLPHIN-PATCHES.md sections 3 and 6. In outline:

      cd theme-work/dolphin-src
      git fetch --tags
      git checkout -b exxos/win7-tiles-22.12 v22.12.3
      git cherry-pick <the Exxos commits from exxos/win7-tiles>

  Conflicts should be confined to the five hook points in
  kstandarditemlistwidget.cpp. The added methods carry over whole.

  Until then the old binary will not start (it links KF5 5.78), so
  ~/Desktop/my-computer.desktop points at something that no longer runs.
  Stock Dolphin still works — it is only the tile view that is missing.
EOF

echo
echo "=== Then check by hand ==="
cat <<'EOF'
  [ ] Panel: quick-launch spacing, start menu colours, taskbar stays one row
  [ ] Titlebar text and buttons present
  [ ] Menus highlight on mouse-over (needs plastique)
  [ ] Icon theme applied
  [ ] Trash widget still on the desktop
  [ ] Global Theme round-trip: switch away and back
EOF
