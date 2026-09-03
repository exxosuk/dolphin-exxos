#!/bin/bash
# Build everything and make it live, so testing needs no terminal.
#
#   ./deploy.sh              build, deploy, and restart Dolphin
#   ./deploy.sh --no-restart build and deploy, leave running windows alone
#
# WHY.  Changes used to reach the desktop only if someone typed the right
# commands: the packaged desktop entry started STOCK Dolphin, and the
# computer:/ worker needed a root install that was repeatedly missed -- three
# sessions were spent testing against a worker months out of date. Nothing here
# needs a password, and nothing needs to be remembered.
#
# What "live" means afterwards:
#   * Dolphin in the menu, on the panel, and from any "open containing folder"
#     is the Exxos build            (install-desktop-entry.sh)
#   * the computer:/ worker is copied into the user's own plugin path on every
#     start, so it can never lag behind the binary   (dolphin-exxos)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

step() { printf '\n=== %s ===\n' "$1"; }

step "computer:/ worker"
./kio-computer/build.sh

step "Dolphin"
if [ ! -d dolphin-src/build ]; then
    echo "dolphin-src/build is missing - configure it first." >&2
    exit 1
fi
make -C dolphin-src/build -j"$(nproc)" 2>&1 | tail -3

step "desktop entry"
./dolphin-src/install-desktop-entry.sh | sed 's/^/    /'

if [ "$1" != "--no-restart" ]; then
    step "restarting Dolphin"
    # By PID: pkill -f matches this script's own command line and kills it.
    for pid in $(pgrep -x dolphin 2>/dev/null); do
        kill "$pid" 2>/dev/null || true
    done
    sleep 2
    # Detached, so it outlives this shell.
    (setsid ./dolphin-src/dolphin-exxos >/dev/null 2>&1 &)
    sleep 5
fi

step "state"
running=$(pgrep -x dolphin | head -1 || true)
if [ -n "$running" ]; then
    echo "    dolphin pid $running -> $(readlink "/proc/$running/exe")"
else
    echo "    dolphin is not running"
fi
for pid in $(pgrep -x kioslave5 2>/dev/null); do
    cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null)
    case "$cmd" in *computer.so*) echo "    worker  pid $pid -> $(echo "$cmd" | awk '{print $2}')" ;; esac
done
echo
echo "Everything above should point inside this directory or \$HOME/.local."
