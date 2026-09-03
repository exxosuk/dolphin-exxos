# Upgrading to MX 23 — what to do, and what to expect

Written 2026-08-31, from MX 21.3 (Debian 11, Plasma 5.20.5, KF5 5.78, Qt 5.15).

**Nothing here is urgent.** The machine is fully patched today. Debian 11's LTS
window is closing though, so this is the plan for when you do move.

## Go to MX 23, not MX 25

| | MX 23 (Debian 12) | MX 25 (Debian 13) |
|---|---|---|
| Frameworks | KF5 5.103, Qt 5.15 — **same generation** | KF6 / Qt6 |
| Theme | works | mostly works |
| `plastique` style | packaged | Qt5 plugin; will not style a Qt6 desktop |
| `computer:/` worker | rebuild; `SlaveBase` deprecated but present | **port** — `SlaveBase` is gone |
| Dolphin patch | rebase 20.12.2 → 22.12.3 | rebase to 25.04.3 **and** port to KF6 |
| Plasmoid patches | re-apply, probably clean | likely need rewriting |

MX 23 is a rebuild. MX 25 is a porting job. Debian 12 is supported into 2028.

## There is no "upgrade" — it is a reinstall

Checked 2026-09-01. MX does not ship an upgrade path between major releases:

* `/usr/bin/mx-*` lists 30 MX tools. None of them upgrades a release.
* `/etc/apt/sources.list.d/` is pinned to `bullseye` on both Debian and MX repos.

MX's position is a fresh install for a major version change, because the MX
packages (mx-tools, the MX kernel, the antiX base) are built against one Debian
release and no transition is built for them.

**And on this machine `/` and `/home` are the same partition** — `/dev/sdb1`,
923 GB, 267 GB used, no separate `/home` in `/etc/fstab`. A clean install
reformats that, home directory included. Everything below assumes that.

The alternative is a straight Debian `bullseye` -> `bookworm` dist-upgrade with
the MX repo swapped over. It usually works and people do it, but MX does not
support it and the MX-specific packages are what break. Given there is a full
drive image, it is recoverable — it is a question of appetite, not of safety.

## Do the laptop first

The laptop is the free canary, and the real unknown is the Dolphin patch
rebasing onto 22.12.3 and the `computer:/` worker rebuilding against KF5 5.103.
Both get discovered there at no cost. Do not do both machines at once.

## Before you upgrade

1. **Back up** — done, and verified 2026-09-01:
   * Full drive image: `BACKUP DRIVE/MX_LINUX/Job-.../hdd-0_e00.pbe`, 218 GB,
     taken 14:55 on 2026-09-01, on a **separate physical disk** (sdc). This is
     the real safety net. Timeshift is not installed and is not needed given this.
   * `backups/pre-mx23/` (776 KB): every config file this work touched, the
     plasmoid overrides as running, the colour scheme, and `VERSIONS.txt`.
   * `BACKUP DRIVE/EXXOS-THEME-REBUILD-KIT/` — all of `theme-work` minus the
     bullseye `devstage/`, plus the full deploy bundle and the repo signing key.

2. **Export the desktop bundle** — done: `deploy/exxos-desktop-20260901.tar.gz`
   (161 MB, 5383 files, icon theme included), on the backup drive. This is the
   clean-install route: it rebuilds the look from scratch rather than dragging
   bullseye config forward.

3. **The icon theme** — 460 MB, not in git, not redistributable, the single
   hardest thing to replace. It is inside the bundle above.

4. **The repo signing key** — `dolphin-exxos-signing-key.asc`, copied to the
   rebuild kit. Lose it and everyone who added the APT repo gets verification
   errors.

## Move the plasmoid overrides aside FIRST

This is the one that bites silently.

```bash
mv ~/.local/share/plasma/plasmoids ~/plasmoids-old-5.20
```

A plasmoid in `~/.local` shadows the system one **entirely**. Left in place,
Plasma 5.27 would ship new QML and your 5.20 copies would still win — no error,
just a start menu or taskbar widget misbehaving. Move them aside, confirm the
panel works on stock, then:

```bash
theme-work/plasmoid-patches/rebuild-overrides.sh
```

That rebuilds from the NEW Plasma's QML plus the 80 lines of Exxos changes. Any
patch that no longer applies fails loudly and that widget keeps stock behaviour.

The login check (`check-on-login.sh`) does this automatically on first login
after the upgrade — but doing it deliberately means you see the result rather
than finding out later.

## Expect to rebuild

**The `computer:/` worker.** `SlaveBase` is deprecated from KF5 5.96 but still
present in 5.103, so it should compile with warnings:

```bash
theme-work/kio-computer/build.sh && sudo theme-work/kio-computer/install.sh
```

**Dolphin.** MX 23 has Dolphin 22.12.3, so the patch needs rebasing:

```bash
cd theme-work/dolphin-src
git fetch --tags
git checkout -b exxos/win7-tiles-22.12 v22.12.3
git cherry-pick <the Exxos commits>
```

Expect conflicts **only** around the five hook points in
`kstandarditemlistwidget.cpp`; the added methods should carry over cleanly.
`DOLPHIN-PATCHES.md` sections 3 and 6 list every hook and the recipe.

The staged build environment (`devstage/`) is tied to bullseye packages and will
need rebuilding for bookworm — `resolve-deps.sh` automates most of that.

## Check after upgrading

- [ ] Panel: quick-launch spacing, start menu colours, taskbar single row
- [ ] Window decoration: title text and buttons present *(if missing, it is the
      Aurorae cache — `rm -rf ~/.cache/kwin && kwin_x11 --replace`)*
- [ ] `plastique` still installed, menus highlight on hover
- [ ] Icon theme applied
- [ ] Dolphin: tile view, capacity bars, `computer:/`
- [ ] Trash widget still on the desktop
- [ ] Global Theme round-trip: switch away and back, confirm it restores

## Things that will NOT survive automatically

| | Why |
|---|---|
| Panel layout | Not in the Global Theme; only the task manager *settings* are scripted |
| Trash widget | Re-add: right-click desktop → Add Widgets → Trashcan |
| Dolphin view properties | Copy `backups/pre-mx23/view_properties/`. **Update the `Timestamp=` lines** or Dolphin silently ignores them |
| The 460 MB icon theme | Copy it manually |

## If it goes wrong

MX has a live-USB rescue and Timeshift. Take a **Timeshift snapshot** before
upgrading — that is the real safety net; everything in `backups/pre-mx23/` only
restores this theme work, not the system.
