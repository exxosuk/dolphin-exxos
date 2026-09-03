# Dolphin Exxos Edition — handover

**Version 1.8.9** · repo `exxosuk/dolphin-exxos` · working copy `~/claude/theme-work/dolphin-src`

## What this is

A patched Dolphin fork plus a custom `computer:/` KIO worker, shipped as part of
the Win7-style desktop for MX Linux 23 / KDE Plasma. It replaces stock Dolphin —
never test against stock, always deploy and test the real thing.

## Build and deploy

```
cd ~/claude/theme-work
bash bump-version.sh --patch      # ALWAYS pass a bump flag
cd dolphin-src
make -C build -j$(nproc)
bash deploy.sh
```

**`bump-version.sh` with no argument only prints the version — it does not bump
and does not regenerate `exxos/exxosversion.h`.** This bit us repeatedly: the
binary kept reporting an old version while carrying new code, which made it look
as though changes had not been applied. If a fix seems not to have taken effect,
check the version in the title bar first.

The `computer:/` worker source lives at `~/claude/theme-work/kio-computer/computer.cpp`
and is mirrored into `dolphin-src/exxos/kio-computer/computer.cpp`. Copy it across
before committing or the repo copy goes stale.

## Layout of the Exxos changes

| Area | Files |
| --- | --- |
| `computer:/` worker | `exxos/kio-computer/computer.cpp` |
| Search box, history, stop button | `src/search/dolphinsearchbox.{h,cpp}` |
| Options row, focus handover | `src/dolphinviewcontainer.cpp` |
| Wildcard filter, `deviceState` role | `src/kitemviews/kfileitemmodel.cpp`, `private/kfileitemmodelfilter.{h,cpp}` |
| Unmounted padlock/greyscale paint | `src/kitemviews/kstandarditemlistwidget.{h,cpp}` |
| Mount sweep, accessibility watch | `src/dolphinmainwindow.{h,cpp}` |
| Mount/Unmount actions, spinner | `src/dolphincontextmenu.cpp` |

## Working, verified

- Wildcard search (`*.mid`): the longest literal run goes to `kio_filenamesearch`
  as `search`, the full pattern rides along as `exxosPattern` and is applied by
  `KFileItemModelFilter::matchesSearchPattern()`.
- Stop button. Note `stopLoading()` does **not** stop `kio_filenamesearch` — the
  worker keeps feeding results (measured 371 → 225 ticks per 2s). The fix is
  `m_loadingFrozen` in `KFileItemModel`, an early return in `slotItemsAdded`, so
  results already on screen survive the stop.
- Recent and Saved search dropdowns; history in `[Search] History`, capped at 20.
- Copy/paste out of search results (it was a focus problem — the view never took
  focus on load; `m_focusViewOnLoad` / `takeFocusViewOnLoad()`).
- Drive icons on `computer:/`. **Do not set `UDS_URL`** — it makes Plasma render
  folder-content previews over the drive icons a second after listing. It was
  tried and reverted.
- Unmounted drives paint greyscale at 0.55 opacity with an `object-locked`
  overlay at 45% icon width, driven by the `deviceState` role from `UDS_EXTRA+3`.

## Open issue — the one to pick up first

**Clicking an unmounted drive in Dolphin still mounts it.**

The worker itself refuses correctly. Asked directly, it does the right thing and
the drive stays unmounted:

```
kioclient5 ls "computer:/_org_freedesktop_UDisks2_block_devices_nvme0n1p1"
# -> Access denied to WIN7 is not mounted. Right-click it and choose Mount to open it.
```

So the refusal logic and the persisted flag both work. Something else mounts the
drive when the tile is double-clicked. Ruled out so far:

- Not Dolphin's own code. The only three `setup()` call sites are the Mount
  context action, and two paths gated on `AutoMountRemovable`, which is `false`.
- Not KDE's device automounter — disabled and unloaded, the drive still mounted.
- No autofs or automount daemon running; no `/etc/fstab` entry; no udev rule in
  `/etc/udev/rules.d` invokes mount.

The decisive clue, from `gdbus monitor --system --dest org.freedesktop.UDisks2`
during a failing click:

```
/org/freedesktop/UDisks2/jobs/56  Operation: 'filesystem-mount'  StartedByUID: <uint32 0>
```

**UID 0 — the mount is started by root**, not by Dolphin (uid 1000). That rules
out the whole user-session side of the search. Next step is to find the root-side
caller: watch `org.freedesktop.UDisks2` on the system bus with the sender name
retained, or `strace -f -e trace=mount` the udisks daemon across a click, and
identify which root process asks. Then block that path rather than adding more
guards in Dolphin.

Also worth checking: the flag is read from `[Exxos] UnmountedByUser` in
`~/.config/dolphinrc`. That file starts with keys **before** any `[section]`
header (`HDMI-0 Height 1080=801`), which `QSettings` treats as malformed — it
abandons the file and silently returns nothing. `userUnmounted()` now parses it
by hand for exactly this reason. Do not reintroduce `QSettings` here, and be
suspicious of any other code reading dolphinrc that way.

## Verifying the deployed worker

`strings` on the worker binary: `QStringLiteral` text is UTF-16 (`strings -el`),
i18n literals are UTF-8 (plain `strings`). The refusal message is an i18n literal.

## Apt repo

`exxos-desktop` 1.8.4 is published. Sign with the Dolphin key `9B16A83279C5A435`
— the older apt key is gone. MX sets `APT::Install-Recommends "0"`, so anything
needed must be a hard Depends.

## Laptop issue awaiting diagnosis

A second machine (same MX 23) installed the Win7 Dolphin package and reports:
taskbar appears updated, **start menu still the old one**, and no Win7-styled
window decorations. Nothing has been investigated yet. Likely suspects are the
kicker system patch (`theme-work/kicker-system-patch/`), the Aurorae decoration
(`theme-work/aurorae-hover/`, `exxos-theme/`), and whether the package actually
pulled them in given Install-Recommends is off.
