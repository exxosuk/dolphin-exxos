#!/bin/bash
# Catch the Dolphin tile redraw bug in the act.
#
#   ~/theme-work/mx23/catch-redraw-bug.sh
#
# Run this, then go and drag the zoom slider as usual. It grabs the Dolphin
# window ~6 times a second for 25 seconds WITHOUT touching the mouse or the
# focus, so nothing you do gets disturbed and the bad frame does not have to
# survive for me to see it.
#
# Grabs go to a folder it prints at the end.
OUT=/tmp/claude-1000/-home-chris-claude/89422398-5bd7-429b-bbfe-f4c140b36f1c/scratchpad/bugframes
rm -rf "$OUT"; mkdir -p "$OUT"

WID=$(xdotool search --class dolphin 2>/dev/null | while read -r w; do
        xdotool getwindowname "$w" 2>/dev/null | grep -q "Dolphin" && echo "$w"; done | tail -1)
if [ -z "$WID" ]; then
    echo "No Dolphin window found. Open it first, then run this."
    exit 1
fi

echo "Watching window $WID -- $(xdotool getwindowname "$WID")"
echo
echo "  NOW: drag the zoom slider around for the next 25 seconds."
echo "  Do not move the mouse away afterwards; just keep using the slider."
echo
# NOTE: grab the ROOT window, not the Dolphin window.
#
# "import -window <id>" asks X for that window's own contents, which can make
# the toolkit repaint it -- the same thing that happens when you move the mouse
# over it, and the same thing that makes the corruption vanish. Every frame
# captured that way came back clean, from this machine and from mine, which is
# almost certainly the measurement destroying what it was measuring.
#
# Grabbing the root window reads the composited screen instead, so the
# application is never asked to redraw anything.
eval "$(xdotool getwindowgeometry --shell "$WID")"
for i in $(seq -w 1 150); do
    import -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" "$OUT/f$i.png" 2>/dev/null
    sleep 0.16
done

echo
echo "Captured $(ls "$OUT" | wc -l) frames in:"
echo "    $OUT"
echo "Tell Claude it is done and it will go through them."
