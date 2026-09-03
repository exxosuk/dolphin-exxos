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

Build output (`out/`, `apt/`, `stage/`, `stage-icons/`, `build/`, `*.deb`,
`*.so`) is deliberately not tracked; it is large and reproducible.

Paths in these scripts use `$HOME` rather than a fixed home directory, and the
examples in comments use placeholder volume names.
