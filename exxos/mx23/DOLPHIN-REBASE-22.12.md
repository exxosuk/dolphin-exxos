# Rebasing the Exxos patches onto Dolphin 22.12.3

Prepared 2026-09-01 while the MX 23 upgrade ran. Companion to
`DOLPHIN-PATCHES.md` sections 3 and 6, which describe each hook point.

## The series is smaller than it looks

22 commits on `exxos/win7-tiles`, but **only 8 touch Dolphin source**. The other
14 are packaging, README, the APT repo under `docs/`, and release artefacts —
upstream never touches those paths, so they cannot conflict.

### The 8 that need care, in order

| commit | what it does | files |
|---|---|---|
| `5c8c03e` | Win7 tile rows + graphical capacity bar | `kfileitemmodel.cpp`, `kstandarditemlistwidget.{h,cpp}` |
| `73f1c23` | Windows size units, tighter tile spacing | `kstandarditemlistwidget.cpp` |
| `aac4961` | Exxos Edition branding | `main.cpp` |
| `fc24ed3` | Tile view in icons layout, multi-column grid | `kstandarditemlistwidget.cpp`, `dolphinitemlistview.{h,cpp}` |
| `636f5c5` | Align capacity-less items | `kitemliststyleoption.{h,cpp}`, `kstandarditemlistwidget.cpp`, `dolphinitemlistview.cpp` |
| `0ac9a17` | Open Containing Folder + default-FM prompt | `dolphincontextmenu.cpp`, `main.cpp` |
| `e4e2df0` | Mouse-over gets its own colour | `dolphincontextmenu.cpp`, `kitemlistwidget.cpp` |
| `ff54fbf` | Places panel hover/selection, capacity bars, darker icons | `kitemlistwidget.cpp`, `kstandarditemlistwidget.{h,cpp}`, `placesitemlistwidget.{h,cpp}` |

Nine source files in total. That is the whole conflict surface.

### The 14 that carry over untouched

`d94e317` (build scripts) · `c0ef144` (launcher) · `3d5b79d` (push helper) ·
`dd20e44` `5f6c932` `5f7814f` `af7d0db` (packaging + signing) ·
`0bcfff7` `53d5647` `e05d19f` `452b5af` (README + screenshot) ·
`588b432` `637d142` `c1e348b` (release artefacts)

The three release commits carry prebuilt `.deb` blobs for 20.12.2. Do **not**
cherry-pick those onto the new branch — regenerate with `make-deb.sh` once it
builds. Take `dd20e44`, `5f6c932`, `5f7814f`, `af7d0db` for the scripts.

## Recipe

```bash
cd theme-work/dolphin-src
git fetch --tags
git checkout -b exxos/win7-tiles-22.12 v22.12.3

# source, in order — stop and resolve as they come
git cherry-pick 5c8c03e 73f1c23 aac4961 fc24ed3 636f5c5 0ac9a17 e4e2df0 ff54fbf

# then the infrastructure, which should be silent
git cherry-pick d94e317 c0ef144 3d5b79d dd20e44 5f6c932 5f7814f af7d0db \
                0bcfff7 53d5647 e05d19f 452b5af
```

## Where the conflicts will be

`kstandarditemlistwidget.cpp` — five of the eight touch it, and it is the file
upstream changed most between 20.12 and 22.12. Expect conflicts in the hook
points, not in the added methods: `updateTilesLayoutTextCache()`,
`drawCapacityBar()`, `win7Size()` and `applyIconEffect()` are self-contained and
should apply whole.

`kitemlistwidget.cpp` — the direct hover paint. Small and localised.

`main.cpp` — 22.12 restructured argument handling, so `aac4961` and the
`exxosOfferToBecomeDefaultFileManager()` half of `0ac9a17` may need hand
placement rather than a clean apply.

## Before any of this

`resolve-deps.sh` has to have staged a **bookworm** build environment —
`rebuild-exxos.sh` stage 3 does that. Nothing here compiles until it has.
