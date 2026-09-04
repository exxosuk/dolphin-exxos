# Changelog

Every released version of the Exxos desktop packages, newest first.

Versions are `major.minor.patch`: the patch number moves with each change, the
minor number with each push to the repository. `exxos-desktop` and
`exxos-icons` are released together and share a version number.

## Unreleased

### Favourites

A **Favourites** entry in the Places panel, directly under Home with a yellow
star, holding shortcuts to anything worth keeping to hand. It opens in the
normal view, the way Recent Files does.

* **Add to Favourites** and **Remove from Favourites** in the context menu of
  any file or folder -- an ordinary listing, the desktop, or search results.
  Removing deletes the shortcut and never the thing it points at.
* Shortcuts are symbolic links in `~/.local/share/exxos/favourites`, so the
  whole state is a directory the user can inspect, back up or repair with a
  file manager. Links rather than copies: a favourite that duplicates a file
  goes stale silently.
* A name already taken becomes "name (2)" rather than something hashed.

* Also available outside Dolphin. The desktop is Plasma's Folder View and other
  file managers are not Dolphin either, so the same two actions ship as a KIO
  service menu, under a **Favourites** submenu, backed by `exxos-favourite`.

### Icons

* The NAS and the Network shortcut are left to the shipped theme. Windows draws
  both as a folder with a globe on it, so taking its versions made the two
  indistinguishable; the theme gives them distinct icons, which is more use
  here than being authentic.

### Favourites tabs

* Fixed: the removal confirmation showed its `<filename>` markup to the user
  verbatim. That markup is KUIT, and only the `xi18n*` family expands it.


A row of tabs across the top of the Favourites view -- All, plus one per group.
A tab **is** a directory inside the favourites folder, so a tab holds shortcuts
the way a folder holds files and there is no list, order file or index to fall
out of step with what is on disk.

* Right-click the tab row -> **Add Tab...** and type a name.
* The **x** on a tab removes it, after asking -- it says how many shortcuts go
  with it, because they do.
* **All** is the root of the favourites folder and has no close button: it is
  where a favourite goes when no tab was chosen.
* The row only appears inside Favourites, and only one level down counts as a
  tab -- a folder you put inside a tab is an ordinary folder.


* `install.sh --check` now reports the worker Dolphin actually loads. It only
  ever compared the root-owned copy under `/usr/lib`, which this build does not
  load once a copy exists in `~/.local` -- so it said STALE while the loaded
  worker was current, and would have said CURRENT while a stale one was in use.
  It also asks any running worker where it was loaded from.
* The record of drives the user unmounted is revalidated at startup. It was only
  ever cleared by a running Dolphin seeing `accessibilityChanged`, so mounting a
  drive with Dolphin closed left it listed as unmounted indefinitely.
* `org.kde.plasma.showdesktop` was a local override byte-identical to the system
  copy: it changed nothing and would have silently shadowed a future Plasma.
  Removed.
* `rebuild-overrides.sh --check` now reports any plasmoid override it does not
  manage, distinguishing one that carries no changes from one that does. It
  found `org.kde.plasma.digitalclock` immediately -- real Win7 clock proportions
  that nothing was maintaining -- which is now a patch like the rest.

## 1.9.21

* Removed a tool from the package that should not have been in it. 1.9.20 was
  built before it was taken out of the tree, so the published package and the
  repository disagreed for a few hours.

## 1.9.20 — colours and blur measured rather than guessed

**Colours.** `exxos-win-colours` reads the palette out of a Windows registry
hive, so the scheme now carries real values instead of approximations: window
and button `240,240,240`, selection `51,153,255`, tooltip `255,255,225`, title
bars `153,180,209` active and `191,205,219` inactive. View stays at
`252,252,251` -- Windows uses pure white, but Chromium takes that as its page
base and pages went blinding.

**Blur.** Strength 5, from Windows 7's own `ColorizationBlurBalance=34` out of
100. At KWin's default the glass is opaque to detail: the colour behind comes
through but every shape is smeared, which reads as the transparency not working
at all.

