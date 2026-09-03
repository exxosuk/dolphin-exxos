#!/bin/bash
# Make the kernel poll removable drives for media changes.
#
#   sudo ./install-media-polling.sh                 install and verify
#   sudo ./install-media-polling.sh --interval 8000  quieter, slower to notice
#   ./install-media-polling.sh --check              report only
#   sudo ./install-media-polling.sh --undo          remove
#
# See the rule file itself for why this is needed and why udisks Rescan is not
# a substitute. One line, one reboot-proof file, no daemon.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
RULE=61-exxos-removable-polling.rules
DEST=/etc/udev/rules.d/$RULE

report() {
    printf '  %-8s %-11s %s\n' "device" "poll(ms)" "state"
    for d in /sys/block/sd[a-z] /sys/block/mmcblk[0-9]; do
        [ -e "$d" ] || continue
        [ "$(cat "$d/removable" 2>/dev/null)" = "1" ] || continue
        n=$(basename "$d")
        p=$(cat "$d/events_poll_msecs" 2>/dev/null)
        sz=$(cat "$d/size" 2>/dev/null)
        case "$p" in
            -1|"") state="NOT POLLED - media changes will be missed" ;;
            0)     state="polling disabled" ;;
            *)     state="polled" ;;
        esac
        printf '  %-8s %-11s %s (size=%s)\n' "$n" "${p:-?}" "$state" "$sz"
    done
}

case "$1" in
--check)
    [ -f "$DEST" ] && echo "rule installed: $DEST" || echo "rule NOT installed"
    report
    exit 0
    ;;
--undo)
    [ "$(id -u)" = 0 ] || { echo "Run me with sudo." >&2; exit 1; }
    rm -f "$DEST"
    udevadm control --reload
    echo "Removed. Existing drives keep their current setting until reboot."
    exit 0
    ;;
esac

INTERVAL=""
if [ "$1" = "--interval" ]; then
    INTERVAL="$2"
    case "$INTERVAL" in
        ''|*[!0-9]*) echo "--interval wants a number of milliseconds." >&2; exit 1 ;;
    esac
fi

[ "$(id -u)" = 0 ] || { echo "Run me with sudo." >&2; exit 1; }

if [ -n "$INTERVAL" ]; then
    # A floppy drive seeks to answer the kernel's check, so the interval is
    # the trade between how soon a swap is noticed and how often the drive
    # makes a noise.
    sed "s/events_poll_msecs}=\"[0-9]*\"/events_poll_msecs}=\"$INTERVAL\"/g" \
        "$HERE/$RULE" > "$DEST"
    chmod 644 "$DEST"
else
    install -m 644 "$HERE/$RULE" "$DEST"
fi
udevadm control --reload

# The rule fires on add|change, so nudge the drives that are already here
# rather than waiting for a replug.
for d in /sys/block/sd[a-z] /sys/block/mmcblk[0-9]; do
    [ -e "$d" ] || continue
    [ "$(cat "$d/removable" 2>/dev/null)" = "1" ] || continue
    udevadm trigger --action=change "$d" 2>/dev/null || true
done
sleep 2

echo
echo "After installing:"
report
echo
echo 'Any line still saying NOT POLLED means the rule did not match that drive;'
echo 'send the output above rather than assuming it worked.'
