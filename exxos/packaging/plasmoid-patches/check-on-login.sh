#!/bin/bash
# Runs at login. Does NOTHING unless Plasma's version has changed since the
# overrides were last built.
#
# WHY: a plasmoid in ~/.local shadows the system one entirely. After a Plasma
# upgrade the old override still wins, and the failure is silent -- no error,
# just a panel widget behaving oddly against a newer Plasma. This notices the
# version change and rebuilds from the new upstream QML plus the Exxos patches.
#
# Deliberately quiet: no notification, no output, nothing touched, when the
# version is unchanged -- which is every login except the one after an upgrade.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
STAMP="$HOME/.config/exxos-plasmoid-built-against"
LOG="$HOME/.cache/exxos-plasmoid-check.log"

now="$(plasmashell --version 2>/dev/null | awk '{print $2}')"
[ -n "$now" ] || exit 0                      # no Plasma? nothing to do

was="$(cat "$STAMP" 2>/dev/null || echo none)"
[ "$now" = "$was" ] && exit 0                # unchanged - the normal case

{
  echo "=== $(date -Is) ==="
  echo "Plasma changed: $was -> $now. Rebuilding Exxos plasmoid overrides."
} >> "$LOG"

if "$HERE/rebuild-overrides.sh" >> "$LOG" 2>&1; then
    echo "$now" > "$STAMP"
    msg="Panel widget patches reapplied for Plasma $now."
    icon=dialog-information
else
    # Record the version anyway so this does not retry every single login;
    # the patch files are unchanged and will not start applying by themselves.
    echo "$now" > "$STAMP"
    msg="Plasma $now changed some panel widgets. Those widgets are using stock behaviour. See $LOG"
    icon=dialog-warning
fi

command -v kdialog >/dev/null 2>&1 && kdialog --title "Exxos theme" --passivepopup "$msg" 12 2>/dev/null &
command -v notify-send >/dev/null 2>&1 && notify-send -i "$icon" "Exxos theme" "$msg" 2>/dev/null &
exit 0
