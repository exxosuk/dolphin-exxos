#!/bin/bash
# Mount every fixed data partition at login, so Dolphin's Places panel shows
# free-space bars straight away instead of "not mounted".
#
# KIO only knows a drive's capacity once it is mounted -- that is why the purple
# bars appeared one by one as they were clicked. Mounting them up front means
# they are all there from the start.
#
# Needs /etc/polkit-1/rules.d/49-exxos-udisks-mount.rules installed, otherwise
# every drive prompts for a password at login, which is worse than the problem.
#
# Installed as an autostart entry; runs as chris, never as root.

# Give udisks and the desktop a moment to settle after login -- but not when
# run by hand from a terminal.
[ -t 1 ] || sleep 8

# NOTE: use -P, not -r. With -r an UNMOUNTED partition prints an empty
# MOUNTPOINT field, the columns shift left, and every unmounted drive -- the
# only ones we care about -- gets skipped. -P emits KEY="value" pairs, so an
# empty field stays a field.
lsblk -Pno NAME,FSTYPE,MOUNTPOINT,TYPE | while read -r line; do
    NAME= FSTYPE= MOUNTPOINT= TYPE=
    eval "$line"
    [ "$TYPE" = "part" ]                     || continue
    [ -n "$FSTYPE" ]                         || continue   # no filesystem
    [ -z "$MOUNTPOINT" ]                     || continue   # already mounted
    case "$FSTYPE" in
        swap|crypto_LUKS|linux_raid_member|LVM2_member) continue ;;
    esac
    if out=$(udisksctl mount -b "/dev/$NAME" --no-user-interaction 2>&1); then
        echo "mounted  /dev/$NAME ($FSTYPE)"
    else
        echo "FAILED   /dev/$NAME ($FSTYPE): $out"
    fi
done
