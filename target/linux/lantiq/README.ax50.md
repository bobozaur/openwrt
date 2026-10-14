# TP-Link Archer AX50 v1

This target implements the GRX500 port for `tplink,archer-ax50-v1`, using the
existing bootloader, NAND partitioning and two firmware banks. It is a development
port. The behavior below describes the source; a successful build or confirmed
boot does not qualify every peripheral, wireless mode or recovery scenario.

## Build and package prerequisites

Use the build-host prerequisites in the [OpenWrt README](../../../README.md).
Select **Lantiq → GRX500 → TP-Link Archer AX50 v1** in `make menuconfig`.
The [image recipe](image/grx500.mk) builds one SquashFS sysupgrade image containing
both bank-specific kernel wrappers and a shared root filesystem. Selecting an
embedded initramfs also builds the dedicated `-initramfs-uImage` RAM installer.
GRX500 uses its own EVA kernel layout; other Lantiq subtarget images are
incompatible.

Three matching local inputs are required for the default radio packages:

* The WAVE GPL driver source archive and separate RF-library source archive named
  by [package/kernel/wave/Makefile](../../../package/kernel/wave/Makefile).
* The radio-firmware archive named by
  [package/firmware/wave600-firmware/Makefile](../../../package/firmware/wave600-firmware/Makefile).
  Its description specifies the deterministic archive format; `SHA256SUMS` lists
  the required original files. The package applies its firmware corrections
  during the build.

Place these inputs in the configured download directory, normally `dl/`. The
recipes check their hashes and deliberately do not fetch missing restricted
inputs from public mirrors. Possession of the archives does not establish
permission to distribute the RF library, resulting module, firmware or images
containing them. Their distribution terms remain unresolved.

The board profile selects the Ethernet/DSA modules, factory-data readers, WAVE
driver and configuration integration, `wpad-mbedtls`, NAND environment/UBI
tools, USB support and basic LED owners. Keep these dependencies together. GPHY
firmware and radio calibration are extracted from the router's protected factory
partitions into RAM; they are not interchangeable downloads or configuration
backup files.

Use the [AX50 feed preset](../../../feeds.conf.ax50) for matching packages, LuCI
and routing revisions. For a fresh checkout, initialize the feeds before selecting
packages:

```sh
cp feeds.conf.ax50 feeds.conf
./scripts/feeds update -a
./scripts/feeds install -a
```

Preserve local changes in existing feed checkouts before replacing them. For
already populated checkouts at the matching revisions, rebuild the normal feed
index and package links:

```sh
./scripts/feeds update -i packages luci
./scripts/feeds install -a
make menuconfig
make defconfig
make
```

`update -i` rebuilds indexes without updating feed repositories. Installing feed
links makes packages selectable; it does not add every application to the image.
Select only the services required for the deployment. In particular:

| Optional function | Packages and configuration owner |
| --- | --- |
| Outgoing L2TP | `xl2tpd`, `luci-proto-ppp`; netifd `network` interfaces |
| Managed L2TP servers | `luci-app-xl2tpd`; existing `xl2tp` UCI configuration |
| IPsec configuration/status | `luci-app-strongswan-swanctl`; pulls `strongswan-swanctl`, `swanmon` and its `davici` dependency; existing `ipsec` UCI configuration |
| XFRM interfaces | `luci-proto-xfrm` and its `xfrm`/kernel dependencies, with matching IPsec kernel support |

IPsec transforms, crypto modules and kernel packages must match the selected
kernel build. Installing a userspace VPN application does not add missing kernel
capabilities. Service startup, credentials, WAN admission and firewall forwarding
remain administrator configuration. L2TP itself provides no traffic encryption.
See the [L2TP backend guide](../../../feeds/packages/net/xl2tpd/README.md) and
[swanctl application guide](../../../feeds/luci/applications/luci-app-strongswan-swanctl/README.md).

The UCI transaction changes require a matching libuci consumer cohort, including
rpcd. Rebuild dependent packages when its ABI changes; see
[UCI transactions](../../../package/system/uci/README.transactions.md). Do not
combine kernel modules or ABI-dependent userspace packages from unrelated builds.

