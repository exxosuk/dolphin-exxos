#!/bin/bash
# Build the apt repository and publish it on GitHub Pages.
#
#   ./packaging/publish-apt.sh           build the repo locally
#   ./packaging/publish-apt.sh --push    build it and publish
#
# Afterwards the package installs the ordinary way:
#
#   sudo apt install exxos-desktop
#
# WHY A SIGNED REPOSITORY.  apt refuses unsigned repositories, and rightly:
# without a signature anything that can answer for the host can hand a machine
# a package that runs as root at install time. The Release file is signed with
# the key below, users install the PUBLIC half, and apt then verifies every
# index it fetches against it.
#
#   key   9B16A83279C5A435   Dolphin Exxos Edition <exxos_uk@yahoo.co.uk>
#
# The suite is called "alpha" on purpose, so the state of the thing is visible
# in the apt line itself rather than only in a description nobody reads.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TW="$(cd "$HERE/.." && pwd)"
VERSION=$(tr -d ' \n' < "$TW/VERSION")
KEYID=9B16A83279C5A435
REPO=exxosuk/dolphin-exxos
PAGES_URL="https://exxosuk.github.io/dolphin-exxos"
SUITE=alpha
APT="$HERE/apt"

DEB="$HERE/out/exxos-desktop_${VERSION}_amd64.deb"
ICONS_DEB="$HERE/out/exxos-icons_${VERSION}_all.deb"
[ -f "$DEB" ] || { echo "No package for $VERSION -- run build-deb.sh first." >&2; exit 1; }
[ -f "$ICONS_DEB" ] || { echo "No icon package for $VERSION -- run build-icons-deb.sh first." >&2; exit 1; }

echo "=== apt repository for exxos-desktop $VERSION ==="
rm -rf "$APT"
mkdir -p "$APT/pool/main/e/exxos-desktop" "$APT/pool/main/e/exxos-icons" \
         "$APT/dists/$SUITE/main/binary-amd64"
cp "$DEB" "$APT/pool/main/e/exxos-desktop/"
cp "$ICONS_DEB" "$APT/pool/main/e/exxos-icons/"

# --- indices ---------------------------------------------------------------
cd "$APT"
dpkg-scanpackages --arch amd64 pool/main > dists/$SUITE/main/binary-amd64/Packages 2>/dev/null
gzip -9kf dists/$SUITE/main/binary-amd64/Packages

# apt wants the sizes and hashes of every index in a Release file, which is
# what the signature actually covers.
cat > "$HERE/apt-release.conf" <<EOF
APT::FTPArchive::Release::Origin "Exxos";
APT::FTPArchive::Release::Label "Exxos desktop";
APT::FTPArchive::Release::Suite "$SUITE";
APT::FTPArchive::Release::Codename "$SUITE";
APT::FTPArchive::Release::Architectures "amd64";
APT::FTPArchive::Release::Components "main";
APT::FTPArchive::Release::Description "Windows 7 style desktop for MX 23 (ALPHA, unsupported elsewhere)";
EOF
apt-ftparchive -c "$HERE/apt-release.conf" release dists/$SUITE > dists/$SUITE/Release
rm -f "$HERE/apt-release.conf"

# --- signature -------------------------------------------------------------
# Both forms: InRelease is what modern apt fetches, Release.gpg is the detached
# signature older clients look for. Cheap to provide both.
gpg --batch --yes --local-user "$KEYID" --armor --detach-sign \
    -o dists/$SUITE/Release.gpg dists/$SUITE/Release
gpg --batch --yes --local-user "$KEYID" --clearsign \
    -o dists/$SUITE/InRelease dists/$SUITE/Release

# --- the public key, in the form apt wants ---------------------------------
# Binary, not ASCII-armoured: a keyring in /etc/apt/keyrings must be a keyring.
gpg --export "$KEYID" > "$APT/exxos-archive-keyring.gpg"

