#!/bin/bash
# Build the all-in-one Exxos desktop package.
#
#   ./packaging/build-deb.sh
#
# Produces packaging/out/exxos-desktop_<version>_amd64.deb
#
# ALPHA, and MX 23 only. Everything here is compiled against the Qt 5.15 / KF5
# 5.103 ABI that MX 23 ships. It will refuse to install elsewhere, because a
# Dolphin that starts and then crashes on an ABI mismatch is a far worse
# failure than one that never installs.
#
# The package deliberately does NOT replace /usr/bin/dolphin. The packaged
# Dolphin stays exactly where apt put it, untouched and working, and this
# installs beside it under /opt. If any of this turns out to be a bad idea,
# removing the package puts the machine back.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TW="$(cd "$HERE/.." && pwd)"
VERSION=$(tr -d ' \n' < "$TW/VERSION")
PREFIX=/opt/exxos-dolphin
STAGE="$HERE/stage"
OUT="$HERE/out"

rm -rf "$STAGE"
mkdir -p "$STAGE" "$OUT"

echo "=== Exxos desktop $VERSION (ALPHA) ==="

# --- 1. Dolphin ------------------------------------------------------------
# A SEPARATE build tree from the one used day to day: this one is configured
# to install under /opt with an RPATH, and reconfiguring the working tree for
# that would quietly change what the desktop is running.
echo "Building Dolphin..."
cmake -S "$TW/dolphin-src" -B "$TW/dolphin-src/build-pkg" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_INSTALL_RPATH="$PREFIX/lib/x86_64-linux-gnu" \
      -DBUILD_TESTING=OFF >/dev/null
make -C "$TW/dolphin-src/build-pkg" -j"$(nproc)" >/dev/null
make -C "$TW/dolphin-src/build-pkg" install DESTDIR="$STAGE" >/dev/null

# The packaged Dolphin must not claim to BE Dolphin: leave the stock desktop
# file and service definitions where apt put them.
rm -rf "$STAGE$PREFIX/share/applications" \
       "$STAGE$PREFIX/share/kservices5" \
       "$STAGE$PREFIX/share/dbus-1" 2>/dev/null || true
# Development files: headers and cmake config for building AGAINST Dolphin.
# Nothing on a user's machine builds against this, and shipping them just
# makes the package bigger and the file list harder to read.
rm -rf "$STAGE$PREFIX/include" \
       "$STAGE$PREFIX/lib/x86_64-linux-gnu/cmake" 2>/dev/null || true

# --- 2. the computer:/ worker ---------------------------------------------
echo "Building the computer:/ worker..."
"$TW/kio-computer/build.sh" >/dev/null
install -D -m 644 "$TW/kio-computer/computer.so" \
        "$STAGE/usr/lib/x86_64-linux-gnu/qt5/plugins/kf5/kio/computer.so"
install -D -m 644 "$TW/kio-computer/computer.protocol" \
        "$STAGE/usr/share/kservices5/computer.protocol"

# --- 3. media-change polling ----------------------------------------------
# Brightness is a live X property with no persistence of its own; this records
# it and puts it back at login. See exxos/system-tools/exxos-brightness.
install -D -m 755 "$TW/system-tools/exxos-brightness" \
        "$STAGE/usr/bin/exxos-brightness"

install -D -m 755 "$TW/system-tools/exxos-arrange-desktop" \
        "$STAGE/usr/bin/exxos-arrange-desktop"

install -D -m 755 "$TW/system-tools/exxos-browser-theme" \
        "$STAGE/usr/bin/exxos-browser-theme"

install -D -m 755 "$TW/system-tools/exxos-win-colours" \
        "$STAGE/usr/bin/exxos-win-colours"

install -D -m 755 "$TW/system-tools/exxos-gtk-colours" \
        "$STAGE/usr/bin/exxos-gtk-colours"

install -D -m 644 "$TW/system-tools/61-exxos-removable-polling.rules" \
        "$STAGE/etc/udev/rules.d/61-exxos-removable-polling.rules"

# --- 4. launcher -----------------------------------------------------------
# The development launcher deploys the worker and offers to install the udev
# rule. In a package both are already installed, so this is just a launcher.
install -D -m 755 /dev/stdin "$STAGE/usr/bin/dolphin-exxos" <<'LAUNCH'
#!/bin/bash
# Dolphin Exxos Edition.
BIN=/opt/exxos-dolphin/bin/dolphin
if [ ! -x "$BIN" ]; then
    echo "Exxos Dolphin is not installed properly: $BIN" >&2
    echo "Falling back to the system file manager." >&2
    exec /usr/bin/dolphin "$@"