Receive scheduling uses threaded NAPI for the GRX500 DMA pollers and the native
`thread_backlog_napi` boot policy for per-CPU receive backlogs. The
`packet_steering` service discovers the registered netdev NAPI workers and treats
the independent GRX500 DMA netdevs as separate groups under the standard
`network.globals.packet_steering` policy. Existing
IRQ placement and WireGuard workqueue ownership remain unchanged. DSA GRO cells
and transmit softirqs retain their native kernel owners. This scheduling policy
does not establish a throughput or latency guarantee under competing traffic.

Optional performance events use the standard OpenWrt `KERNEL_PERF_EVENTS` option
and `perf` package. The target currently selects software events only; hardware
PMU interrupt ownership is not qualified. Kernel samples and callchains use the
matching kernel and module symbols. MIPS userspace callchains are not supported
by this kernel implementation. Profiling tools are optional and are not selected
by the board profile; the normal kernel and root-filesystem size limits still
apply when enabling them.

## Initial installation and storage contract

Initial installation uses the native embedded-initramfs image and the ordinary
board sysupgrade image. There is no OEM web-upgrade/factory wrapper. This path
supports the recorded AX50 v1 U-Boot `2010.06-dirty-LANTIQ-v-2.3.149` command
bodies and the storage layout below; it does not migrate arbitrary OEM layouts.
An independently established bidirectional serial console, off-device recovery
backups and a bootable OEM bank A are prerequisites.

Preserve the bootloader, both redundant environment partitions, GPHY firmware,
radio calibration, manufacturing data and a known working fallback bank.
The installer preserves bank A's kernel and root filesystem and verifies their
logical-volume hashes, together with `data_vol`, before trial selection. It
requires free UBI capacity without erasing or shrinking existing firmware/OEM
volumes. This guide supplies no raw-NAND formatting recipe.

The [layout validator](grx500/base-files/lib/functions/ax50.sh) requires `ubi0`
on the `system_sw` partition (`mtd5`), dynamic volumes with 126976-byte usable
eraseblocks, and these exact assignments:

| UBI ID | Name | Reserved eraseblocks |
| --- | --- | ---: |
| 0 | `kernelA` | 25 |
| 1 | `rootfsA` | 215 |
| 2 | `kernelB` | 25 |
| 3 | `rootfsB` | 207 |
| 4 | `data_vol` | 166 |
| 6 | `ax50_overlayA` | 128 |
| 7 | `ax50_overlayB` | 128 |

An initial installation requires exactly the first five volumes, stable-A
selector `0`, at least 256 currently available eraseblocks, and no additional
volumes. It creates only the two new dynamic
128-eraseblock overlays. Subsequent upgrades require all seven listed volumes.
Additional OEM volumes are preserved by rejecting initial provisioning; ordinary
standard upgrades leave them untouched. A different layout is a failed
prerequisite, not an instruction to recreate it.
The [DTS](dts/grx500_tplink_archer-ax50-v1.dtsi) marks the bootloader, GPHY,
calibration and reserved flash partitions read-only. `data_vol` supplies the
factory MAC through a temporary read-only mount.

Normal upgrades use `ax50_check_bootenv` to compare `bootcmd` and `check_image0`
through `check_image3` with the expected dispatch strings. Initial installation
additionally compares the recorded `ubi_init`, `flash_flash`, `flashargs`,
`addmisc`, `run_bootcore` and `switchbankA/B` command bodies exactly. A different
bootloader contract requires separate qualification; do not edit the environment
just to bypass this check. Normal alternate-bank recovery still requires
independently established, intact bank-switch command bodies.
[uboot-envtools](../../../package/boot/uboot-tools/uboot-envtools/files/lantiq)
describes its redundant environment storage. A default, invalid or differently
configured environment must be resolved through the established recovery path;
forcing an upgrade does not repair that prerequisite.

