# Dolphin patches — Exxos / Windows 7 tile view

Maintenance record for the local Dolphin source patch. Separate from
`THEME-LOG.md` on purpose: this file is what you need when Dolphin updates and
the patch has to be reapplied.

| | |
|---|---|
| **Patched version** | Dolphin **20.12.2** (`4:20.12.2-1`, Debian bullseye / MX21) |
| **Frameworks** | KF5 5.78.0, Qt 5.15.2 |
| **Source** | `theme-work/dolphin-src`, cloned from `https://invent.kde.org/system/dolphin.git` at tag `v20.12.2` |
| **Branch** | `exxos/win7-tiles` |
| **Baseline commit** | `7e78ad7` — build scripts only, no Dolphin source touched |
| **Patch commit** | `f322ce4` — the actual change |
| **Files changed** | 3, all under `src/kitemviews/` |

---

## 1. What the patch does

Windows Explorer's Computer view lays each drive out as:

```
+--------+
|        |  Drive name                 <- bold
|  icon  |  [=========      ]          <- graphical capacity bar
|        |  520 GB free of 931 GB      <- smaller, muted
+--------+
```

Dolphin cannot do this out of the box, and that was established by test, not
assumption:

- **Icons view** puts text *below* the icon, never beside it.
- **Details view** is a single line. Newlines in item text are **stripped** —
  verified by creating a file whose name contained real newline bytes and
  observing `LINE-ONE\nLINE-TWO\nLINE-THREE` render as `LINE-ONELINE-TWOLINE-THREE`.
- **Per-row bold** is impossible: a row is one string in one font.
- **No capacity-bar painter exists in the item view.** `nm -DC` on
  `libdolphinprivate.so.5.0.0` finds **0** capacity symbols;
  `libKF5KIOFileWidgets.so.5` has 4 — that is the *Places sidebar*, a different
  widget. (Note: the `KCapacityBar` class itself lives in **KWidgetsAddons**,
  not KIOFileWidgets.)

## 2. Design decision — why NOT a new view mode

A proper `DolphinView::TilesView` mode would have meant touching the mode enum,
`viewproperties.cpp`, `dolphinviewactionhandler.cpp`, the `.kcfg` files, the
settings KCM and the menu actions — six or more files spread across the tree,
each a chance for an update to conflict.

Instead the patch **reuses the existing details layout and switches an
individual row to a tile** when the model supplies capacity data. Consequences:

- Only **3 files**, all in one directory (`src/kitemviews/`).
- Rows without capacity data are **completely unaffected** — every ordinary
  folder renders exactly as stock Dolphin.
- No new user-visible setting, so nothing to migrate.
- Any KIO worker can opt a row in, so this is not hard-wired to `computer:/`.

Trade-off, stated plainly: the tile is not selectable as a view mode; it
triggers on data. That is the price of the small surface.

## 3. The changes, file by file

### 3.1 `src/kitemviews/kfileitemmodel.cpp` — surface the capacity roles

In `KFileItemModel::retrieveData()`, immediately after
`data.insert(sharedValue("url"), item.url());`:

Reads `KIO::UDSEntry::UDS_EXTRA` and `UDS_EXTRA + 1` as decimal byte counts and
inserts them as the **`freeSpace`** and **`totalSpace`** roles.

Inserted *unconditionally* rather than behind an `m_requestRole[...]` gate. Both
values are already present in the `UDSEntry`, so reading them is free, and this
avoids registering two new roles in the role table — which would have meant
touching more files.

Also adds `#include <KIO/UDSEntry>`.

**Update risk: LOW.** `retrieveData()` is stable and the anchor line
(`data.insert(sharedValue("url"), ...)`) is the first statement in the function.

### 3.2 `src/kitemviews/kstandarditemlistwidget.h` — declarations

Adds after the three `updateXLayoutTextCache()` declarations:

```cpp
void updateTilesLayoutTextCache();
bool hasCapacityInfo() const;
void drawCapacityBar(QPainter* painter) const;
```

and after `Layout m_layout;`:

```cpp
bool        m_isTile;
qulonglong  m_freeSpace;
qulonglong  m_totalSpace;
QRectF      m_capacityBarRect;
QPointF     m_capacityFreeTextPos;
QStaticText m_capacityFreeText;
```

**Update risk: LOW.** Pure additions.

### 3.3 `src/kitemviews/kstandarditemlistwidget.cpp` — the implementation