**GTK and browsers.** `exxos-gtk-colours` writes the KDE scheme into
`colors.css`, which is what GTK applications and Chromium browsers actually
read. Plasma only regenerates that where `kde-gtk-config` is installed; without
it a browser toolbar sits on a stale palette indefinitely. Chrome paints its
toolbar from `theme_bg_color` while Brave uses `theme_base_color` and takes the
tab strip from `theme_bg_color`, so the two are set deliberately rather than
left at the scheme's own values. `exxos-browser-theme` sets every Chrome, Brave
and Chromium profile to follow the system theme and use the window manager's
title bar.

**Desktop.** `exxos-arrange-desktop` creates the standard icons and puts My
Computer, System Settings, Discover and the Wastebin down the top-left, writing
a `positions` key where Plasma has never made one.

**Fixed.** Icon packaging corrupted SVGs: the metadata stripper mishandled
`<sodipodi:namedview>` elements with children, leaving an orphaned closing tag,
so six icons -- including `preferences-system` and `go-previous` -- shipped as
malformed XML and drew nothing. The build now refuses to write an SVG the trim
would break. Dropping oversized icons also left 28 dangling symlinks, which are
now removed. `rebuild-overrides.sh` wrote its backups into the packaged,
root-owned directory, losing the previous overrides while reporting success;
they now go to `~/.local/share/exxos/plasmoid-superseded/`.

**Dolphin.** Zooming left fragments of the previous icons behind: the cell
resizes at once but the icon size animates, so for the length of that animation
the icon is larger than the cell and nothing clipped it. Both the icon and the
item name are now clipped to the widget's rect. Recent Files and Recent
Locations order by when a thing was used rather than by file modification time,
which had buried anything on a drive that is only ever read from. "Send to
Desktop (create shortcut)" added to the item context menu.

## 1.9.0 — the unmount work, and the plasmoid patches made to apply

The PC and the laptop had drifted apart: the PC carried 1.8.10 to 1.8.13, none of
which had ever been pushed, while the laptop had pushed theme work the PC had
never seen. Merged, with the PC's code as the base and the laptop's changes
reapplied on top, so neither side lost anything.

**Unmount, from the PC (1.8.10 - 1.8.13).** Unmount appeared to do nothing. The
guard that makes it stick lived in a worker Dolphin was not loading: only the
`dolphin-exxos` wrapper set `QT_PLUGIN_PATH`, so launching Dolphin any other way
silently loaded the stale root-owned worker under `/usr/lib`. `main.cpp` now
prepends `~/.local/lib/qt5/plugins` to the library paths itself, so the launcher
cannot be bypassed. With that fixed:

* A fourth device state, `locked`, separates "not mounted, will mount when you
  open it" from "you unmounted this, and it will refuse to open". Only `locked`
  draws the padlock.
* The record is written only when `teardownDone` reports success, cleared when a
  drive becomes accessible by ANY route, and cleared when the medium is removed.
* `teardown()` returning false is handled. The udisks2 backend answers false and
  emits nothing when a setup or teardown is already running -- the one path that
  gives exactly "spinner forever, no message".
* The redundant `emblem-unmounted` red star is gone; the padlock already says it.
* State flags moved out of the `m_pixmap.isNull()` guard, so a recycled list
  widget cannot keep another drive's state.

**The plasmoid patches could never apply.** All five carried absolute paths in
their headers (`--- /usr/share/...`, `+++ /home/.../audit/...`), so
`rebuild-overrides.sh`, which uses `-p1`, could not find a single file to patch.
Every run reported WOULD FAIL and a real rebuild would have left every widget
stock. Headers are now relative `a/` and `b/` paths; all five apply cleanly on
plasmashell 5.27.5, and the rebuilt overrides are byte-identical to the working
PC's.

**Patching a file that was already patched.** Where `kicker-system-patch/apply.sh`
has been run, `/usr/share` carries an Exxos edit and keeps the original beside it
as `.exxos-orig`. Copying that and patching it applied the same change twice --
two Rectangles with `id: win7ListBackdrop`, a duplicate-id error, and a start menu
that would not open. `rebuild-overrides.sh` now reverts any `.exxos-orig` before
patching, so it gives the same result whether or not the system patch is
installed.

