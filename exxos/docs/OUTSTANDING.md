# Outstanding — Dolphin / Exxos, as of 2026-09-02

Kept so nothing gets lost between sessions. Newest first.

## Read this first

**Until 2026-09-02 the patched build could silently hand off to stock Dolphin.**
`KDBusService` used the name "dolphin", the same as `/usr/bin/dolphin`, so
whichever started first served every later request. Anything that opened a
folder started the stock binary, and after that `dolphin-exxos` just handed it
the window -- so the patched build looked like it was doing nothing and every
fix looked like it had failed. Now fixed; it registers as `dolphin-exxos`.

If a fix ever appears absent again, check the window title says
"Dolphin Exxos Edition" and `readlink /proc/<pid>/exe` points at
`theme-work/dolphin-src/build/bin/dolphin`.

## Open

| # | Item | State |
|---|---|---|
| A | **Published repo cannot rebuild the .deb** | `packaging/`, `kio-computer/`, `system-tools/`, `README.md`, `VERSION` are in `theme-work/`, which is not a git repo. Only `dolphin-src/` is published. Needs consolidating. |
| B | **`theme-work/` is not publishable** | Config backups, `user-places.xbel`, browser state, real-desktop screenshots. Never push it wholesale. Existing `dolphin-src` commit messages also name real drive labels and `~`. |
| C | **True `apt-get install`** | DONE. Signed repo at https://exxosuk.github.io/dolphin-exxos, suite `alpha`. Verified: published InRelease checks out against the published key alone, in a clean keyring. |
| 0 | **Removable media polling** | DONE and confirmed: rule installed, all four removable drives polled at 2000 ms, and kernel/udisks sizes now agree where they used to diverge. A physical floppy swap still needs testing by hand. |
| 1 | **CD/DVD bay in the Places panel** | Blocked. It can be listed, but only under "Places", not "Removable Devices", and `KFilePlacesModel` cannot be extended to fix that -- proof below. Needs replacing the model. |
| 2 | **Card-reader slots under Removable Devices** | Same blocker as #1. |
| 3 | **All device retesting from 2026-09-02 evening** | Invalid. `install.sh --check` proved the installed worker was the 00:19 build all along, so `refreshSolid()` has never run here. Retest after installing. |
| 4 | **NAS / Network empty** | FIXED IN DOLPHIN. `ExxosNetworkDiscovery` keeps the sender address from the WS-Discovery reply instead of resolving the advertised name, so this works on any machine with no setup. `network-tools/fix-nas-name.sh` is now optional. The NAS refuses anonymous listing, so Dolphin asks for credentials once -- correct behaviour, not a fault. |

## Fixed 2026-09-02 (evening) -- needs a run-through on the real hardware

All four were the SAME root cause or were verified in the act; see
`THEME-LOG.md` for the measurements.

| Item | What was wrong | Verified how |
|---|---|---|
| Icon view never updated (disc, floppy, eject, labels) | The pooled KIO worker never dispatches Qt events, so Solid's device cache was frozen from the moment that worker started. Reloading could not help; restarting Dolphin got a new worker, which is why that "worked". | `kio-computer/solid-stale-test.cpp`: a loop device attached and detached mid-run was invisible without `processEvents()` and appeared/vanished within one cycle with it. |
| No way to mount without opening, and no way to unmount | Auto-mount is off by default by design, and nothing else offered the operation. | Mount / Unmount / Eject added to the device context menu in `computer:/`. Not yet exercised on real media. |
| Auto-mount option not findable | It was under View. | Moved to Settings, and added to the `computer:/` background context menu. |
| Zoom "grow" animation skipped at some steps | The second layout pass of a zoom stops every animation when the grid gains or loses a column, icon resize included. | Caught in the act with `EXXOS_ZOOM_DEBUG=1`: `icon 41 -> 32 animate=0` -- an animation cut off mid-flight. |

## Done in this stretch

* Computer bookmark restored (this session's cleanup had deleted it), and the
  cleanup narrowed so it can only ever match entries it created.
* `[no label]` shown for readable media carrying no volume label.
* `No disc` instead of "not mounted" for an empty optical tray.
* Empty bays listed in `computer:/` (hardware sweep, pass 2).
* Drive status moved to its own line in a tile instead of being elided.
* Split panes share the softer background.
* Zoom slider floored at 32px; tile scales from 32.
* Tile grid flush left; capacity bar snapped to whole pixels.

## #2/#3 grouping: PROVEN unsafe, not merely awkward  (2026-09-02)

Appending synthetic rows to `KFilePlacesModel` -- the remaining way to get an
empty bay under "Removable Devices" -- would crash the panel:

```cpp
Solid::Device KFilePlacesModel::deviceForIndex(const QModelIndex &index) const
{
    KFilePlacesItem *item = static_cast<KFilePlacesItem *>(index.internalPointer());
    if (item->isDevice()) {          // <-- no null check, and not virtual
```

A synthetic row has a null internalPointer, so the first call dereferences
null. `url()`, `text()`, `icon()` and `isHidden()` all route through `data()`
and would have been safe; `deviceForIndex()` does not and cannot be overridden.

Combined with the earlier finding (a UDI groups it correctly but makes it
vanish when empty), BOTH routes through KFilePlacesModel are closed. The only
remaining option is a replacement model, which also means owning it across
future KIO updates.

## What is known about #1, the animation

* End state is CORRECT at every step. Measured twice -- instrumented sizes and
  drawn pixels -- 32/48/64/80/96/112/128 with the source pixmap matching.
* `KItemListView::updateWidgetProperties()` cleared `animateIconResizing`
  whenever the widget's own size came out unchanged. Fixed; did not resolve it.
* Instrumenting that branch during a single zoom step logged **zero**
  `iconSize() != newIconSize` events, so the widget's icon size is already
  current by the time that code runs -- something else sets it earlier. That
  is the thread to pull next: find who calls `KItemListWidget::setIconSize()`
  first, because the animation branch is dead if the value is already right.
* An earlier run at high zoom DID show the widget ramping 114 -> 128, so the
  animation is reachable in some conditions. Establish which.

### 2026-09-02 session

* `iconSize` IS correctly declared `Q_PROPERTY(int iconSize READ iconSize
  WRITE setIconSize)`, so `QPropertyAnimation` on it is sound.
* `m_iconSize` is written in only two places: `setIconSize()`, and
  `styleOptionChanged()` when it is still -1 (first time only).
* So the animation machinery and the setter are both fine, and the earlier
  "zero events" result is still unexplained -- that instrumentation may not
  have been running against the binary being tested. **Re-run it before
  drawing any conclusion from it.**

**Do not attempt another fix from theory.** The three so far were all
plausible and all wrong. The next step is finding the caller that sets the
size early, then deciding.
