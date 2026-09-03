#!/bin/bash
# Build the out-of-tree kernel modules for the NEW kernel before rebooting.
#
#   sudo ~/theme-work/mx23/fix-dkms-newkernel.sh
#
# Why this is needed: MX diverts /usr/sbin/dkms to a wrapper from buildiso-mx
# that execs /usr/sbin/dkms.mx. Debian's kernel hooks call
# "dkms kernel_preinst -k <ver>", an action dkms.mx does not know, so it printed
# its usage and did nothing. The new kernel therefore has NO dkms modules --
# and that includes the NVIDIA driver and every wifi driver on this machine.
set -o pipefail
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }

RUNNING="$(uname -r)"
# newest installed kernel image that is not the one we are running
NEW="$(ls -1 /lib/modules | grep -v "^$RUNNING\$" | sort -V | tail -1)"
[ -n "$NEW" ] || { echo "could not find a new kernel in /lib/modules"; exit 1; }

echo "=== running kernel : $RUNNING"
echo "=== target kernel  : $NEW"
echo

count() { ls "/lib/modules/$1/updates/dkms/" 2>/dev/null | wc -l; }
echo "modules now:  $RUNNING = $(count "$RUNNING")   $NEW = $(count "$NEW")"

echo
echo "=== 1/4  Kernel headers for $NEW ==="
if [ -d "/usr/src/linux-headers-$NEW" ]; then
  echo "  already present"
else
  apt-get -y install "linux-headers-$NEW" || { echo "!!! could not install headers — nothing can build without them"; exit 1; }
fi

echo
echo "=== 2/4  Building the modules ==="
# autoinstall IS an action dkms.mx understands, so the MX wrapper is fine here;
# it is only the kernel_preinst/postinst hook actions that it chokes on.
if ! dkms autoinstall -k "$NEW"; then
  echo
  echo "  MX's dkms failed. Falling back to Debian's own dkms 3.2.0,"
  echo "  which mx-system diverted to /usr/sbin/dkms.dpkg-dist."
  [ -x /usr/sbin/dkms.dpkg-dist ] && /usr/sbin/dkms.dpkg-dist autoinstall -k "$NEW"
fi

echo
echo "=== 3/4  Checking what actually got built ==="
BEFORE=$(count "$RUNNING"); AFTER=$(count "$NEW")
echo "  $RUNNING : $BEFORE modules"
echo "  $NEW : $AFTER modules"
ls "/lib/modules/$NEW/updates/dkms/" 2>/dev/null | sed 's/^/     /'
echo
MISSING=""
for m in $(ls "/lib/modules/$RUNNING/updates/dkms/" 2>/dev/null); do
  [ -e "/lib/modules/$NEW/updates/dkms/$m" ] || MISSING="$MISSING $m"
done
if [ -n "$MISSING" ]; then
  echo "  !!! MISSING on the new kernel:$MISSING"
  echo "  !!! nvidia-* = no graphics driver.  8812au/8821cu/rtl8821ce/wl = no wifi."
  echo "  !!! If these matter, boot the OLD kernel ($RUNNING) from the GRUB menu"
  echo "  !!! -- it still has all of its modules -- and sort this out from there."
else
  echo "  every module present on $RUNNING was rebuilt for $NEW"
fi

echo
echo "=== 4/4  initramfs and grub ==="
update-initramfs -u -k "$NEW" || echo "  (update-initramfs reported a problem)"
update-grub                   || echo "  (update-grub reported a problem)"

echo
if [ -z "$MISSING" ]; then
  echo "OK to reboot. Both kernels are in the GRUB menu; $RUNNING stays as a fallback."
else
  echo "Do NOT reboot into $NEW blind. Either fix the modules above, or pick"
  echo "$RUNNING from the GRUB menu at boot."
fi