fi
exec "$BIN" "$@"
LAUNCH

# --- 5. make it the file manager ------------------------------------------
# /usr/local/share comes BEFORE /usr/share in XDG_DATA_DIRS, so a desktop file
# with the same id shadows the packaged one -- which is how the menu, the panel
# and every "open containing folder" end up here instead of at stock Dolphin.
# Removing the package removes this file and the stock one takes over again.
if [ -f /usr/share/applications/org.kde.dolphin.desktop ]; then
    mkdir -p "$STAGE/usr/local/share/applications"
    # ONE expression, and "dolphin" must be the whole word.
    #
    # Two expressions -- one for "dolphin", one for "/usr/bin/dolphin" -- ate
    # their own output: \b matches between "dolphin" and "-", so the second
    # rewrote the first's result and the entry came out as
    # "Exec=/usr/bin/dolphin-exxos-exxos", pointing at a binary that does not
    # exist. Requiring whitespace or end-of-line after "dolphin" makes it match
    # the packaged command and nothing else, however many times it is run.
    sed -E 's|^Exec=(/usr/bin/)?dolphin([[:space:]].*)?$|Exec=/usr/bin/dolphin-exxos\2|' \
        /usr/share/applications/org.kde.dolphin.desktop \
        > "$STAGE/usr/local/share/applications/org.kde.dolphin.desktop"
    # An entry pointing at a binary that does not exist is worse than none:
    # the menu keeps working and simply does nothing when clicked.
    entry_exec=$(grep -m1 '^Exec=' "$STAGE/usr/local/share/applications/org.kde.dolphin.desktop" | cut -d= -f2- | awk '{print $1}')
    [ "$entry_exec" = /usr/bin/dolphin-exxos ] || {
        echo "Desktop entry points at '$entry_exec', not /usr/bin/dolphin-exxos" >&2
        exit 1
    }
fi

# --- 6. the Windows 7 theme ------------------------------------------------
# Files only. Applying a theme writes into a user's own configuration, and a
# package installs as root for everybody, so that is left to a command the
# user runs once -- see exxos-theme-apply.
echo "Collecting theme files..."
theme() {
    [ -e "$2" ] || { echo "  ! missing: $2" >&2; return 0; }
    mkdir -p "$STAGE/usr/share/exxos/$1"
    cp -a "$2" "$STAGE/usr/share/exxos/$1/"
    echo "  + $(basename "$2")"
}
theme plasma/look-and-feel "$HOME/.local/share/plasma/look-and-feel/com.exxos.win7"
theme plasma/desktoptheme  "$HOME/.local/share/plasma/desktoptheme/Exxos"
theme aurorae/themes       "$HOME/.local/share/aurorae/themes/exposeair"
theme color-schemes        "$HOME/.local/share/color-schemes/Exxos.colors"

normalise_modes() {
# Normalise permissions.
#
# These files are copied out of a home directory with cp -a, which preserves
# whatever mode they happened to have there -- and three of them were 0600,
# written by an earlier script under a tight umask. Owned by root at 0600 in a
# package, a normal user cannot read them at all, so exxos-theme-apply died
# half way through with "Permission denied". A package's files must be readable
# by everybody; nothing here is a secret.
# Shell scripts must stay executable, or exxos-theme-apply cannot call them.
find "$1" -type d -exec chmod 755 {} +
find "$1" -type f -exec chmod 644 {} +
find "$1" -type f -name '*.sh' -exec chmod 755 {} +
}
normalise_modes "$STAGE/usr/share/exxos"