Five edits:

1. **Constructor init list** — `m_isTile(false), m_freeSpace(0), m_totalSpace(0),`
   added after `m_layout(IconsLayout),`.
2. **Layout dispatch** in `updateTextsCache()` — `case DetailsLayout:` now calls
   `updateTilesLayoutTextCache()` when `hasCapacityInfo()`, else the original
   `updateDetailsLayoutTextCache()`.
3. **Three new methods** inserted before `updateDetailsLayoutTextCache()`:
   `hasCapacityInfo()`, `updateTilesLayoutTextCache()`, `drawCapacityBar()`.
4. **`m_isTile = false;`** added as the first line of all three original
   `updateXLayoutTextCache()` methods, so a view-mode change cannot leave the
   flag stale.
5. **`paint()`** — two edits: the name is drawn with a bold font when
   `m_isTile`; and after the name, `drawCapacityBar()` plus the free-space
   `QStaticText` are drawn.

Adds `#include <KIO/Global>` (for `KIO::convertSize`) and
`#include <KLocalizedString>` (for `i18nc`).

**Update risk: MEDIUM.** `paint()` and `updateTextsCache()` are the parts most
likely to be refactored upstream. The three new methods are self-contained and
will carry over unchanged; only the five hook points need re-finding.

### Bar colours

Sampled from the Windows 7 reference `theme-work/computer.PNG`, not from the Qt
palette — reproducing that specific look is the entire point:

```
border       (161,161,161)
empty track  (251,251,251)
fill         (96,217,246) -> (72,193,221) -> (15,136,165) -> (2,128,157)
near-full    red variant at >= 90% used, as Explorer does
```

## 4. The other half — the KIO worker

The patch only renders what a worker advertises. `theme-work/kio-computer/computer.cpp`
sets, in `driveEntry()`:

```cpp
e.fastInsert(KIO::UDSEntry::UDS_EXTRA,     QString::number(availB));
e.fastInsert(KIO::UDSEntry::UDS_EXTRA + 1, QString::number(totalB));
```

and `describe()` now returns just the drive name, because the tile draws the bar
and the free-space line itself.

**Fallback for an unpatched Dolphin:** setting the environment variable
`EXXOS_TEXT_BAR=1` makes the worker fall back to the old Unicode text bar, so
the drive list still shows capacity if you ever run stock Dolphin.

## 5. Building

No root needed. The whole toolchain is staged from `.deb` files into
`theme-work/devstage/root` with `dpkg-deb -x` — nothing is installed
system-wide.

```bash
cd theme-work/dolphin-src
./configure-staged.sh      # cmake against the staged tree
./build-staged.sh          # make -j$(nproc)  ->  build/bin/dolphin
```

`resolve-deps.sh` exists because the staged `-dev` packages ship cmake configs
that reference binaries and libraries from their *runtime* packages. It loops:
configure, read the missing path out of the error, symlink it from the system
(or report that a package is needed), retry. It resolved **20** such gaps
automatically on the first run.

Packages that had to be downloaded rather than symlinked, because they are not
installed on this machine at all:

```
cmake  cmake-data  extra-cmake-modules  libjsoncpp24  librhash0
qt5-qmake  qt5-qmake-bin  gettext  gettext-base
kdoctools5  libkf5doctools5  libkf5doctools-dev  kinit  kinit-dev
libkf5coreaddons-dev-bin  libkf5auth-dev-bin  libkf5config-dev-bin
libkf5kcmutils-dev  libkf5newstuff-dev  libkf5parts-dev
libkf5notifications-dev  libkf5crash-dev  libphonon4qt5-dev
```

Two fixes the staged tree needs, both already applied and worth knowing if the
stage is ever rebuilt:

- **Dangling `.so` symlinks.** `-dev` packages ship `libFoo.so ->
  libFoo.so.5.x.y`, but the versioned file is in the runtime package and lives
  system-wide. Fix: for each dangling symlink, link the system copy into the
  stage. 60 libraries needed this.
- **qmake wrapper.** `root/usr/lib/x86_64-linux-gnu/qt5/bin/qmake` is a shell
  wrapper that execs the absolute path `/usr/lib/qt5/bin/qmake`, which does not
  exist system-wide. Replaced with a symlink to the staged real binary
  (original kept as `qmake.wrapper.orig`).
