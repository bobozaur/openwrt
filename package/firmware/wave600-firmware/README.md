# WAVE600 firmware

The normal OpenWrt download and package lifecycle extracts one pinned cohort
from the public NETGEAR RAX40 V1 1.0.6.106 and TP-Link Archer AX50 V1 1.1.2
Build 20251022 archives. Archive hashes, extracted image hashes, fixed SquashFS
offsets and the nine extracted file hashes are checked during the build. The
package uses the ordinary host `unzip` prerequisite and OpenWrt's host
`unsquashfs4`, including its XZ decoder. Target extraction tools are unnecessary.

The eight NETGEAR executable/PHY files stay together. Only `PSD.bin` comes from
the AX50 image. NETGEAR calibration and PSD are never used. Per-device AX50
calibration and Ethernet PHY firmware continue to come from the protected OEM
partitions through `ax50-caldata`; this package never writes those partitions.

The original files are installed directly in `/lib/firmware`, so ordinary
module loading works without network access, a writable overlay, a preseeded
volume or a user-run importer. Factory reset uses the same image files. A
settings-preserving sysupgrade installs the new image's cohort with its matching
driver. The focused WDS/BlockAck branch applies its pinned instruction overlay
in the native loader; this package does not modify the stored AP image.

On upgrade from the old importer, an early preinit hook runs after overlay and
backup restoration and before radio module loading. It removes only the nine
recognized importer symlinks from `/lib/firmware/updates`, disables the old
service's enable links, and retires exact old package scripts. Customized
scripts are preserved disabled. The old `wave600_firmware` UBI volume and its
contents are left intact and unmounted for fallback compatibility. Normal
sysupgrade preserves configuration rather than firmware overrides; explicit
user firmware overrides remain the administrator's responsibility.

## Firmware terms

The recipe and migration source are GPL-2.0-only. They do not grant a license to
the vendor firmware. `LicenseRef-NETGEAR-RAX40-Firmware` and
`LicenseRef-TP-Link-Archer-AX50-Firmware` identify unresolved firmware terms,
without asserting modification or redistribution rights.

The [NETGEAR release page](https://kb.netgear.com/000066105/RAX40-Firmware-Version-1-0-6-106)
and [TP-Link release page](https://www.tp-link.com/us/support/download/archer-ax50/v1/)
provide the pinned originals. Public download availability is not a grant to
redistribute them. NETGEAR's [terms, section 18](https://www.netgear.com/about/terms-and-conditions/)
restrict copying and modification, subject to written permission, applicable
law and the separate licenses of open source components. A radio-specific
modification or redistribution grant has not been established here.

Publishing this source recipe is separate from publishing vendor archives,
extracted firmware packages, images or firmware caches. Keep that boundary
when distributing build output. The packaged original firmware also needs
applicable distribution rights; this boundary is not limited to the focused
instruction modification. The focused firmware correction remains
outside an official OpenWrt submission until its applicable modification and
redistribution permissions are resolved.