# --- a page for humans who land on the URL ---------------------------------
cat > "$APT/index.html" <<EOF
<!doctype html>
<meta charset="utf-8">
<title>Exxos desktop &mdash; apt repository</title>
<style>
 body{font:16px/1.6 system-ui,sans-serif;max-width:46rem;margin:3rem auto;padding:0 1rem;color:#222}
 pre{background:#f4f4f4;padding:1rem;overflow-x:auto;border-left:3px solid #1f94e0}
 .warn{background:#fff6e0;border-left:3px solid #e0a800;padding:1rem}
 code{background:#f4f4f4;padding:.1em .3em}
</style>
<h1>Exxos desktop</h1>
<p>A Windows&nbsp;7 style desktop for MX Linux&nbsp;23 &mdash; a patched Dolphin with an
Explorer drive view, a <code>computer:/</code> that lists every drive including empty
bays, media-change detection, network discovery, and the theme to match.</p>

<p class="warn"><strong>ALPHA, and MX&nbsp;23 only.</strong> Built and tested on MX&nbsp;23
(Debian&nbsp;12, Plasma&nbsp;5.27, KF5&nbsp;5.103, Qt&nbsp;5.15.8, amd64) and nothing else.
It is compiled against that exact ABI and will not install elsewhere.</p>

<h2>Install</h2>
<pre>sudo mkdir -p /etc/apt/keyrings
sudo wget -O /etc/apt/keyrings/exxos-archive-keyring.gpg \\
     $PAGES_URL/exxos-archive-keyring.gpg

echo "deb [signed-by=/etc/apt/keyrings/exxos-archive-keyring.gpg] $PAGES_URL $SUITE main" \\
  | sudo tee /etc/apt/sources.list.d/exxos.list

sudo apt update
sudo apt install exxos-desktop
exxos-theme-apply        # as your normal user, NOT with sudo</pre>

<p>Log out and back in for the window decorations.</p>

<h2>Remove</h2>
<pre>sudo apt remove exxos-desktop
exxos-theme-apply --undo
sudo rm /etc/apt/sources.list.d/exxos.list /etc/apt/keyrings/exxos-archive-keyring.gpg</pre>

<p>Source, and what every change is for:
<a href="https://github.com/$REPO">github.com/$REPO</a></p>
<p>Current version: <strong>$VERSION</strong></p>
EOF

echo "  pool:    $(ls pool/main/e/exxos-desktop/) + $(ls pool/main/e/exxos-icons/)"
echo "  suite:   $SUITE"
echo "  signed:  $(gpg --list-packets dists/$SUITE/Release.gpg 2>/dev/null | grep -m1 keyid || echo '?')"

# --- verify before publishing ---------------------------------------------
# A repository that does not verify is worse than none: apt will refuse it and
# the user has no way to tell whether the fault is theirs.
gpg --verify dists/$SUITE/InRelease >/dev/null 2>&1 \
    && echo "  InRelease verifies" \
    || { echo "  InRelease DOES NOT VERIFY" >&2; exit 1; }

if [ "$1" != "--push" ]; then
    echo
    echo "Built in $APT -- run again with --push to publish."
    exit 0
fi

# --- publish ---------------------------------------------------------------
echo
echo "Publishing to gh-pages..."
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
git clone --depth 1 "https://github.com/$REPO.git" "$WORK/repo" >/dev/null 2>&1
cd "$WORK/repo"
# A FRESH orphan branch every time, force-pushed.
#
# The site shares a repository with the source but none of its history, so
# publishing never rewrites the source tree. Recreating it rather than adding
# a commit matters now the icon package is 41 MB: keeping the history would
# add that much to the repository on every single publish, for old versions
# nobody can install any more.
git checkout --orphan gh-pages >/dev/null 2>&1
git rm -rq --cached . >/dev/null 2>&1 || true
rm -rf ./* .github 2>/dev/null || true
cp -a "$APT/." .
touch .nojekyll        # or GitHub Pages hides dists/ and pool/
git add -A
git -c user.name="exxos" -c user.email="exxos_uk@yahoo.co.uk" \
    commit -q -m "apt repository: exxos-desktop $VERSION (ALPHA)"
git push -q -f origin gh-pages
echo "Published: $PAGES_URL"