- **liblto_plugin.so.** The staged gcc-10 has `cc1plus` but not the LTO plugin.
  Fixed with `-B<stage>/... -B/usr/lib/gcc/x86_64-linux-gnu/10/
  -fno-use-linker-plugin` in `CMAKE_CXX_FLAGS`.

## 6. Reapplying after a Dolphin update

1. `cd theme-work/dolphin-src && git fetch --tags`
2. `git checkout -b exxos/win7-tiles-<newver> v<newver>`
3. `git cherry-pick f322ce4`
4. Fix conflicts. Expect them **only** in
   `kstandarditemlistwidget.cpp` around the five hook points listed in 3.3;
   the three new methods should apply cleanly.
5. Rebuild and re-run the render test in section 7.

**If Dolphin moves to KF6/Qt6** the patch will need real porting work, not a
cherry-pick — `KIO::UDSEntry` and the item-view classes both change.

### Upstreaming

If this were accepted upstream the maintenance burden disappears entirely. To be
upstreamable it would want: a real view mode rather than data-triggered
switching, the colours taken from the palette rather than hard-coded Win7
values, and a test. As written it is deliberately a **local** patch optimised
for a small diff, not an upstream candidate.

## 7. Verification

Status is recorded honestly per item.

| Check | Status |
|---|---|
| Baseline (unpatched) builds against the staged toolchain | **PASS** — `build/bin/dolphin`, `dolphin --version` -> 20.12.2 |
| Patched tree builds | **PASS** — clean build |
| Worker builds with the `UDS_EXTRA` fields | **PASS** — `kdemain` present |
| **Tile renders on screen** | **PASS** — screenshot below |
| **Bar colours match the reference** | **PASS** — measured, within 1-2 units |
| Ordinary folders unaffected | **PASS** — no capacity roles, so the details layout runs unchanged |

### Render result

Running the patched binary against `computer:/` produces icon-left, bold drive
name, a real graphical capacity bar and the free-space line — the computer.PNG
layout. Screenshot: `scratchpad/tile3.png`; side-by-side against the reference:
`scratchpad/sbs.png`.

Bar colours sampled from the rendered output versus the values sampled from
`computer.PNG`:

```
                 rendered          reference       delta
top edge      (94,215,244)      (96,217,246)      -2,-2,-2
gloss split   (14,135,164)      (15,136,165)      -1,-1,-1
bottom        ( 3,129,158)      ( 2,128,157)      +1,+1,+1
border        (161,161,161)     (161,161,161)      exact
```

### Remaining differences from computer.PNG — stated honestly

1. **Single column.** Explorer flows tiles into 2-3 columns; details view is
   inherently one row per item. Multi-column tiles would need the icon view's
   flow layout, a substantially bigger change.
2. **Decimal precision.** Explorer shows "914 GB free of 1.81 TB"; this shows
   "910.6 GB free of 1.8 TB". Explorer varies precision by magnitude; this uses
   one decimal throughout.
3. **Name colour.** The drive name picks up `[Colors:View] ForegroundNormal`
   (navy, set earlier in the theme work) where Explorer uses near-black. That is
   a colour-scheme choice, not a patch limitation.
4. On a selected row the free-space line uses the "additional info" colour,
   which is low-contrast against the navy selection. Cosmetic.

## 8. Using it — the packaged binary is NOT replaced

`/usr/bin/dolphin` is deliberately left alone: apt would overwrite it on the
next update, and a broken system file manager is a bad failure mode. Use the
launcher instead, which sets the staged library path and runs the local build:

```bash
theme-work/dolphin-src/dolphin-exxos "computer:/"
```

The stock Dolphin stays available and untouched as a fallback.

**Requires** `[DetailsMode] IconSize >= 56` and the `computer:/` location in
details view (`ViewMode=1`) — the tile needs the row height, and it only
replaces the details layout.

---

## 9. Publishing to GitHub

Everything local is ready: the repo is committed on branch `exxos/win7-tiles`,
939 KiB packed, with no personal paths or credentials in any tracked file
(checked with `git grep`).

**You do not have to publish this.** The GPL only requires offering source to
someone you give a *binary* to. Using it privately on your own machine obliges
nothing. Publishing is worth it only if you want a backup, or want to offer it
to KDE later.

### One-time setup

**1. Make an access token.** GitHub stopped accepting account passwords for git
in 2021, so you need a token instead:

- github.com -> your avatar -> **Settings**
- bottom left: **Developer settings**
- **Personal access tokens** -> **Tokens (classic)** -> **Generate new token (classic)**
- Note: `dolphin-exxos`; Expiration: your choice; tick the **`repo`** box
- **Generate token**, then copy it. It is shown once. Treat it like a password.

**2. Make an empty repository.** github.com -> **+** -> **New repository**
- Name: `dolphin-exxos`
- Public or Private — either is fine; the GPL does not force public
- **Do not** tick "Add a README" — the repo must be empty or the push is refused

**3. Push.**

```bash
cd ~/theme-work/dolphin-src
./push-to-github.sh <your-github-username>
```

When it asks for a **username**, type your GitHub name. When it asks for a
**password**, paste the **token** — the cursor will not move as you paste, that
is normal.

### To avoid retyping the token

```bash
git config --global credential.helper store
```

This writes it in plain text to `~/.git-credentials`. Fine on a single-user
machine; if you would rather not, `credential.helper cache` keeps it in memory
for 15 minutes instead.

### If it is rejected

- *"Repository not found"* — the repo name or username is wrong, or the token
  lacks the `repo` scope.
- *"Updates were rejected"* — the GitHub repo is not empty (you ticked README).
  Either delete and recreate it empty, or `git push -u origin exxos/win7-tiles --force`.
- *"Support for password authentication was removed"* — you typed your account
  password instead of the token.

### Note on what gets published

The repo contains the full Dolphin source at `v20.12.2`, which is GPL-2.0+ and
freely redistributable, plus the patch and build scripts. Nothing proprietary.
The Windows 7 reference screenshots are **not** in this repo and should not be
added — they are Microsoft screenshots.

---

## 10. Branding — "Dolphin Exxos Edition"

`src/main.cpp`. The window title and About box now read
**Dolphin Exxos Edition (20.12.2)**, and the version string is
`1.0 · base Dolphin 20.12.2`.

The base version is **not hardcoded**. It comes from `DOLPHIN_VERSION_STRING`,
which `ecm_setup_version()` generates from the top-level `CMakeLists.txt`, so
after rebasing onto a newer Dolphin tag the name and About box report the new
base automatically with no edit here.

`EXXOS_EDITION_VERSION` (defined next to the includes in `main.cpp`) tracks the
patch set itself — bump it when the patch changes, leave it alone when merely
rebasing.

The application **id** is deliberately left as `dolphin`, not renamed. Changing
it would orphan `dolphinrc` and every view-property file, losing all the
configuration built up in this work.

**Update risk: LOW.** One self-contained block in `main.cpp`.

## 11. Tile view in icons layout — the multi-column grid

Initially the tile was implemented for the details layout only. Because the
worker no longer emits the Unicode text bar, icon view was left showing just the
drive name — a regression. Fixed, and it turned out to be the *better* layout:

**Icon view flows items into multiple columns, which is how Explorer's Computer
view actually looks.** Details view can only ever be one row per item, so the
icons layout is the closer match to `computer.PNG`.

Three coordinated changes:

1. **`kstandarditemlistwidget.cpp`** — `updateIconsLayoutTextCache()` delegates
   to `updateTilesLayoutTextCache()` when the item has capacity data.
2. **Same file** — `iconOnTop` becomes
   `(m_layout == IconsLayout) && !m_isTile`, so a tile puts the icon on the left
   in icons layout as well. The tile also derives its icon box from
   `option.iconSize` in icons layout, because there the cell is taller than the
   icon (in details the row height *is* the icon box).
3. **`views/dolphinitemlistview.{h,cpp}`** — `updateGridSize()` switches the
   icons grid to wide, short cells (`iconSize + 220px bar` wide, three text
   lines tall) when `viewHasCapacityItems()` is true.

### The timing trap

`updateGridSize()` runs **before the model has any items**, so the capacity check
returned false and the tiles were laid out in normal narrow cells — the
free-space text truncated to "910.6 GB free of 1.8 …".

Fixed by overriding `onModelChanged()` to connect to the model's
`itemsInserted` / `itemsRemoved`, and relaying out **only when the answer
changes** (`m_hasCapacityItems`), since `itemsInserted` fires repeatedly while a
directory loads and `updateGridSize()` is not cheap.

**Update risk: MEDIUM** for `dolphinitemlistview.cpp` (its `updateGridSize()` is
occasionally reworked upstream); LOW for the widget changes.

