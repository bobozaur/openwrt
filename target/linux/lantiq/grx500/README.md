# Archer AX50 v1

## Build

Use the `ax50-integration` branch and its committed `feeds.conf.default` for the
full feature image. The default feed file pins matching public packages, LuCI
and routing revisions. An existing local `feeds.conf` overrides that file;
retain any local work separately and use the committed inputs for this build.

Start with a fresh clone and the ordinary
[OpenWrt build prerequisites](https://openwrt.org/docs/guide-developer/toolchain/install-buildsystem).
No private firmware cache, source override, image overlay or setup wrapper is
required.

```sh
git clone --branch ax50-integration https://github.com/bobozaur/openwrt.git
cd openwrt
./scripts/feeds update -a
./scripts/feeds install -a
cp config/ax50-full.config .config
make defconfig
make download -j8
make -j8
```

The committed seed selects **Lantiq**, **GRX500**, and **TP-Link Archer AX50 v1
(full services)**. It also preserves patented media functionality while
leaving the optional FFmpeg x264 encoder disabled. Selecting the profile alone
in `menuconfig` sets its package list; the seed supplies these additional
build choices. Use ordinary `make menuconfig` to adjust the selection if needed.

The full profile includes LuCI with native wireless setup, WireGuard, OpenVPN,
strongSwan/IPsec, PPP variants, multi-WAN and policy routing, DDNS, UPnP,
multicast helpers, SQM, storage, Samba, DLNA and related UI packages. Selecting
software does not configure or enable every optional service. The ordinary
device profile remains available for a smaller image, and package selection
can be changed in `menuconfig`.

Production selection leaves kernel profiling and tracing disabled. The build
enforces both kernel-bank bounds and the smaller rootfs-bank bound; a successful
package build alone does not prove the complete image fits. Output is under
`bin/targets/lantiq/grx500/`.

## Firmware and first use

The `wave600-firmware` package downloads and verifies the pinned vendor archives
during the build. The resulting image already contains the complete original
radio firmware cohort. It can initialize the radios without Internet access,
an existing firmware volume or a manual importer. Per-device calibration is
read from the protected OEM partition. See the
[firmware package documentation](../../../../package/firmware/wave600-firmware/README.md)
for sources, verification, migration and the unresolved firmware permission
boundary. This source publication does not grant redistribution rights for
vendor firmware or authorize publication of binary build output.

First boot uses ordinary OpenWrt defaults: LAN management and an Ethernet WAN,
with wireless disabled until configured. Set an administrator password and
configure the regulatory country, networks and wireless roles through LuCI.
The image does not contain a saved SSID, wireless key, uplink, country, VLAN or
preferred interface name. Native AP, station, supported concurrency, WDS,
bridges and VLANs use the normal OpenWrt controls. A wireless uplink can be
configured without first obtaining firmware through that uplink.

## Installation and upgrades

The build produces the standard AX50 `sysupgrade.bin`. Normal OpenWrt upgrades
write the inactive kernel/rootfs bank and restore saved configuration to that
bank's overlay. The confirmed bank remains the recovery path until the new
boot is confirmed. Use LuCI's firmware upgrade page or ordinary `sysupgrade`;
do not use a generic raw-flash operation.

Initial installation uses the OEM U-Boot serial console and a native OpenWrt
embedded-initramfs image. The
[AX50 hardware page](https://openwrt.org/inbox/toh/tp-link/archer_ax50)
documents the serial header and 115200 8N1 console; it does not provide an
installation method for this port. OEM web recovery has not been established
as a way to load these OpenWrt images.

Save the full `.config` and copy the full `sysupgrade.bin` outside the build
output directory before rebuilding. Move the saved configuration aside so
`.config` is absent when starting ordinary `make menuconfig`. From that fresh
selection, choose **Lantiq**, **GRX500**, the smaller **TP-Link Archer AX50 v1**
device profile, and an embedded ramdisk under **Target Images**
(`CONFIG_TARGET_ROOTFS_INITRAMFS=y`,
`CONFIG_TARGET_ROOTFS_INITRAMFS_SEPARATE=n`). Switching profiles in the full
configuration retains explicit package selections and does not produce the
smaller recovery image. Build the freshly selected profile's
`openwrt-lantiq-grx500-tplink_archer-ax50-v1-initramfs-uImage`. Both compressed
and expanded size bounds are enforced; the full services profile is not a
recovery ramdisk. Use a 3.3 V TTL UART with TX/RX/GND and no VCC connection.
Interrupt the OEM autoboot countdown with Space. At the tested U-Boot prompt:

```text
loady 82000000
```

Send the initramfs uImage with the terminal's YMODEM transfer, then boot the
received RAM image:

```text
bootm 82000000
```

These commands load RAM and boot its uImage; they do not save the bootloader
environment or invoke vendor flash-update macros. In the resulting native
OpenWrt environment, transfer the full sysupgrade image to
`/tmp/openwrt-ax50-sysupgrade.bin` using normal OpenWrt file transfer. From its
console, validate and install through the platform owner:

```sh
sysupgrade -T /tmp/openwrt-ax50-sysupgrade.bin
sysupgrade -n /tmp/openwrt-ax50-sysupgrade.bin
```

The initial installer accepts only its recorded OEM boot environment and
five-volume UBI layout. A rejected layout/environment requires a separate
compatibility assessment; no forced or generic raw flash operation is supplied.
For accepted input it preserves OEM fallback, calibration, identity data and
existing volumes, creates only the two standard OpenWrt overlays, and installs
the first OpenWrt bank. The serial RAM load/boot mechanism and the platform
installer have distinct qualification scopes; an image without embedded
initramfs support is not an initial-install image.

Factory reset clears the selected OpenWrt overlay and user configuration;
packaged radio firmware stays in the read-only image. The former importer's
shared firmware volume is retained without mounting or modifying it. Firmware
and driver versions stay paired through ordinary complete-image upgrades.
