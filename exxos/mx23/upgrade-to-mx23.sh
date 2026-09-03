#!/bin/bash
# MX 21.3 (bullseye) -> MX 23 (bookworm), in place.
#
#   tmux new -s upgrade
#   sudo ~/theme-work/mx23/upgrade-to-mx23.sh
#
# MUST run inside tmux: X and Plasma get restarted during this, and anything
# started from a terminal window dies with them. A dist-upgrade killed halfway
# leaves a broken system; tmux survives X.
set -o pipefail
LOG=/var/log/mx23-upgrade.log
STATE=/var/lib/mx23-upgrade
REAL_USER=chris
REAL_HOME=$(getent passwd "${SUDO_USER:-$USER}" | cut -d: -f6)

exec > >(tee -a "$LOG") 2>&1
say()  { echo; echo "=== $* ==="; }
die()  { echo; echo "!!! FAILED: $*"; echo "!!! log: $LOG"; exit 1; }
done_step() { touch "$STATE/$1"; }
todo()  { [ ! -e "$STATE/$1" ]; }

[ "$(id -u)" = 0 ] || die "run with sudo"
# sudo strips $TMUX from the environment, so that variable alone cannot answer
# this. Walk up the process tree instead and look for the tmux server, which is
# the parent of every pane's shell.
in_tmux() {
  [ -n "$TMUX" ] && return 0
  local pid=$PPID i
  for i in 1 2 3 4 5 6 7 8; do
    [ -n "$pid" ] && [ "$pid" -gt 1 ] 2>/dev/null || return 1
    case "$(ps -o comm= -p "$pid" 2>/dev/null)" in
      tmux*|screen*|*"tmux: server"*) return 0 ;;
    esac
    pid=$(ps -o ppid= -p "$pid" 2>/dev/null | tr -d ' ')
  done
  return 1
}
if [ "${ALLOW_NO_TMUX:-0}" = 1 ]; then
  echo "!!! ALLOW_NO_TMUX=1 — running outside tmux at your own risk."
elif ! in_tmux; then
  die "not inside tmux, and no tmux server found above this process.
     Run:  tmux new -s upgrade    then re-run this inside it.
     If you are certain you are safe from X restarting (a real TTY, say),
     override with:  sudo ALLOW_NO_TMUX=1 $0"
fi

mkdir -p "$STATE"
echo "MX 23 upgrade — started $(date)"

# ---------------------------------------------------------------- 1. snapshot
if todo 01-snapshot; then
  say "1/8  Snapshotting current state"
  D="$STATE/snapshot"; mkdir -p "$D"
  dpkg --get-selections            > "$D/selections.txt"
  apt-mark showmanual              > "$D/manual.txt"
  cp -a /etc/apt                     "$D/apt-etc"
  uname -r                         > "$D/kernel.txt"
  echo "  $(wc -l < "$D/selections.txt") packages recorded -> $D"
  done_step 01-snapshot
fi

# ------------------------------------------------- 2. move our things aside
# Both are built against KF5 5.78 and will be wrong on 5.103. The plasmoids
# shadow the system ones ENTIRELY, so a stale copy wins silently on the new
# Plasma. The KIO worker is a .so that the new Plasma would try to dlopen.
if todo 02-stand-down; then
  say "2/8  Standing down the Exxos customisations"
  if [ -d "$REAL_HOME/.local/share/plasma/plasmoids" ]; then
    mv "$REAL_HOME/.local/share/plasma/plasmoids" "$REAL_HOME/plasmoids-old-5.20"
    chown -h "$REAL_USER:$REAL_USER" "$REAL_HOME/plasmoids-old-5.20"
    echo "  plasmoid overrides -> ~/plasmoids-old-5.20"
  else
    echo "  no plasmoid overrides in place"
  fi
  for f in /usr/lib/x86_64-linux-gnu/qt5/plugins/kf5/kio/computer.so \
           /usr/share/kservices5/computer.protocol \
           "$REAL_HOME/.local/share/kservices5/computer.protocol"; do
    [ -e "$f" ] && { mv "$f" "$f.pre-mx23"; echo "  $f -> .pre-mx23"; }
  done
  done_step 02-stand-down
fi

# ------------------------------------------------ 3. finish on bullseye first
if todo 03-settle; then
  say "3/8  Settling bullseye (must be clean before changing release)"
  apt-get update                    || die "apt update on bullseye"
  apt-get -y upgrade                || die "apt upgrade on bullseye"
  apt-get -y --purge autoremove
  dpkg --audit
  [ -z "$(dpkg -l | awk '/^i[^i]/{print}')" ] || die "packages in a broken state — fix before continuing"
  echo "  bullseye is clean"
  done_step 03-settle
fi

