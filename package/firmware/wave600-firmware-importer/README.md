# WAVE600 firmware importer

On the Archer AX50 V1, run `wave600-firmware-import` as root to import the
pinned NETGEAR RAX40 V1 1.0.6.106 firmware with the AX50 V1 1.1.2 Build
20251022 PSD. The eight NETGEAR executable/PHY files form one fixed cohort;
NETGEAR PSD and calibration are not used. No firmware is shipped in this
package. Reboot after import to initialize the radios with the matching driver.

Without arguments, download the pinned NETGEAR input and, only if no verified
AX50 PSD is available locally, the pinned AX50 input. Local inputs never cause
a download. Supply the NETGEAR ZIP or raw image as the first argument; if no
verified PSD is available, supply the AX50 ZIP or raw image as the second:

```sh
wave600-firmware-import /path/to/RAX40-stock.zip /path/to/Archer-AX50-stock.zip
```

An existing verified PSD from the firmware store, an interrupted unpublished
stage, regular firmware files or the retained legacy directory is reused. The
AX50 temporary archive/image is released before downloading NETGEAR. Caller
inputs are never removed.

The importer creates only the named dynamic UBI volume `wave600_firmware`,
using 32 eraseblocks (4,063,232 bytes). It leaves existing volumes, bank
selection, calibration and settings untouched. This volume
is separate from the selected bank's resettable overlay and survives both
normal sysupgrade and factory reset.

The native `S09wave600-firmware` boot service mounts the store read-only at
`/mnt/wave600_firmware`, after overlay and upgrade-backup restoration but before
`S10boot` loads the radio modules. Exactly nine
links in `/lib/firmware/updates` use the kernel's native firmware search order
to read the pinned cohort. Package installation and early boot automatically
migrate complete verified current-cohort regular files from `/lib/firmware`;
the same offline operation is available as `wave600-firmware-import --migrate`.
Neither operation downloads or converts older AX50 binaries. A complete current
regular-file cohort can migrate over a recognized old store. Without one, that
store is retained but unmounted, with guidance to reimport explicitly. Matching
old regular files are moved individually into the non-search directory
`/lib/firmware/wave600-legacy`, so interrupted moves can resume without leaving
old files in the kernel's fallback search path. Unknown bytes are not moved or
deleted. Complete current-cohort regular files are accepted before this
withdrawal. Only matching legacy files are removed after the new persistent
copy is verified.
The service is enabled on package installation and upgrade, including an APK
installation on an existing image. Settings are never copied into the firmware
store.

Imports serialize through a temporary lock. Only an install or migration mounts
the volume writable, and it returns to read-only on exit. The importer verifies
and synchronizes the complete staged cohort before publishing it with one
directory rename. Interrupted unpublished staging is reclaimed on the next
import; an empty newly created volume is retained for retry. A verified
published cohort is reused without downloading or writing it again. A
published cohort which fails verification is withdrawn from the firmware search
path and reported rather than replaced. Explicit reimport can replace only the
recognized old AX50 cohort: all nine new files are assembled and verified in
RAM before any old file is retired. After copying its verified PSD to RAM,
the old store is unmounted during bulk NETGEAR acquisition; input failure leaves
the old data intact. The store is remounted only after the complete RAM cohort
passes verification. The old files then retire through
the unpublished staging path, without requiring space for two persistent
cohorts. Interruption after old retirement can leave no published firmware;
repeat the explicit import using retained inputs. A verified PSD surviving in
unpublished staging or the legacy directory can be reused, but RAM staging is
lost on reboot. No firmware is erased from other volumes. An older image using
the previous manifest will reject the new shared store; its radio firmware must
match its driver before use. Busy mounts are lazily detached; this
prevents new lookups but cannot revoke bytes already read by a running driver.
If withdrawal itself fails, release users of the store and unmount it before
loading radios.

## Recovery after an interrupted first filesystem format

Power loss while UBIFS first initializes an empty volume can leave it nonempty
but unmountable. Normal imports do not erase such a volume automatically.
Interrupted staging on a mountable filesystem is reclaimed by an ordinary
repeat import.

For an unmountable store, retain both supported vendor ZIPs or raw images before
proceeding: removing the store requires importing the NETGEAR cohort and AX50
PSD again. Stop the radios with `wifi down`, close any users of `/mnt/wave600_firmware`, and unmount
that path if mounted. A busy detached mount must also have released its users.
The following native procedure finds only the dedicated named store and
requires it to be unmounted before removal. Other volumes and bank selection
remain untouched:

```sh
. /lib/upgrade/nand.sh
ubidev=$(nand_find_ubi system_sw)
volume=$(nand_find_volume "$ubidev" wave600_firmware)
[ -n "$ubidev" ] && [ -n "$volume" ] &&
[ "$(cat "/sys/class/ubi/$volume/name")" = wave600_firmware ] &&
awk -v name="$ubidev:wave600_firmware" -v dev="/dev/$volume" '
    $1 == name || $1 == dev {used=1}
    END {exit used ? 1 : 0}' /proc/mounts &&
ubirmvol "/dev/$ubidev" -N wave600_firmware
```

After successful removal, reimport the retained vendor inputs and reboot:

```sh
wave600-firmware-import /path/to/RAX40-stock.zip /path/to/Archer-AX50-stock.zip && reboot
```