Build the installer with **ramdisk** selected under **Target Images**, using an
embedded initramfs rather than a separate cpio archive. Keep the normal board
storage, environment, factory-data and Ethernet dependencies. The RAM wrapper
derives its entry from the matching initramfs ELF, including per-device builds.
The expanded kernel, embedded userspace and DT must remain below the compressed
staging address; an oversized configuration fails packaging. Reduce optional
packages for the installer instead of changing the established addresses.

At the verified bootloader prompt, `loady 0x82000000` receives this image through
the bootloader's native YMODEM operation. Verify the complete transferred image
against the build artifact using the available bootloader integrity operation
before `bootm 0x82000000`. Do not save an alternative boot command or invoke the
OEM firmware-update macros. The dedicated RAM DT has no bank root or persistent
overlay selector; native `/init` uses tmpfs. Boot confirmation recognizes only
this explicit RAM mode and makes no environment update.

After confirming the expected board, RAM root, intact fallback and recovery
access, copy the matching normal sysupgrade image into RAM, verify its published
checksum, and use the existing native entrypoint:

```sh
sysupgrade -T /tmp/ax50-sysupgrade.bin
sysupgrade -n /tmp/ax50-sysupgrade.bin
```

The RAM-only branch rejects configuration backups. It creates the two overlays,
writes and reads back only `rootfsB` and `kernelB`, creates native UBIFS only on
the new B overlay, rechecks preserved data, then submits B trial last. The first
successful standard B boot uses the normal strict root/overlay/factory-MAC
confirmation. No OEM settings are imported.

Before selector submission, ordinary failure cleanup can remove only the exact
new overlay volumes whose creation this invocation acknowledged. Once selector
submission begins, the candidate is retained even if its acknowledgement is
lost. A power interruption or unacknowledged volume creation can leave a partial
layout with A still selected; a later attempt fails closed on existing overlays.
Use the retained recovery backups and inspect that state explicitly rather than
deleting matching names or rerunning provisioning blindly. An already provisioned
standard router is not a pristine-OEM installer target.

## Normal operation

The board defaults place `lan1` through `lan4` in LAN and use `wan` as the uplink.
Configure network interfaces, bridge VLANs and routes through UCI/netifd; DSA
owns the physical LAN ports. The factory MAC identifies LAN and the next MAC
identifies WAN. Missing factory identity prevents trial boot confirmation.

Configure radios through `/etc/config/wireless` and the WAVE netifd integration.
Hostapd and wpa_supplicant own authentication and station state. Use the configured
country and capabilities actually exposed by the radio; an accepted firmware
command alone does not qualify the associated wireless feature. The
[radio contracts](../../../package/kernel/wave/README.radio-contracts.md) and
[forwarding contracts](../../../package/kernel/wave/README.forwarding.md) describe
the supported boundaries and remaining limitations.

Firewall4/nftables owns filtering, NAT and forwarding policy. Native GRX500
[routed/NAT assistance](README.grx500-flow-offload.md) supports plain IPv4/IPv6
TCP/UDP routing and single-sided address/port translation through the native
flowtable. Completed packets return through the CPU for current Linux egress
and bridge policy. Unsupported actions remain in software; enabling offload
does not establish admission of a particular flow or imply CPU bypass or line
rate. VLAN-aware bridges use Linux software
forwarding because switch tag reconstruction clears the ingress C-tag DEI bit.
This preserves the original tag for Linux VLAN filtering but costs CPU time and
does not provide VLAN-aware bridge hardware acceleration. VLAN-unaware bridges
retain their supported hardware forwarding. A bridge that changes VLAN mode
stays in software until its ports leave and rejoin a supported bridge.

Procd owns service startup. Block/fstools owns storage and the selected writable
overlay. The LED configuration is under `/etc/config/system`; the Internet
indicator uses the configured `pingcheck` uplink probes rather than Ethernet
carrier alone. Keep local addresses, credentials, uplink choices and optional
services in configuration instead of changing the generic board profile.