# --- overlay the plasmoid patches with the repo's copies -------------------
# The look-and-feel bundle may carry stale patches from the build machine's
# home directory. The repo is the source of truth.
REPO_PATCHES="$HERE/plasmoid-patches"
if [ -d "$REPO_PATCHES/patches" ]; then
    DEST="$STAGE/usr/share/exxos/plasma/look-and-feel/com.exxos.win7/bundle/plasmoid-patches"
    # Clear first. Copying over the top leaves behind any patch the build
    # machine's bundle has and the repo no longer does -- a showdesktop patch
    # shipped that way, for a widget the patch set stopped managing.
    rm -rf "$DEST/patches"
    mkdir -p "$DEST/patches"
    cp "$REPO_PATCHES"/patches/*.patch "$DEST/patches/"
    cp "$REPO_PATCHES/rebuild-overrides.sh" "$DEST/"
    cp "$REPO_PATCHES/check-on-login.sh"    "$DEST/"
    cp "$REPO_PATCHES/BASE-VERSION.txt"     "$DEST/"
    chmod 755 "$DEST/rebuild-overrides.sh" "$DEST/check-on-login.sh"
    echo "  + plasmoid patches overlaid from repo"
fi

install -D -m 755 "$TW/packaging/exxos-theme-apply" "$STAGE/usr/bin/exxos-theme-apply"

# --- 7. docs ---------------------------------------------------------------
install -D -m 644 "$TW/README.md" "$STAGE/usr/share/doc/exxos-desktop/README.md"
# THEME-LOG.md is deliberately NOT shipped. It is a development log, and it
# names this machine's real drive labels, its NAS and its IP address, home
# directory paths and hardware serials -- none of which belong in a package
# that goes to other people. The README carries the reasoning users need.
[ -f "$TW/docs/screenshot-computer.png" ] && install -D -m 644 \
    "$TW/docs/screenshot-computer.png" \
    "$STAGE/usr/share/doc/exxos-desktop/screenshot-computer.png"

# --- 8. control ------------------------------------------------------------
INSTALLED_KB=$(du -sk "$STAGE" | cut -f1)
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: exxos-desktop
Version: $VERSION
Section: x11
Priority: optional
Architecture: amd64
Installed-Size: $INSTALLED_KB
Depends: dolphin (>= 4:22.12), kio, libkf5kiocore5, libkf5kiowidgets5,
 libkf5kiofilewidgets5, libkf5solid5, libkf5i18n5, libkf5coreaddons5,
 libkf5parts5, libkf5newstuff5, libkf5kcmutils5, libkf5crash5,
 libqt5core5a, libqt5widgets5, libqt5gui5, libqt5dbus5, libqt5network5,
 libphonon4qt5-4, udev, exxos-icons (= $VERSION)
Recommends: udisks2, samba-common-bin
Maintainer: exxos <exxos_uk@yahoo.co.uk>
Homepage: https://github.com/exxosuk/dolphin-exxos
Description: ALPHA - Windows 7 style desktop for MX 23 (Dolphin + theme)
 Makes MX Linux behave the way someone arriving from Windows 7 expects it to,
 rather than asking them to learn a different set of habits first.
 .
 Includes Dolphin Exxos Edition - a patched Dolphin with an Explorer style
 tile view for drives (icon, name, capacity bar, free space), a computer:/
 that lists every drive including empty bays, network machines found by
 WS-Discovery, and media-change detection for drives the kernel does not poll.
 .
 ALPHA. Built and tested ONLY on MX 23 (Debian 12, Plasma 5.27, KF5 5.103,
 Qt 5.15.8, amd64). No other release is tested or supported.
 .
 The packaged Dolphin is left alone: this installs beside it under /opt and
 removing the package puts the machine back as it was.
 .
 The matching icon theme is in exxos-icons, which is installed with this.
 It is a separate package only because it is 385 MB - twenty times everything
 else here - so it can be rebuilt and republished independently. It is a
 dependency rather than a recommendation because MX turns recommendations off
 by default, and a first install with no icons looks broken.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
    update-desktop-database /usr/local/share/applications >/dev/null 2>&1 || true
    udevadm control --reload >/dev/null 2>&1 || true
    # Existing drives were enumerated before the rule arrived.
    for d in /sys/block/sd[a-z] /sys/block/mmcblk[0-9]; do
        [ -e "$d" ] || continue
        [ "$(cat "$d/removable" 2>/dev/null)" = "1" ] || continue
        udevadm trigger --action=change "$d" >/dev/null 2>&1 || true
    done
    cat <<'MSG'

Exxos desktop installed.

  Dolphin          already the file manager -- open it as usual
  Windows 7 theme  run once, as your normal user:   exxos-theme-apply

This is ALPHA and only MX 23 is supported. Remove it with:
  sudo apt remove exxos-desktop

MSG
fi
exit 0
EOF
chmod 755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
exit 0
EOF
chmod 755 "$STAGE/DEBIAN/prerm"

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then
    kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
    update-desktop-database /usr/local/share/applications >/dev/null 2>&1 || true
    udevadm control --reload >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 755 "$STAGE/DEBIAN/postrm"

DEB="$OUT/exxos-desktop_${VERSION}_amd64.deb"
fakeroot dpkg-deb --build "$STAGE" "$DEB" >/dev/null
echo
echo "Built: $DEB  ($(du -h "$DEB" | cut -f1))"
dpkg-deb -I "$DEB" | sed -n '1,12p'
