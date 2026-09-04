# Exxos sources

Everything that makes the Exxos desktop, other than the patched Dolphin in
`../src`. Kept here so this repository alone can rebuild the packages.

| | |
|---|---|
| `kio-computer/` | the `computer:/` KIO worker - the drive view Plasma does not have |
| `packaging/` | builds `exxos-desktop` and `exxos-icons`, and publishes the apt repository |
| `system-tools/` | udev rule that makes the kernel poll drives which cannot report media changes |
| `mx23/` | the MX 23 upgrade and rebuild scripts |
| `docs/` | patch notes, upgrade notes and the open-issue list |
| `deploy.sh` | build and install everything into `$HOME`, without root |
| `bump-version.sh` | the single source of the version number - `--patch` or `--minor`, never bare |
| `sync-repo.sh` | keep a machine's working directory and this checkout in step, both ways |

Build output (`out/`, `apt/`, `stage/`, `stage-icons/`, `build/`, `*.deb`,
`*.so`) is deliberately not tracked; it is large and reproducible.

Paths in these scripts use `$HOME` rather than a fixed home directory, and the
examples in comments use placeholder volume names.

## Two machines

The work is done in a working directory (`theme-work/`) that is not a checkout
and does not have this repository's shape: `theme-work/dolphin-src/src/` is
`src/` here, and `theme-work/*.md` are `docs/*.md`. Nothing joined the two, so
they drifted -- on 4 September 2026 one machine held four unpushed versions of
Dolphin while the repository held theme work that machine had never seen, and
neither side was a superset of the other.

`sync-repo.sh` answers "what has moved, and which way":

    exxos/sync-repo.sh              # report drift, change nothing
    exxos/sync-repo.sh --to-repo    # working dir -> checkout, ready to commit
    exxos/sync-repo.sh --from-repo  # checkout -> working dir, after a pull

Set `EXXOS_WORK` if the working directory is not `~/claude/theme-work`.

Run the check **before** starting work and **after** finishing it. If both
sides have moved, it says so rather than picking a winner -- merge by hand, and
take the code from whichever side actually ran it.

`--to-repo` refuses to copy anything if it finds real volume labels, mount
points or home paths in the files it is about to publish. That scrub has been
missed by hand twice. `THEME-LOG.md` is never copied: it is a record of a real
desktop and belongs on the machine and the NAS, not here.