## 12. Packaging — what a `.deb` would have to contain

Established by inspection, and it matters:

```
RUNPATH   ~/theme-work/dolphin-src/build/bin
libdolphinprivate.so.5 => build/bin/libdolphinprivate.so.5
libdolphinvcs.so.5     => build/bin/libdolphinvcs.so.5
```

**The patch lives in `libdolphinprivate`, not in the `dolphin` executable.** So a
package must ship that library too — shipping only the binary would silently
load the system (unpatched) library and the tiles would vanish.

It must **not** install `libdolphinprivate.so.5` to `/usr/lib`, because the
stock `dolphin` package owns a file of that exact name; overwriting it would
break stock Dolphin for every other application that links it.

Correct approach: install everything under a private prefix, e.g.

```
/opt/dolphin-exxos/bin/dolphin
/opt/dolphin-exxos/lib/libdolphinprivate.so.5
/opt/dolphin-exxos/lib/libdolphinvcs.so.5
```

with `RUNPATH=/opt/dolphin-exxos/lib`, plus the `computer.so` KIO worker and the
desktop file. The package can then `Depends: dolphin` and reuse the stock data
files (`dolphinui.rc` etc.), which the binary already finds because its
application id is still `dolphin`.

**ABI constraint, stated plainly:** such a package is tied to this exact
Frameworks/Qt ABI — **KF5 5.78 / Qt 5.15 on Debian 11 (bullseye) or MX 21**. On a
different distribution or a newer KDE it will not load. Anything else needs a
rebuild from source on that machine.

---

## 13. Distribution — the `.deb` and the APT repository

Confirmed with the user: the second machine runs the **same MX 21**, so the same
KF5 5.78 / Qt 5.15 ABI. A binary package works; no source build needed there.

### Building the package

```bash
cd theme-work/dolphin-src
./make-deb.sh                 # -> dist/dolphin-exxos_20.12.2+exxos1_amd64.deb   (1.5 MB)
EXXOS_REV=2 ./make-deb.sh     # bump the revision for a new release
```

The version is `<base dolphin>+exxos<n>`, e.g. `20.12.2+exxos1`. That sorts
*after* plain `20.12.2`, and `+exxos2` sorts after `+exxos1`, so `apt upgrade`
does the right thing without any special handling.

### Layout, and why

```
/opt/dolphin-exxos/bin/dolphin
/opt/dolphin-exxos/lib/x86_64-linux-gnu/libdolphinprivate.so.5.0.0   <- the patch lives HERE
/opt/dolphin-exxos/lib/x86_64-linux-gnu/libdolphinvcs.so.5.0.0
/usr/lib/x86_64-linux-gnu/qt5/plugins/kf5/kio/computer.so            <- KIO must find these
/usr/share/kservices5/computer.protocol                                 at the system path
/usr/bin/dolphin-exxos                                               <- thin launcher
/usr/share/applications/dolphin-exxos.desktop
```

**The private prefix is not cosmetic.** `libdolphinprivate.so.5` is owned by the
stock `dolphin` package. Installing our build over it would break stock Dolphin
for everything that links that library. Under `/opt` there is no collision, and
cmake already sets `RUNPATH=/opt/dolphin-exxos/lib/x86_64-linux-gnu` on the
installed binary so it loads the patched copy.

**Verified, not assumed:** every one of the 56 files in the package was checked
with `dpkg -S`; none is owned by another package.

The package `Depends: dolphin`, and reuses its data. The application id is still
`dolphin`, so it shares `dolphinrc` and the view-property files — the tile view
settings carry over.

### The APT repository

```bash
./make-apt-repo.sh            # regenerates docs/
```

GitHub Pages serves a repository's `docs/` folder over https, which is all APT
needs — no server, no cost.

```
docs/dists/stable/Release
docs/dists/stable/main/binary-amd64/Packages(.gz)
docs/pool/main/dolphin-exxos_*.deb
docs/.nojekyll
```

Verified: the SHA256 in `Packages` matches the actual `.deb`, and `Release`
carries 16 checksum lines.

### Publishing, and setting it up on the second machine

1. Push the repo (see section 9), then on GitHub: **Settings -> Pages -> Source:
   Deploy from a branch -> Branch: `exxos/win7-tiles`, folder: `/docs`**.
   The URL becomes `https://<username>.github.io/<repo>/`.

2. On her machine, once:

