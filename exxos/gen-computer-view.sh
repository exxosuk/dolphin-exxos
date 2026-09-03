#!/bin/bash
# Generate a Windows-7-style "Computer" folder for Dolphin.
#
# No compiled code and no Dolphin patching: the folder is filled with .desktop
# link entries, one per drive, whose Name carries the free/total figures.
# Dolphin renders .desktop entries natively, so this survives Dolphin, KIO and
# Plasma updates untouched.
#
# Re-run to refresh the figures (drives mount/unmount, free space changes).

OUT="$HOME/.local/share/my-computer"
mkdir -p "$OUT"
rm -f "$OUT"/*.desktop

human() { numfmt --to=iec --suffix=B --format="%.1f" "$1" 2>/dev/null || echo "$1"; }

emit() { # emit <mountpoint> <label> <icon>
    local mp="$1" label="$2" icon="$3"
    [ -d "$mp" ] || return
    # must be a real mount point - otherwise df silently reports the parent
    # filesystem and every unmounted path shows the root drive's figures
    mountpoint -q "$mp" || return
    read -r _ total used avail _ < <(df -B1 --output=source,size,used,avail,target "$mp" 2>/dev/null | tail -1)
    [ -z "$total" ] && return
    local pct=$(( used * 100 / (total>0?total:1) ))
    local name="$label — $(human "$avail") free of $(human "$total")  [${pct}% used]"
    local safe; safe=$(echo "$label" | tr -c 'A-Za-z0-9_-' '_')
    cat > "$OUT/${safe}.desktop" <<INNER
[Desktop Entry]
Type=Link
Name=$name
Icon=$icon
URL=$mp
INNER
}

emit "/" "rootMX21 (System)" drive-harddisk

for mp in /media/"$USER"/*; do
    [ -d "$mp" ] || continue
    emit "$mp" "$(basename "$mp")" drive-removable-media
done

[ -d /mnt/nas ] && emit /mnt/nas "NAS" folder-network

echo "Generated $(ls -1 "$OUT"/*.desktop 2>/dev/null | wc -l) drive entries in $OUT"