**The icon applet lost its settings on rebuild.** `contents/config/main.xml`
declares `exxosSlotWidth` and `exxosSpacing`, and no patch covered it -- so a
rebuild produced QML referring to configuration keys that no longer existed, and
quick-launch spacing reverted. Added as a patch and registered in the file list.

**Smaller things.** `rebuild-overrides.sh` suggested restarting with `kquitapp5`,
which is not installed on MX 23; it now names `kstart5 plasmashell --replace`.
The personal-data scrub missed `dolphinview.cpp`, `dolphinmainwindow.cpp`,
`computer.cpp` and the patch headers -- real drive labels and home paths in
comments, now generic.

### exxos-theme-apply — fixed, was broken on a clean install

`exxos-theme-apply` only set 4 of the 12 config keys the theme needs. On the
machine it was built on everything was already configured, so the gaps were
invisible. On a fresh install the start menu stayed stock, quick-launch icons
had large gaps, the widget style was wrong, and the title bar buttons still
had the KDE default layout.

Fixed:
* Sets all KWin settings: button layout (empty left, IAX right), blur on,
  dim-inactive off, window placement centred.
* Sets `widgetStyle=plastique` (was staying at Breeze).
* Sets `LookAndFeelPackage` so System Settings shows Exxos as active.
* Runs `rebuild-overrides.sh` to apply the plasmoid patches (start menu
  white backdrop, quick-launch icon spacing, taskbar single-row layout).
* Sets up the autostart entry for login-time patch rebuilds after Plasma
  upgrades.
* Clears the Plasma theme cache so the new theme renders immediately.
* The `--undo` command now reverses all of the above, not just the four
  keys it used to know about.

### Packaging fixes

* **Plasmoid patches regenerated for Plasma 5.27.5.** The shipped patches
  were from 5.20.5 (MX 21) and silently failed on MX 23 because Plasma 5.27
  renamed `units` to `PlasmaCore.Units`. Patches are now generated from the
  working PC's actual overrides and stored in the repo as the source of truth.
* **Removed the showdesktop patch.** The working PC does not patch it — the
  override was a copy identical to the system file.
* **Shell scripts in the package are now executable.** `normalise_modes` was
  setting everything to 644, so `rebuild-overrides.sh` and `check-on-login.sh`
  could not be executed by `exxos-theme-apply`.
* **Repo patches overlay the bundled copies.** `build-deb.sh` now copies the
  repo's patches into the package, so stale patches from the build machine's
  home directory cannot sneak in.

### exxos-icons

* **814 of 4703 SVGs in the icon theme were malformed** (Adobe `a:` namespace
  used without being declared). Qt refused to parse them, so those icons showed
  as blank. The repaired icon theme from the working PC — with all fixes from
  the theme work sessions — should be used as the package source.

* The README now says outright that this is not a faithful reproduction of
  Windows 7. It reproduces one personal Windows 7 setup — the taskbar, Start
  menu and colours as they were arranged there — not the stock layout. What
  carries over is the way it behaves, not the pixels.

## 1.8.5 to 1.8.9 — unmounting a drive means something

* Unmount appeared to do nothing. The guard that makes it stick lived in a KIO
  worker Dolphin was not loading, because only the wrapper script set
  `QT_PLUGIN_PATH`. Dolphin now prepends its own plugin directory, so the
  launcher cannot be bypassed.
* A drive the user unmounted is drawn as unavailable and refuses to open, with
  a padlock rather than the redundant red star.
* The record follows the drive: written only on a successful teardown, cleared
  when the drive is mounted again by any route, cleared when the medium is
  removed.
* `dolphinrc` is read without QSettings, which had been returning stale values.

## 1.8.0 to 1.8.4 — the search panel

* Two named search lists, with air under the options row.
* Search controls no longer depend on window width.
* The busy bar stops when the search is stopped.
* Searching Computer no longer produces `computer:` URLs that nothing can open.
* Search results take the keyboard when they arrive.