```bash
echo "deb [trusted=yes] https://<username>.github.io/<repo>/ stable main" \
  | sudo tee /etc/apt/sources.list.d/dolphin-exxos.list
sudo apt update
sudo apt install dolphin-exxos
```

3. From then on your updates arrive through the normal update manager. To ship
   one: `EXXOS_REV=2 ./make-deb.sh && ./make-apt-repo.sh`, commit, push. Her next
   `apt upgrade` picks it up.

### On `[trusted=yes]` — the honest trade

The repository is **unsigned**, so apt will not verify the packages actually came
from you. For a personal repo between two known machines that is a reasonable
trade. It would **not** be acceptable for a public repo that strangers install
from: anyone able to serve that URL could ship arbitrary root-run code.

To sign it properly:

```bash
gpg --full-generate-key                       # once
REPO_GPG_KEY=<key-id> ./make-apt-repo.sh      # produces InRelease + dolphin-exxos.gpg
```

Then she installs the key instead of using `trusted=yes`:

```bash
sudo curl -fsSL https://<username>.github.io/<repo>/dolphin-exxos.gpg \
     -o /etc/apt/trusted.gpg.d/dolphin-exxos.asc
echo "deb https://<username>.github.io/<repo>/ stable main" \
  | sudo tee /etc/apt/sources.list.d/dolphin-exxos.list
```

### Limits worth stating

- Tied to **MX 21 / Debian 11, KF5 5.78, Qt 5.15, amd64**. A different release
  or architecture needs a rebuild.
- `dist/` is git-ignored; the published copy lives in `docs/pool/`. Do not commit
  both or the repo doubles in size per release.

---

## 14. Published

Repository: **https://github.com/exxosuk/dolphin-exxos**, branch
`exxos/win7-tiles`. APT repo served from `docs/` via GitHub Pages at
`https://exxosuk.github.io/dolphin-exxos/`.

Signed with GPG key **9B16A83279C5A435**
(*Dolphin Exxos Edition <exxos_uk@yahoo.co.uk>*), so clients verify packages
normally — no `[trusted=yes]`.

### The shallow-clone trap

The first push was **rejected**:

```
remote: fatal: did not receive expected object 0c8afb230274...
error: remote unpack failed: index-pack failed
 ! [remote rejected] exxos/win7-tiles -> exxos/win7-tiles (failed)
```

Cause: the source was cloned with `git clone --depth 1` to keep the download
small. That produces a **shallow** repository whose base commit records a parent
that was never fetched. GitHub validates the object graph on receipt, found the
gap and refused the entire push — after uploading 5.35 MiB, which makes it look
like a transfer failure rather than a history problem.

Fix, without downloading Dolphin's full history (~200 MB):

```bash
git replace --graft <base-commit>          # declare it parentless
git filter-branch -f --tag-name-filter cat -- --all
rm -f .git/shallow
git replace -d <base-commit>
rm -rf .git/refs/original
git reflog expire --expire=now --all
git gc --prune=now
```

All 15 commits preserved; repo 5.6 MB; `git fsck` clean.

**If you ever re-clone this source, do NOT use `--depth 1`** unless you only
intend to build locally. Use a full clone, or expect to repeat the above.

The same `filter-branch` pass rewrote the author email on the early commits from
the private address to the public one, since GitHub shows commit authors
publicly regardless of what the package metadata says.

The repository deliberately does **not** carry Dolphin's upstream history. To
diff against upstream:

```bash
git remote add upstream https://invent.kde.org/system/dolphin.git
```

### End-to-end verification of the published repository

Tested against the live URLs exactly as `apt` would, using an isolated keyring
containing ONLY the published public key:

```
1. public key fetched          rsa4096 9B16A83279C5A435
                               uid  Dolphin Exxos Edition <exxos_uk@yahoo.co.uk>
2. InRelease signature         Good signature
3. Packages sha256 vs the
   signed Release              MATCH  b2b87b8a6b05da43...
4. .deb sha256 vs Packages     MATCH  (1 515 692 bytes)
5. package metadata            dolphin-exxos 20.12.2+exxos1, deps resolve
```

The chain of trust is therefore complete: the key signs the Release, the Release
fixes the Packages hash, and Packages fixes the .deb hash. A tampered package at
any point would fail verification on the client.

---

# Port to Dolphin 22.12.3 (MX 23 / KF5 5.103) — 2026-09-01