Scheduled reboot uses the standard root crontab, available in LuCI under
**System → Scheduled Tasks**, with `/sbin/reboot` as the scheduled command.
The separate Wi-Fi Schedule application owns radio windows, not reboot policy.
Cron must be enabled for schedules to resume on boot. The crontab is included
in normal configuration backup; a configuration reset removes customized jobs.

Management access is controlled by the selected rpcd account ACLs, Dropbear
authentication/listener settings, uHTTPd listener settings and firewall input
policy. A restricted Scheduled Tasks writer needs the cron ACL's exact reload
command and its `file.exec` transport permission. Read-only views retain their
read access and cannot save schedules or request reboot. Scheduling writes root
commands, so grant that capability only to accounts intended to administer them.

## Qualification boundaries

The port remains under development. Configuration options, advertised radio
capabilities and successful short transfers establish different properties.
The following boundaries apply when selecting a deployment or counterpart:

| Area | Current boundary |
| --- | --- |
| Ethernet | VLAN-aware bridges use the software path described above. Full physical-port VLAN/STP coverage, LAN3/LAN4 aggregation and aggregate switch capacity remain unqualified. Supported routed/NAT assistance retains current Linux egress; unsupported flow actions remain in software. |
| Native wireless roles | Ordinary AP, routed three-address STA and concurrent AP/STA paths exist. Native STA four-address forwarding remains unresolved. Generic late four-address/AP_VLAN transitions and child group-key ownership are incomplete; use the explicit supported modes in the forwarding guide. |
| Channel transitions | Native AP/STA following must be established from an actual upstream CSA, matching radio state and downstream traffic. A restart/reassociation or same-channel countdown does not qualify a channel/width change. Mixed-role DFS/CAC and real radar behavior require a lawful capable upstream and appropriate equipment. |
| Advanced radio features | Native HE160, AP SU beamforming and exclusive monitor reception retain their qualified scopes. Monitor injection, forced STBC, TWT, DCM, ER, 80+80, mesh and IBSS are unavailable or unqualified and are not exposed as supported features. Multi-user configuration alone does not qualify OFDMA air behavior. See the radio guide for directional and lifecycle limits. |
| Radio controls and telemetry | Native AP/STA byte RTS and standard FIXED DATA power retain their qualified policy and lifecycle contracts with the matching firmware fixes. Fragmentation and per-packet DATA ACK/retry status remain unsupported. Linux counters, configured power and interrupt timing are not measured RF power or over-air ACK/retry evidence. |
| Performance and security lifetime | Short payload, authentication and key-replacement checks do not establish sustained combined-load throughput, latency, loss or rekey stability. Concurrent forwarding and VPN traffic can have very different service rates; link speed is not a routing-capacity claim. |
| Storage and services | Permissions and persistence depend on the actual filesystem, mount options and physical medium. Journaled HFS+ remains read-only. Local DDNS/IPTV fixtures do not establish real provider authentication or playback. |
| Boot and recovery | Warm startup and service restarts do not qualify cold/factory-default LAN recovery, cold-power persistence or generic first-install/ordinary-upgrade recovery. Preserve the recovery prerequisites below. |

The selected rtw88 USB AP driver does not provide the beacon-count/TBTT
completion path needed for AP CSA. A generic hostapd `switch_chan` method does
not add that driver capability; its forced restart fallback is not a CSA test.
An independently capable upstream can qualify the native radio's following
behavior without changing this USB-driver boundary.

Use the linked radio and forwarding guides for the owning interface and its
explicit unsupported results. Final source/package review and representative integrated qualification
remain necessary before treating this development port as a completed release.

## Backup, upgrade and confirmation

From a running standard installation, inspect bank identity and prerequisites:

```sh
. /lib/functions/ax50.sh
ax50_running_bank
ax50_check_layout && ax50_check_bootenv && ax50_check_lower_root
fw_printenv -n update_chk
```

The booted bank comes from the appended device tree's root and overlay arguments.
`update_chk` selects the next boot and cannot identify the currently running bank:
the bootloader consumes a trial before Linux starts.

| `update_chk` | Next boot | Selector retained before entering Linux |
| --- | --- | --- |
| 0 | Stable A | 0 |
| 1 | Trial B | 0, allowing fallback to A |
| 2 | Stable B | 2 |
| 3 | Trial A | 2, allowing fallback to B |