# ----------------------------------------------------- 4. repoint the sources
if todo 04-sources; then
  say "4/8  Repointing sources at bookworm"
  # Only real .list files. apt ignores .save and ~ , and so do we.
  #
  # The backups go in a directory of their OWN, not beside the originals.
  # Leaving "debian.list.pre-mx23" in sources.list.d makes apt print
  #     N: Ignoring file 'debian.list.pre-mx23' ... invalid filename extension
  # on every single apt run, for ever, on a machine that has been upgraded.
  # apt only reads sources.list.d itself, so a sibling directory is invisible
  # to it and the pristine copies are still one ls away.
  mkdir -p /etc/apt/pre-mx23-sources
  for f in debian.list debian-stable-updates.list mx.list; do
    [ -e "/etc/apt/pre-mx23-sources/$f" ] || \
      cp -a "/etc/apt/sources.list.d/$f" "/etc/apt/pre-mx23-sources/$f"
    # An earlier run of this script left its backup in the wrong place.
    [ -e "/etc/apt/sources.list.d/$f.pre-mx23" ] && \
      mv "/etc/apt/sources.list.d/$f.pre-mx23" "/etc/apt/pre-mx23-sources/$f"
  done
  sed -i -E \
    -e 's/\bbullseye\b/bookworm/g' \
    /etc/apt/sources.list.d/debian.list \
    /etc/apt/sources.list.d/debian-stable-updates.list \
    /etc/apt/sources.list.d/mx.list
  # Debian 12 split firmware out of non-free.
  sed -i -E '/deb\.debian\.org|security\.debian\.org/ s/non-free$/non-free non-free-firmware/' \
    /etc/apt/sources.list.d/debian.list \
    /etc/apt/sources.list.d/debian-stable-updates.list
  # The running kernel is an MX AHS kernel, so AHS has to come along or it is
  # orphaned at its bullseye version.
  # mx.list ships the ahs line commented out; uncomment it rather than append.
  if ! grep -qE '^deb .* ahs' /etc/apt/sources.list.d/mx.list; then
    sed -i -E 's|^#\s*(deb .*/repo/ bookworm ahs)|\1|' /etc/apt/sources.list.d/mx.list
  fi
  grep -qE '^deb .* ahs' /etc/apt/sources.list.d/mx.list \
    && echo "  AHS enabled (the running kernel is an AHS kernel)" \
    || die "could not enable the AHS component in mx.list"

  echo "--- new sources ---"
  grep -rhvE '^\s*#|^\s*$' /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources | sed 's/^/  /'
  # MX 23 signs its repo with a different key than MX 21 (0D0D91C3655D0AF4).
  # The .deb beside this script was verified before staging: byte-identical from
  # mxrepo.com and the gb.mirrors.cicku.me mirror, and matching the SHA256 in
  # both of their Packages indexes.
  #   fpr 8AFEB908376620CCDBFBBB730D0D91C3655D0AF4
  #   uid MX-23 Repository (Repo signing key) <maintainer@mxrepo.com>
  KEYDEB="$(dirname "$0")/mx23-archive-keyring_2023.6.6_all.deb"
  if ! apt-key list 2>/dev/null | grep -q '0D0D 91C3 655D 0AF4' \
     && [ ! -e /etc/apt/trusted.gpg.d/mx-23-archive-keyring.asc ]; then
    [ -f "$KEYDEB" ] || die "MX-23 keyring .deb missing: $KEYDEB"
    echo "  installing the MX-23 archive keyring"
    sha256sum "$KEYDEB" | grep -q '^64fe94f8387c132be8ffef1f98c73403acb8d35716b37600465ecf8fa473a5f4 ' \
      || die "MX-23 keyring .deb does not match the verified checksum — do NOT continue"
    dpkg -i "$KEYDEB" || die "installing mx23-archive-keyring"
  else
    echo "  MX-23 keyring already present"
  fi
  apt-get update || die "apt update against bookworm — check the sources above"
  done_step 04-sources
fi

# ------------------------------------------------------- 5+6. the upgrade
# Debian's documented two-stage order. confdef+confold keeps our /etc as-is
# and takes the package default where we never had an opinion, so the whole
# run is unattended -- there is nothing of ours in /etc to lose.
export DEBIAN_FRONTEND=noninteractive
export APT_LISTCHANGES_FRONTEND=none
DPKGOPTS=(-o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold)

if todo 05-minimal; then
  say "5/8  Minimal upgrade (no new packages yet)"
  apt-get -y "${DPKGOPTS[@]}" upgrade --without-new-pkgs || die "minimal upgrade"
  done_step 05-minimal
fi

if todo 06-full; then
  say "6/8  Full upgrade — this is the long one"
  apt-get -y "${DPKGOPTS[@]}" full-upgrade || die "full-upgrade"
  done_step 06-full
fi

# ------------------------------------------------------------- 7. kernel
if todo 07-kernel; then
  say "7/8  Making sure a bookworm kernel is installed"
  if ! dpkg -l | grep -qE '^ii +linux-image-6\.(1|[2-9])'; then
    apt-get -y "${DPKGOPTS[@]}" install linux-image-amd64 || echo "  (could not install linux-image-amd64 — check manually)"
  fi
  dpkg -l | grep -E '^ii +linux-image' | awk '{print "  "$2"  "$3}'
  update-grub || echo "  (update-grub failed — check before rebooting)"
  done_step 07-kernel
fi

# ------------------------------------------------------------- 8. tidy
if todo 08-tidy; then
  say "8/8  Tidying"
  apt-get -y --purge autoremove
  apt-get clean
  dpkg --audit
  done_step 08-tidy
fi

say "DONE"
echo "  Debian is now: $(cat /etc/debian_version)"
echo "  log: $LOG"
cat <<'EOF'

  Reboot:   sudo reboot

  Then, logged in as chris and NOT with sudo:
      ~/theme-work/mx23/rebuild-exxos.sh
EOF