Branch `exxos/win7-tiles-22.12`, cut from `v22.12.3`. The 20.12.2 series is
`exxos/win7-tiles` and stays for reference.

## Build (no staged toolchain any more)

MX 21 could not install the Qt5/KF5 `-dev` packages: `qtbase5-dev` pulled
`libgl-dev` demanding `libgl1 = 1.3.2-1` while MX shipped `1.4.0-1~mx21+1`.
MX 23 ships `libgl1 1.6.0-11.6.0-1` and a matching `libgl-dev`, so the conflict
is gone and `devstage/` is obsolete.

```bash
sudo apt-get install -y cmake extra-cmake-modules g++ baloo-kf5-dev \
  libkf5baloowidgets-dev libkf5filemetadata-dev libkf5newstuff-dev \
  libkf5parts-dev libkf5activities-dev kuserfeedback-dev libphonon4qt5-dev \
  kinit-dev qtbase5-dev libkf5kio-dev libkf5solid-dev libkf5i18n-dev \
  libkf5coreaddons-dev libkf5kcmutils-dev libkf5notifications-dev libkf5crash-dev

cd theme-work/dolphin-src && rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && make -j$(nproc)
```

The binary is run from the build tree by `dolphin-exxos`; `/usr/bin/dolphin`
is deliberately left stock so apt can never break the system file manager.

## What the rebase needed

Only 8 of the 22 commits touch Dolphin source; the rest are packaging and
docs and carry over untouched. Conflicts and their resolutions:

| file | what changed upstream | resolution |
|---|---|---|
| `kfileitemmodel.cpp` | include block reordered | keep both sets |
| `main.cpp` | `kdemain()` became `main()` | keep the Exxos helper, take upstream's entry point |
| `dolphincontextmenu.cpp` | rewritten with lambdas | the NET Exxos change was only three `"Open Path"` -> `"Open Containing Folder"` renames, so take upstream and rename rather than replay two commits |
| `dolphinitemlistview.h` | `viewmodesettings.h` include dropped | `ViewModeSettings::ViewMode` no longer exists and our `viewMode()` was never defined -- dead declaration, removed |
| `dolphinitemlistview.h` | 22.12 builds with `QT_NO_KEYWORDS` | `slots:` -> `Q_SLOTS:` |
| `placesitemlistwidget.{h,cpp}` | **deleted upstream** | see below |

**Do NOT cherry-pick the three release commits** (`588b432`, `637d142`,
`c1e348b`) onto this branch — they carry prebuilt 20.12.2 `.deb` blobs that
would not match the source. Regenerate with `make-deb.sh`.

## The Places panel had to be reimplemented

22.12 dropped Dolphin's own places widget for KIO's `KFilePlacesView`, whose
delegate is private. KIO uses `QPalette::Highlight` for BOTH the selected-row
background and the capacity bar fill, and Highlight is grey on this desktop,
so the bars came out grey-on-grey.

A `QProxyStyle` cannot help: the delegate calls `QApplication::style()` with no
widget argument, so there is nothing to discriminate on. The delegate is
wrapped instead (`placespanel.cpp`): paint the row background ourselves, clear
`Selected`/`MouseOver` so KIO does not paint over it, and hand it purple as
`Highlight` for the bar. Section headings are repainted at full `WindowText`
because KIO draws them as `mixedColor(WindowText, Window, 60)`.

**Not recovered:** the 10% icon darkening. KIO reads icons straight from the
model's `DecorationRole`, and a proxy model is unsafe — the delegate
`static_cast`s the model to `KFilePlacesModel`.

## New hook points added in this port

Beyond the original five, for anyone rebasing further:

* `KStandardItemListWidgetInformant::calculateIconsLayoutItemSizeHints()` and
  `…DetailsLayoutItemSizeHints()` — tiles must report their CELL height, not a
  caption height, or the tile paints outside its own bounds.
* `KStandardItemListWidget::exxosTileScale()` / `exxosTileFont()` — one scale
  for the whole tile.
* `KItemListGroupHeader::exxosScaledHeaderFont()` — must be used by all three
  of: the widget that draws the heading, `updateSize()` which sizes the rect it
  is drawn into, and `KItemListView::updateGroupHeaderHeight()`.
* `ZoomLevelInfo::minimumLevel()` — 32px floor.
* `DolphinItemListView::updateGridSize()` — repaints the container viewport
  after a grid change.