Create a configuration backup and copy it off the router before upgrading:

```sh
sysupgrade -l
sysupgrade -k -b /tmp/ax50-config.tar.gz
```

The archive contains the selected configuration files and an installed-package
list, not package binaries, both firmware banks, or factory partitions. Store
hardware-recovery backups separately and protect configuration archives containing
credentials. Keep custom configuration paths in `/etc/sysupgrade.conf` as needed.

Copy the intended board sysupgrade image to `/tmp/ax50-sysupgrade.bin`, verify it
against its build's published checksum, and use the standard command:

```sh
sysupgrade -T /tmp/ax50-sysupgrade.bin
sysupgrade /tmp/ax50-sysupgrade.bin
```

Proceed to the second command only if the first succeeds. `-T` checks the image
without flashing; it does not prove runtime behavior. The default upgrade saves
configuration. `sysupgrade -n /tmp/ax50-sysupgrade.bin` instead starts the target
bank with a fresh standard overlay and requires reconfiguration.

The [upgrade implementation](grx500/base-files/lib/upgrade/platform.sh) requires
an already confirmed running bank. It checks the image members, board metadata,
sizes, magic values, layout and boot environment. It writes and reads back only
the inactive root filesystem and matching kernel, empties only that bank's
standard overlay, installs any configuration backup there, and requests a trial
boot last. It preserves the active bank and shared factory data. Use this path
instead of invoking its stage-2 functions directly.

[Boot confirmation](grx500/base-files/sbin/ax50-boot-success) runs once after procd
reaches its running state. A trial requires the correct SquashFS lower root,
writable bank-specific UBIFS overlay, expected layout/environment and matching
factory identity before it becomes stable. Already confirmed banks retain normal
extroot and RAM-overlay recovery behavior. A trial using those alternate writable
roots is not accepted as a normal standard installation.

The DTS requests a 120-second userspace startup deadline. Failure to reach
procd's running state requests reboot; failed confirmation also requests reboot.
This does not guarantee recovery from a kernel hang or verify networking and
radio functionality. After reconnection, inspect the running bank, selector,
mounts and logs, then verify the required services before accepting the deployment.

## Recovery and reset

A failed trial leaves the previous stable bank as the next-boot fallback until
confirmation changes the selector. A power cycle or reset may be needed when
the failed system cannot reboot itself. Recovery depends on the preserved
bootloader, environment and fallback contents; a successful boot is not proof
that another bank contains a bootable image.

For an intentional trial of a verified alternate **standard** bank from a
working standard installation, use `ax50_select_bank BANK trial` from
`/lib/functions/ax50.sh`, replacing `BANK` with `A` or `B`, then reboot only if
the helper succeeds. This changes selection, not firmware contents. The helper
checks the dispatch strings described above and verifies its selector update;
it does not validate the bank-switch command bodies or alternate image. Establish
both independently before use. It is not an OEM recovery installer. Do not force
stable selection to bypass trial checks.

The serial console is `ttyLTQ0` at 115200 baud, 8N1. The target prepares GPHY
firmware before ordinary wired failsafe networking. Use the standard OpenWrt
failsafe/configuration-repair tools when the kernel can start; this target does
not supply a universal bootloader flashing command or board pinout.

For the normal internal overlay, `factoryreset` and the reset-button handler
reset the current bank's settings through fstools. They do not reinstall OEM
firmware or replace calibration. The source reset handler reboots after a press
shorter than one second and requests factory reset after at least five seconds
when an overlay is mounted. Treat factory reset as configuration loss and retain
an off-device backup first.

To restore a configuration archive to a compatible running installation, use
`sysupgrade -r /tmp/ax50-config.tar.gz` and restart the affected services or reboot.
This restores configuration in the running system; it does not switch banks or
install missing packages. Install a coherent package set through the normal
package manager or image build. Physical recovery, full peripheral reliability
and complete end-to-end qualification remain separate from these source contracts.
