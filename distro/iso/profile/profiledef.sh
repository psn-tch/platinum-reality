#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="platinum-reality"
iso_label="ARCH_$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y%m)"
iso_publisher="Platinum Reality experimental project"
iso_application="Platinum Reality Live Desktop (VM prototype)"
iso_version="$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y.%m.%d)"
install_dir="arch"
bootmodes=('bios.syslinux'
           'uefi.grub')
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'xz' '-b' '1M' '-processors' '4')
bootstrap_tarball_compression=('zstd' '-c' '-T0' '--auto-threads=logical' '--long' '-19')
kernel_params_aarch64="clk_ignore_unused pd_ignore_unused arm64.nopauth"
file_permissions=(
  ["/home/liveuser"]="1000:1000:750"
  ["/home/liveuser/Desktop"]="1000:1000:755"
  ["/home/liveuser/Desktop/Install Platinum Reality.desktop"]="1000:1000:755"
  ["/usr/bin/platinum-reality-install"]="0:0:755"
  ["/usr/lib/platinum-reality/installer-engine"]="0:0:644"
  ["/home/liveuser/.profile"]="1000:1000:644"
  ["/etc/sudoers.d/10-platinum-live"]="0:0:440"
  ["/etc/shadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/.automated_script.sh"]="0:0:755"
  ["/root/.gnupg"]="0:0:700"
  ["/usr/local/bin/choose-mirror"]="0:0:755"
  ["/usr/local/bin/Installation_guide"]="0:0:755"
  ["/usr/local/bin/livecd-sound"]="0:0:755"
)
