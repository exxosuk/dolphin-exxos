#!/bin/bash
# Exxos Theme — SNAPSHOT
#
# Captures every file this theming effort changed into a timestamped folder,
# so the whole look can be restored if something breaks later.
# Companion: exxos-theme-restore.sh
#
# Deliberately does NOT copy Chrome's Preferences / Local State wholesale —
# those contain personal browsing data. The two theme-relevant settings inside
# them are recorded as a re-apply script instead.

set -u
BASE="$(cd "$(dirname "$0")" && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$BASE/snapshot-$STAMP"
mkdir -p "$OUT"/{config,local,scripts}

copy() { # copy() <src> <destdir-under-OUT>
    [ -e "$1" ] || { echo "  skip (absent): $1"; return; }
    mkdir -p "$OUT/$2/$(dirname "${1#$HOME/}")"
    cp -r "$1" "$OUT/$2/${1#$HOME/}" 2>/dev/null && echo "  saved: ${1#$HOME/}"
}

echo "Exxos Theme snapshot -> $OUT"
echo "[config]"
for f in \
    "$HOME/.config/kdeglobals" \
    "$HOME/.config/kwinrc" \
    "$HOME/.config/plasmarc" \
    "$HOME/.config/plasmashellrc" \
    "$HOME/.config/kcminputrc" \
    "$HOME/.config/plasma-org.kde.plasma.desktop-appletsrc" \
    "$HOME/.config/gtk-3.0/settings.ini" \
    "$HOME/.gtkrc-2.0" \
    "$HOME/.config/autostart/xrandr-brightness.desktop" ; do
    copy "$f" config
done

echo "[local share]"
for d in \
    "$HOME/.local/share/plasma/plasmoids/org.kde.plasma.icon" \
    "$HOME/.local/share/plasma/plasmoids/org.kde.plasma.showdesktop" \
    "$HOME/.local/share/plasma/plasmoids/org.kde.plasma.kicker" \
    "$HOME/.local/share/plasma/desktoptheme/Exxos" \
    "$HOME/.local/share/aurorae/themes" \
    "$HOME/.local/share/color-schemes" \
    "$HOME/.local/share/plasma_icons" \
    "$HOME/.local/share/icons/Win7-plasma5up-scalable-icontheme-blackysgate.de/index.theme" ; do
    copy "$d" local
done

echo "[scripts]"
copy "$HOME/theme-work/bin/restore-brightness.sh" scripts
copy "$HOME/theme-work/bin/chrome-system-titlebar.sh" scripts

echo "[firefox]"
FFP=$(ls -d "$HOME"/.mozilla/firefox/*.default-release 2>/dev/null | head -1)
[ -n "${FFP:-}" ] && [ -f "$FFP/user.js" ] && { mkdir -p "$OUT/firefox"; cp "$FFP/user.js" "$OUT/firefox/"; echo "  saved: firefox user.js"; }

echo "[browser settings recorded as a re-apply script, not raw profile copies]"
cat > "$OUT/scripts/reapply-browser-settings.sh" <<'INNER'
#!/bin/bash
# Re-applies the two browser settings this theme relies on.
# Chrome MUST be closed: it rewrites these files on exit.
set -e
if pgrep -f "^/opt/google/chrome/chrome" >/dev/null; then
  echo "ERROR: quit Chrome first." >&2; exit 1
fi
python3 - <<'PY'
import json,os
p=os.path.expanduser('~/.config/google-chrome/Default/Preferences')
d=json.load(open(p,encoding='utf-8'))
d.setdefault('browser',{})['custom_chrome_frame']=False      # use the KWin/Aurorae titlebar
json.dump(d,open(p,'w',encoding='utf-8'),separators=(',',':'))
q=os.path.expanduser('~/.config/google-chrome/Local State')
s=json.load(open(q,encoding='utf-8'))
b=s.setdefault('browser',{})
f=[x for x in (b.get('enabled_labs_experiments') or []) if not x.startswith('overlay-scrollbars')]
f.append('overlay-scrollbars@2')                             # classic, always-visible scrollbars
b['enabled_labs_experiments']=f
json.dump(s,open(q,'w',encoding='utf-8'),separators=(',',':'))
print("Chrome settings re-applied.")
PY
INNER
chmod +x "$OUT/scripts/reapply-browser-settings.sh"

cat > "$OUT/MANIFEST.txt" <<MAN
Exxos Theme snapshot
Taken: $(date -Is)
Host:  $(hostname)
Plasma: $(plasmashell --version 2>/dev/null)
KWin:   $(kwin_x11 --version 2>/dev/null)

Look:
  Plasma style      : $(kreadconfig5 --file plasmarc --group Theme --key name)
  Window decoration : $(kreadconfig5 --file kwinrc --group org.kde.kdecoration2 --key theme)
  Titlebar buttons  : L='$(kreadconfig5 --file kwinrc --group org.kde.kdecoration2 --key ButtonsOnLeft)' R='$(kreadconfig5 --file kwinrc --group org.kde.kdecoration2 --key ButtonsOnRight)'
  Widget style      : $(kreadconfig5 --group KDE --key widgetStyle)
  Colour scheme     : $(kreadconfig5 --group General --key ColorScheme)
  Icon theme        : $(kreadconfig5 --group Icons --key Theme)

Restore with: ./exxos-theme-restore.sh $(basename "$OUT")
MAN
echo
cat "$OUT/MANIFEST.txt"
echo "Snapshot complete: $OUT"