## 1.7.0 to 1.7.1 — search results that other programs can open

* Results are given as real file URLs, so other applications can open them.
* A visible stop control, and saved searches head the drop-down.

## 1.6.0 — wildcards in search

* Wildcards in the search field, and a stop button that actually stops.

Versions between those headings were build numbers rather than releases: the
patch number moves with every change, so not all of them describe something a
user would notice.

## 1.5.2

* Corrected the icon theme credit: the artwork is by Blackcrack
  (blackysgate.de) under CC BY-NC-SA, which requires the author to be credited
  and the licence kept, so the credit stays in the package.
* Replaced the screenshot with one that includes the places panel, which is
  half of what the Explorer layout is showing.

## 1.5.1

* Fixed the menu entry launching stock Dolphin instead of this one. The build
  rewrote `Exec=` with two patterns, and the second matched the output of the
  first, producing a command that does not exist. One anchored rewrite now,
  and the build refuses to package a desktop entry that does not point at
  `dolphin-exxos`.

## 1.5.0

* Fixed files in both packages being installed unreadable for anyone but the
  owner. Three theme files carried private permissions from the machine they
  were copied off, and `exxos-theme-apply` then failed with "Permission
  denied". Both package builds now normalise every mode.

## 1.4.0

* **The icon theme is packaged**, as `exxos-icons`, and apt installs it with
  the desktop package. Without it a fresh install fell back to Breeze and
  looked broken. Kept separate because it is 385 MB installed and everything
  else works without it.
* Rewrote the repository front page: what the project is for, what is in it,
  how to install it, and an ALPHA warning stating that MX 23 is the only tested
  release and that the packaged Dolphin is built against that exact ABI.

## 1.2.0

First packaged release: `exxos-desktop`, installable from a signed apt
repository, alongside the Dolphin that apt already installed rather than
replacing it. Three-part version numbers start here.

### Dolphin Exxos Edition

A patched Dolphin 22.12.3 with the drive view Plasma does not have.

* **Tile view for drives** — icon, volume name, the hardware it lives on, a
  graphical capacity bar and the free space, flowed into columns and turning
  red past 90% full. Scales with the zoom slider down to 32px; the capacity bar
  follows the view width; the drive model sits on its own line, smaller and
  unbolded, under the label.
* **A real `computer:/` worker.** Lists every drive including empty bays, so an
  empty card slot or an optical drive with no disc says so instead of vanishing.
  Groups them as Explorer does: hard disks, removable storage, network
  locations.
* **Media changes are noticed.** The worker services Qt events before it
  enumerates, so its view of the hardware is current; the kernel is polled for
  drives that cannot report a media change by themselves; and the listing
  refreshes once per insertion or removal rather than twice or not at all.
  Optical drives are never rescanned — doing so closes the tray on a disc.
* **A spinner on the drive being read**, in the Windows 7 style, on that drive
  only, until its listing is complete. Plus a status-bar message while drives
  are being read.
* **Mount and unmount from the view**, with automatic mounting available as an
  option that is off by default and states the risk before it is switched on.
* **Network machines are discovered directly** over WS-Discovery and addressed
  by IP, instead of relying on a name lookup that does not answer.
* Places panel restyled to match, listing drive hardware rather than only
  mountable volumes, with distinct hover and selection colours and readable
  capacity bars.
* Zoom animation no longer fires when media changes, and no longer skips
  between certain zoom levels or in split view.
* Registers its own D-Bus name, so opening a folder never hands the window to
  stock Dolphin.
* Installs its `computer:/` worker into the user's own plugin directory at
  launch, so no part of testing or running it needs root or a terminal.
* Drops the dotted focus rectangle from the view.

### The theme

Window decorations, Plasma theme, colour scheme, Start menu styling and a
Windows-style icon set, applied per user with `exxos-theme-apply` and
reversible with `--undo`. Nothing replaces a system file: the packaged Dolphin
stays where apt put it and removing the package puts the machine back.
