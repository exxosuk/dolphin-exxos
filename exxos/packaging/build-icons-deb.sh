#!/bin/bash
# Build the icon theme package.
#
#   ./packaging/build-icons-deb.sh
#
# WHY IT IS A SEPARATE PACKAGE. The icons are 483 MB unpacked -- twenty times
# everything else put together. Keeping them apart means the desktop package
# stays small enough to rebuild and republish in seconds, and anyone who wants
# the behaviour without the artwork can have it.
#
# LICENCE. The theme is CC BY-NC-SA by Blackcrack (blackysgate.de). That licence
# permits redistribution, including a modified version, on three conditions:
# credit the author, keep the same licence, and do not use it commercially.
# COPYING, AUTHORS and Readme.md are therefore shipped inside the theme exactly
# as they came, and the package's own copyright file repeats it. This is not
# optional decoration -- strip the attribution and there is no longer any right
# to redistribute the files at all.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TW="$(cd "$HERE/.." && pwd)"
VERSION=$(tr -d ' \n' < "$TW/VERSION")
SRC="$HOME/.local/share/icons/Win7-plasma5up-scalable-icontheme-blackysgate.de"
NAME="Win7-plasma5up-scalable-icontheme-blackysgate.de"
STAGE="$HERE/stage-icons"
OUT="$HERE/out"

[ -d "$SRC" ] || { echo "Icon theme not found: $SRC" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/usr/share/icons" "$OUT"
echo "Copying $(du -sh "$SRC" | cut -f1)..."
cp -a "$SRC" "$STAGE/usr/share/icons/$NAME"

# Files this machine's own repairs left behind, not part of the theme.
find "$STAGE/usr/share/icons/$NAME" \
     \( -name "*.orig-gradient" -o -name "*.pre-intl-fix" \) -delete

# A handful of icons carry whole bitmap images inside them -- one is 9 MB on
# its own -- and they are obscure ones nobody sees. Dropping them takes about
# 80 MB off, and a missing icon falls back through the theme's inherit chain
# rather than showing as broken.
echo "Dropping oversized icons with embedded bitmaps..."
dropped=0
while IFS= read -r f; do
    if grep -qm1 "base64" "$f" 2>/dev/null; then
        rm -f "$f"; dropped=$((dropped + 1))
    fi
done < <(find "$STAGE/usr/share/icons/$NAME" -name "*.svg" -size +900k)
echo "  dropped $dropped"

echo "Stripping editor metadata..."
find "$STAGE/usr/share/icons/$NAME" -name "*.svg" -print0 \
    | xargs -0 -n 400 python3 "$HERE/svgtrim.py" | tail -1

# --- copyright -------------------------------------------------------------
install -D -m 644 /dev/stdin "$STAGE/usr/share/doc/exxos-icons/copyright" <<EOF
Files: usr/share/icons/$NAME/*
Copyright: Blackcrack <https://www.blackysgate.de>
License: CC-BY-NC-SA
 Creative Commons Attribution-NonCommercial-ShareAlike.
 .
 You may share and adapt these files provided you credit the author, licence
 any derivative under the same terms, and do not use them commercially. The
 theme's own COPYING, AUTHORS and Readme.md are shipped with it unchanged.
 .
 Modifications made here: files this machine's own repairs left behind were
 removed, a small number of icons containing embedded bitmap images were
 dropped for size, and editor metadata was stripped from the SVGs. No artwork
 was redrawn.
EOF

# Same reason as the desktop package: a couple of files arrived 0600 and would
# be unreadable by anyone but root once installed.
find "$STAGE/usr/share/icons" -type d -exec chmod 755 {} +
find "$STAGE/usr/share/icons" -type f -exec chmod 644 {} +

INSTALLED_KB=$(du -sk "$STAGE" | cut -f1)
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: exxos-icons
Version: $VERSION
Section: x11
Priority: optional
Architecture: all
Installed-Size: $INSTALLED_KB
Depends: hicolor-icon-theme
Maintainer: exxos <exxos_uk@yahoo.co.uk>
Homepage: https://github.com/exxosuk/dolphin-exxos
Description: Classic Windows-style icon theme for the Exxos desktop
 A scalable icon theme in the classic Windows style, so the Exxos desktop
 looks right from the first login instead of falling back to Breeze.
 .
 Artwork by Blackcrack (blackysgate.de), CC BY-NC-SA. Installed under
 /usr/share/icons; select it with exxos-theme-apply or in System Settings.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    gtk-update-icon-cache -q /usr/share/icons/Win7-plasma5up-scalable-icontheme-blackysgate.de 2>/dev/null || true
    kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 755 "$STAGE/DEBIAN/postinst"

DEB="$OUT/exxos-icons_${VERSION}_all.deb"
echo "Packing (xz, this takes a minute)..."
fakeroot dpkg-deb -Zxz -z9 --build "$STAGE" "$DEB" >/dev/null
echo
echo "Built: $DEB  ($(du -h "$DEB" | cut -f1), installs $((INSTALLED_KB / 1024)) MB)"
