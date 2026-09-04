# Outstanding — Dolphin / Exxos

What is still broken or not yet built. Anything fixed and released lives in
`CHANGELOG.md` instead -- this file used to carry both and the duplication made
it hard to see what actually needed doing.

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

## Still broken

| # | What | Where it stands |
|---|---|---|
| 1 | **CD/DVD bay in the Places panel** | Blocked. It can be listed, but only under "Places", not "Removable Devices", and `KFilePlacesModel` cannot be extended to fix that -- proof below. Needs replacing the model. |
| 2 | **Card-reader slots under Removable Devices** | Same blocker as the row above. |
| 3 | **`install.sh --check` checks the wrong worker** | It compares the root-owned copy under `/usr/lib`, which this build no longer loads. It reported STALE while the loaded worker was current, and would report OK while a stale one was in use. Should check what Dolphin resolves. |
| 4 | **A stale `UnmountedByUser` record is never revalidated** | The record is only cleared by a running Dolphin seeing `accessibilityChanged`. Mount a drive while Dolphin is closed and it stays listed. Harmless today because the worker checks `mounted` before `locked`, so the view is still right — but the record lies. Validate it on startup. |
| 5 | **`org.kde.plasma.showdesktop` override is unmanaged** | The PC carries one, `rebuild-overrides.sh` does not know about it, and it is a full copy of older Plasma QML — the exact silent-shadow failure the patch system exists to prevent. Decide whether it is needed; if not, delete it. |

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

## Wanted — not started

### Favourites: a central hub in the Places panel

A new **Favourites** section in Dolphin's Places panel, below My Computer, with
a yellow star. It holds symbolic links to anything the user drops in it -- files,
folders, whole drives -- so it works as one place to reach the things they use,
the way the dock on their Windows 7 machine did.

Ways in and out, all of which need to work:

* Drag a file or folder onto the Favourites view on the right.
* Right-click any file or folder -> Add to Favourites. Must work from an ordinary
  listing, from the desktop, and from **search results** -- search is where it is
  most useful, because the item is in front of you and its folder is not.
* Right-click an entry in Favourites -> Remove from Favourites. Removing takes
  away the link only, never the target.

Notes for whoever builds it:

* Symbolic links, not copies. A favourite that silently goes stale, or that
  duplicates a file, is worse than no favourite.
* A link whose target has gone should be visible as broken rather than
  disappearing quietly or erroring on open -- the same argument as the padlock
  on an unmounted drive.
* Places entries come from `user-places.xbel`. Whether Favourites is a section
  in that file or a directory of links under `~/.local/share/exxos/favourites`
  shown through a custom entry is the first design decision. The directory is
  probably simpler, survives Dolphin upgrades, and can be backed up.
* "Add to Favourites" belongs next to "Send to Desktop (create shortcut)" in
  `DolphinContextMenu::addItemContextMenu()`, which is already common to every
  branch including search results.

## How this project is kept

* **Changelog as you go.** An entry goes under `## Unreleased` when the change
  is made; that heading becomes a version number only when it is actually
  pushed to the apt repository. Writing it in arrears is how 1.6 to 1.8 ended up
  with no entries and everything since 1.5.2 sat filed as unreleased while it
  was live.
* **Rebuild and republish after any change to what is packaged.** 1.9.20 was
  built before a tool was removed from the tree, so the published package
  shipped a file the repository no longer had, and nothing said so.
* **After 1.9 comes 1.10, not 2.0.** Three independent numbers, not decimals.
  2.0 is for a deliberate break and should not happen by counting.
