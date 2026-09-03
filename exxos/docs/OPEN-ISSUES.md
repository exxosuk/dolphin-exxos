# Open issues — things that are NOT fixed

Written 2026-09-01, after the MX 23 upgrade. Everything here is either
unexplained or unfinished, and all of it needs settling before a universal
installer can claim to reproduce this desktop on another machine.

Fixed work is recorded in `THEME-LOG.md` and `DOLPHIN-PATCHES.md`.

---

## 1. Kicker submenus  — FIXED 2026-09-01

Submenus are light, the arrows are black.

**The fix.** The backdrop has to go in the SYSTEM copy of
`ItemListView.qml`, not the `~/.local` override:

```bash
sudo ~/theme-work/kicker-system-patch/apply.sh
```

Idempotent, keeps a `.exxos-orig` backup, refuses rather than mangles if a
future Plasma changes that QML. **apt will overwrite it on any plasma-desktop
update — re-run it afterwards.** The `~/.local` overrides survive updates;
this one does not, so the installer has to treat it separately.

The arrow colour (`opacity` 0.85 + a dark `ColorOverlay`) is in
`ItemListDelegate.qml` in the normal override and needs nothing special.

Both patches broke on the upgrade for the same reason: Plasma 5.27 renamed the
global `units` context object to `PlasmaCore.Units`, so the 5.20 patches no
longer applied. Regenerated against 5.27; `BASE-VERSION.txt` says `5.27.5`.

---

## 2. Menu category icons showed the Mexico flag — FIXED 2026-09-01

**Cause.** `Icon=mx-tools` (and similar) does not exist in the Win7 theme.
KIconLoader falls back by stripping trailing `-segments`, so `mx-tools`
becomes `mx`, and the theme's `scalable/intl/mx.svg` — the Mexican flag — is a
perfect match. Any icon name whose first segment happens to be a country code
hits this: `no-`, `it-`, `is-`, `in-`, `am-`, `be-`, `do-`, `me-`, `so-`, `to-`.

**The fix.** The flag directory is moved out of the theme entirely:

```
theme-work/removed-from-icon-theme/intl/     (272 files, kept)
```

Nothing on this desktop displays country flags, so nothing is lost. Restore by
moving it back to `scalable/intl` if it is ever wanted.

**Removing it from `index.theme` is NOT enough** — that was tried and the flags
came straight back. The lookup finds the files by scanning the directory
regardless of what `Directories=` says. The files themselves have to go.

---

## THE METHODOLOGY ERROR THAT COST HOURS — read this first

Both problems above were diagnosed correctly early on and then "disproved" by
tests that never actually ran.

**`kquitapp5` is not installed on this machine** (it is in
`libkf5dbusaddons-bin`). Every restart of the form

```bash
kquitapp5 plasmashell; (plasmashell &)      # WRONG - silently does nothing
```

failed at the first command, and the second exited immediately because Plasma
allows only one instance. The original plasmashell — PID 4712, started 16:44 —
kept running through hours of "restarts". Nothing that was changed was ever
loaded, so every test came back negative and produced a confident, wrong
conclusion: that the icon theme was not the cause, that the flag was not
`intl/mx.svg`, that the override file was not being read.

**Restart it by PID, and check the PID changed:**

```bash
OLD=$(pgrep -x plasmashell | head -1)
kill "$OLD"; sleep 4
rm -rf ~/.cache/plasmashell; rm -f ~/.cache/icon-cache.kcache
(setsid plasmashell >/dev/null 2>&1 &); sleep 10
echo "$OLD -> $(pgrep -x plasmashell | head -1)"     # MUST differ
```

The general rule, which also caught the Dolphin redraw bug: **verify the
experiment ran before trusting what it says.** A negative result from a test
that silently did not execute is indistinguishable from a real one.

---

## 3. Dolphin zoom redraw — believed fixed, watch for it

Three separate causes were found and fixed (see `DOLPHIN-PATCHES.md`). Reported
resolved. Flagged here only because it took five wrong attempts before the
cause was found, so treat any recurrence as a real regression rather than
"the same old bug".

**A capture lesson worth keeping:** `import -window <id>` makes the toolkit
repaint the window, which cleared the artefact *before it could be recorded* —
every screenshot taken that way came back clean while the bug was plainly
visible on screen. Grab the root window and crop instead:

```bash
import -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" out.png
```

That single change is what finally caught it. `mx23/catch-redraw-bug.sh` does
this over 25 seconds.

---

## 4. Smaller things

* **`mariadb-upgrade` never run.** Left over from the dist-upgrade. Moot once
  the server packages are removed, which was the plan.
* **121 SVGs with dangling gradient references** — rewritten to `none`, proven
  pixel-identical, so this is closed. Noted only because the originals are kept
  beside each file as `.orig-gradient` and are safe to delete.
* **Group heading font scale** is defined in
  `KItemListGroupHeader::exxosScaledHeaderFont()` and used from three places.
  If a heading is ever clipped again, it is because a fourth place measured the
  unscaled font.
