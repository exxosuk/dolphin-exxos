#!/bin/bash
# Keep the working directory on a machine and the git checkout in step.
#
#   ./sync-repo.sh                report drift, change nothing (default)
#   ./sync-repo.sh --to-repo      working dir  ->  checkout, ready to commit
#   ./sync-repo.sh --from-repo    checkout     ->  working dir, after a pull
#
# WHY THIS EXISTS
# The work happens in theme-work/, which is not a git checkout, and the repo
# has a different shape -- theme-work/dolphin-src/src/ is the repo's src/, and
# theme-work/*.md are the repo's exxos/docs/*.md. Nothing connected the two, so
# they drifted: on 4 September the PC held 1.8.10 to 1.8.13 that had never been
# pushed while the repo held theme work the PC had never seen, and neither side
# was a superset of the other. Merging that by hand took an afternoon. This
# makes the question "what has moved, and which way" a one-line answer.
#
# THEME-LOG.md is deliberately NOT mapped. It records real drive labels, mount
# points and screenshots of the actual desktop. It stays on the machine and on
# the NAS; it is not publishable, and neither is theme-work as a whole.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(dirname "$HERE")"
WORK="${EXXOS_WORK:-$HOME/claude/theme-work}"

[ -d "$WORK" ] || { echo "Working directory not found: $WORK" >&2; echo "Set EXXOS_WORK to point at it." >&2; exit 1; }
[ -d "$REPO/.git" ] || { echo "Not a git checkout: $REPO" >&2; exit 1; }

# repo path : working-directory path.
# kio-computer is listed file by file, not as a directory: the working copy is
# also the build directory and carries computer.so, .bak files and scratch test
# programs that have no business in the repository.
MAP="
src/:dolphin-src/src/
exxos/kio-computer/computer.cpp:kio-computer/computer.cpp
exxos/kio-computer/computer.protocol:kio-computer/computer.protocol
exxos/kio-computer/build.sh:kio-computer/build.sh
dolphin-exxos:dolphin-src/dolphin-exxos
exxos/VERSION:VERSION
exxos/bump-version.sh:bump-version.sh
exxos/deploy.sh:deploy.sh
exxos/gen-computer-view.sh:gen-computer-view.sh
exxos/packaging/build-deb.sh:packaging/build-deb.sh
exxos/packaging/build-icons-deb.sh:packaging/build-icons-deb.sh
exxos/packaging/publish-apt.sh:packaging/publish-apt.sh
exxos/packaging/exxos-theme-apply:packaging/exxos-theme-apply
exxos/packaging/svgtrim.py:packaging/svgtrim.py
exxos/packaging/plasmoid-patches/:packaging/plasmoid-patches/
exxos/system-tools/:system-tools/
exxos/docs/CHANGELOG.md:CHANGELOG.md
exxos/docs/DOLPHIN-PATCHES.md:DOLPHIN-PATCHES.md
exxos/docs/MX23-UPGRADE.md:MX23-UPGRADE.md
exxos/docs/OPEN-ISSUES.md:OPEN-ISSUES.md
exxos/docs/OUTSTANDING.md:OUTSTANDING.md
"

# Things that must never reach a public repository. Checked before any copy
# INTO the checkout, because the scrub has been missed by hand twice.
LEAKS='WIN7|GAMES3|QVO BACKUP|exxos_nas|/home/chris|/home/exxos|exxos21|/media/chris'

MODE="${1:---check}"
case "$MODE" in --check|--to-repo|--from-repo) ;; *) sed -n '3,5p' "$0"; exit 1 ;; esac

drift=0
for pair in $MAP; do
    r="${pair%%:*}"; w="${pair#*:}"
    R="$REPO/$r"; W="$WORK/$w"
    if [ ! -e "$R" ] && [ ! -e "$W" ]; then continue; fi
    if [ ! -e "$R" ]; then echo "only in working dir : $w"; drift=1; continue; fi
    if [ ! -e "$W" ]; then echo "only in checkout    : $r"; drift=1; continue; fi

    if [ -d "$R" ]; then
        out=$(diff -rq "$R" "$W" 2>/dev/null)
    else
        out=$(diff -q "$R" "$W" 2>/dev/null)
    fi
    [ -z "$out" ] && continue
    drift=1
    echo "== $r  <->  $w"
    echo "$out" | sed 's/^/   /'
done

if [ "$MODE" = "--check" ]; then
    echo
    [ "$drift" = 0 ] && echo "In step." || echo "Drift above. Resolve with --to-repo or --from-repo, or by hand if both sides moved."
    exit $drift
fi

if [ "$MODE" = "--to-repo" ]; then
    # The scrub guard. Copying first and grepping after is how personal data
    # ends up in a commit that then has to be rewritten.
    # Scan exactly what would be copied, nothing else. Scanning whole
    # directories instead reported the working copy's own .bak files -- which
    # are never published -- and refused a clean tree.
    targets=""
    for pair in $MAP; do
        w="$WORK/${pair#*:}"
        [ -e "$w" ] && targets="$targets $w"
    done
    hits=$(grep -rInE "$LEAKS" $targets 2>/dev/null | grep -v '/THEME-LOG.md:')
    if [ -n "$hits" ]; then
        echo "REFUSING to copy into the checkout -- this would publish machine detail:" >&2
        echo "$hits" | head -20 | sed 's/^/   /' >&2
        echo "Scrub these first (comments are fine, the names are not)." >&2
        exit 1
    fi
fi

for pair in $MAP; do
    r="${pair%%:*}"; w="${pair#*:}"
    R="$REPO/$r"; W="$WORK/$w"
    if [ "$MODE" = "--to-repo" ]; then src="$W"; dst="$R"; else src="$R"; dst="$W"; fi
    [ -e "$src" ] || continue
    if [ -d "$src" ]; then
        mkdir -p "$dst"; cp -a "$src/." "$dst/"
    else
        mkdir -p "$(dirname "$dst")"; cp -a "$src" "$dst"
    fi
    echo "  $r"
done
echo
if [ "$MODE" = "--to-repo" ]; then
    echo "Copied into $REPO. Review with 'git -C $REPO diff' before committing."
else
    echo "Copied into $WORK. Rebuild with ./deploy.sh."
fi
