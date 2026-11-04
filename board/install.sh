#!/bin/sh
set -eu
cd "$(dirname "$0")"
test "$(id -u)" = 0
test -f dual_console.bit || { echo "dual_console.bit is missing; build it with Vivado first (see README)" >&2; exit 1; }
sha256sum -c SHA256SUMS
mountpoint -q /sd
test -x /opt/a2-dual/start-lcdterm.sh
echo 'dfd845be56f9f233fa4a95571ea46e768d9eb64f4a58e123b7eff5eff7426cda  /sd/image.ub' | sha256sum -c -
baseline=fe57c1a6bdbc52be7346cba34a84eaa72b9239e4d364aff529b9124855cff09c
old=/sd/a2_fb43_top.bit.before-shared-console
if [ ! -f "$old" ]; then
    echo "$baseline  /sd/a2_fb43_top.bit" | sha256sum -c -
    cp /sd/a2_fb43_top.bit "$old"
    sync
fi
echo "$baseline  $old" | sha256sum -c -

if ! command -v tmux >/dev/null 2>&1; then
    test -f tmux_1.9-6_armhf.deb || { echo "tmux_1.9-6_armhf.deb is missing; download it from the Debian Jessie archive (see README)" >&2; exit 1; }
    dpkg -i tmux_1.9-6_armhf.deb
fi
test "$(tmux -V)" = 'tmux 1.9'
dir=/opt/shared-console
install -d "$dir" /etc/systemd/system/lcdterm.service.d /root/.config/autostart /root/.local/share/applications
for f in lcdterm active start-lcdterm shared-shell lcd-attach lcdterm-bash shared-console; do
    install -m 755 "$f" "$dir/$f.next"
    mv "$dir/$f.next" "$dir/$f"
done
install -m 644 tmux.conf "$dir/tmux.conf"
install -m 644 dual_console.bit "$dir/dual_console.bit"
tic -x shared-lcd.ti
install -m 644 shared-console.desktop /root/.config/autostart/shared-console.desktop
install -m 644 shared-console.desktop /root/.local/share/applications/shared-console.desktop
if [ ! -e /usr/local/bin/shared-console ]; then
    ln -s "$dir/shared-console" /usr/local/bin/shared-console
else
    test "$(readlink /usr/local/bin/shared-console)" = "$dir/shared-console"
fi

if ! grep -q '^# BEGIN SHARED CONSOLE$' /root/.profile; then
    if [ ! -e /root/.profile.before-shared-console ]; then cp -p /root/.profile /root/.profile.before-shared-console; fi
    cp -p /root/.profile /root/.profile.shared-next
    printf '\n' >> /root/.profile.shared-next
    cat serial-profile >> /root/.profile.shared-next
    sh -n /root/.profile.shared-next
    mv /root/.profile.shared-next /root/.profile
fi

drop=/etc/systemd/system/lcdterm.service.d/70-a2-codex.conf
install -m 644 lcdterm.conf "$drop.next"
mv "$drop.next" "$drop"
systemctl daemon-reload
cp dual_console.bit /sd/a2_fb43_top.bit.next
cmp dual_console.bit /sd/a2_fb43_top.bit.next
sync
mv /sd/a2_fb43_top.bit.next /sd/a2_fb43_top.bit
sync
sha256sum /sd/a2_fb43_top.bit
