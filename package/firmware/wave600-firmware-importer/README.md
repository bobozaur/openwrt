# WAVE600 firmware importer

On the Archer AX50 V1, run `wave600-firmware-import` as root to download and
import the pinned official 1.1.2 Build 20251022 release. Pass its local ZIP or
raw image as the sole argument for an offline import. No firmware is shipped
in this package. Reboot after the first import to initialize the radios.

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
migrate complete verified regular-file imports from `/lib/firmware`; the same
local operation is available as `wave600-firmware-import --migrate`. Only
matching legacy files are removed after the persistent copy is verified.
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
path and reported rather than replaced. Busy mounts are lazily detached; this
prevents new lookups but cannot revoke bytes already read by a running driver.
If withdrawal itself fails, release users of the store and unmount it before
loading radios.

## Recovery after an interrupted first filesystem format

Power loss while UBIFS first initializes an empty volume can leave it nonempty
but unmountable. Normal imports do not erase such a volume automatically.
Interrupted staging on a mountable filesystem is reclaimed by an ordinary
repeat import.

For an unmountable store, retain the supported stock ZIP or raw image before
proceeding: removing the store requires importing those files again. Stop the
radios with `wifi down`, close any users of `/mnt/wave600_firmware`, and unmount
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

After successful removal, reimport the retained stock input and reboot:

```sh
wave600-firmware-import /path/to/Archer-AX50-stock.zip && reboot
```
