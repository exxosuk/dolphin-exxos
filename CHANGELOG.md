# Changelog

Every released version of the Exxos desktop packages, newest first.

Versions are `major.minor.patch`: the patch number moves with each change, the
minor number with each push to the repository. `exxos-desktop` and
`exxos-icons` are released together and share a version number.

## Unreleased

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
