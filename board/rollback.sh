#!/bin/sh
set -eu
test "$(id -u)" = 0
mountpoint -q /sd
old=/sd/a2_fb43_top.bit.before-shared-console
echo "fe57c1a6bdbc52be7346cba34a84eaa72b9239e4d364aff529b9124855cff09c  $old" | sha256sum -c -
cp "$old" /sd/a2_fb43_top.bit.next
cmp "$old" /sd/a2_fb43_top.bit.next
sync
mv /sd/a2_fb43_top.bit.next /sd/a2_fb43_top.bit
sync
echo "Reboot to restore the previous dual-screen version."
