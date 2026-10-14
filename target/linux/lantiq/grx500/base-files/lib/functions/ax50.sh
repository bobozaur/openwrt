# SPDX-License-Identifier: GPL-2.0-only

# Only the dedicated embedded-initramfs DT admits initial provisioning. Native
# /init switches to tmpfs and skips mount_root; no bank is running in this mode.
ax50_is_initial_install() {
	awk '{for (i=1;i<=NF;i++) {
		if ($i ~ /^ax50_initial_install=/) {n++; if ($i == "ax50_initial_install=1") ok++}
		if ($i ~ /^(root|fstools_overlay_name)=/) bad++
	}} END {exit n == 1 && ok == 1 && !bad ? 0 : 1}' /proc/cmdline &&
	awk '$2 == "/" {n++; if ($3 == "tmpfs" && $4 ~ /(^|,)rw(,|$)/) ok++}
		$2 == "/rom" || $2 == "/overlay" {bad++}
		END {exit n == 1 && ok == 1 && !bad ? 0 : 1}' /proc/mounts
}

# The appended DT is immutable bank identity. U-Boot consumes the trial
# selector before entering Linux, so update_chk cannot identify this root.
ax50_running_bank() {
	local root overlay
	root=$(awk '{for (i=1;i<=NF;i++) if ($i ~ /^root=/) {n++; v=$i}}
		END {if (n != 1) exit 1; print v}' /proc/cmdline) || return 1
	overlay=$(awk '{for (i=1;i<=NF;i++) if ($i ~ /^fstools_overlay_name=/) {n++; v=$i}}
		END {if (n != 1) exit 1; print v}' /proc/cmdline) || return 1
	case "$root:$overlay" in
	root=/dev/ubiblock0_1:fstools_overlay_name=ax50_overlayA) echo A ;;
	root=/dev/ubiblock0_3:fstools_overlay_name=ax50_overlayB) echo B ;;
	*) return 1 ;;
	esac
}

ax50_check_volume() {
	local dir="/sys/class/ubi/ubi0_$1"
	[ "$(cat "$dir/name")" = "$2" ] &&
	[ "$(cat "$dir/reserved_ebs")" = "$3" ] &&
	[ "$(cat "$dir/usable_eb_size")" = 126976 ] &&
	[ "$(cat "$dir/type")" = dynamic ]
}

ax50_check_core_layout() {
	[ "$(cat /sys/class/ubi/ubi0/mtd_num)" = 5 ] &&
	[ "$(cat /sys/class/mtd/mtd5/name)" = system_sw ] &&
	ax50_check_volume 0 kernelA 25 &&
	ax50_check_volume 1 rootfsA 215 &&
	ax50_check_volume 2 kernelB 25 &&
	ax50_check_volume 3 rootfsB 207 &&
	ax50_check_volume 4 data_vol 166
}

ax50_check_layout() {
	ax50_check_core_layout &&
	ax50_check_volume 6 ax50_overlayA 128 &&
	ax50_check_volume 7 ax50_overlayB 128
}

ax50_check_initial_layout() {
	local dir available
	ax50_check_core_layout &&
		[ ! -e /sys/class/ubi/ubi0_6 ] && [ ! -e /sys/class/ubi/ubi0_7 ] || return 1
	for dir in /sys/class/ubi/ubi0_*; do
		[ -d "$dir" ] || continue
		# Only the recorded five-volume OEM layout is an admitted install
		# input. Extra OEM/foreign volumes are preserved by rejecting it.
		case "${dir##*/}" in ubi0_[0-4]) ;; *) return 1;; esac
	done
	available=$(cat /sys/class/ubi/ubi0/avail_eraseblocks) || return 1
	case "$available" in ''|*[!0-9]*) return 1;; esac
	[ "$available" -ge 256 ]
}

ax50_check_lower_root() {
	local bank root dev
	bank=$(ax50_running_bank) || return 1
	case "$bank" in A) root=1 ;; B) root=3 ;; esac
	[ "$(cat "/sys/class/ubi/ubi0_$root/name")" = "rootfs$bank" ] || return 1
	dev=$(cat "/sys/class/block/ubiblock0_$root/dev") && [ -n "$dev" ] || return 1
	awk -v dev="$dev" '$5 == "/rom" {
		count++
		if ($3 == dev)
			for (i=7;i<NF;i++) if ($i == "-" && $(i+1) == "squashfs") matched++
	} END {exit count == 1 && matched == 1 ? 0 : 1}' /proc/self/mountinfo
}

ax50_check_running_root() {
	local bank overlay
	ax50_check_lower_root || return 1
	bank=$(ax50_running_bank) || return 1
	case "$bank" in A) overlay=6 ;; B) overlay=7 ;; esac
	awk -v name="ubi0:ax50_overlay$bank" -v dev="/dev/ubi0_$overlay" '
		$2 == "/" && $3 == "overlay" && $4 ~ /(^|,)rw(,|$)/ &&
			$4 ~ /(^|,)upperdir=\/overlay\/upper(,|$)/ {root++}
		$2 == "/overlay" && ($1 == name || $1 == dev) && $3 == "ubifs" &&
			$4 ~ /(^|,)rw(,|$)/ {overlay++}
		END {exit root == 1 && overlay == 1 ? 0 : 1}' /proc/mounts
}

ax50_check_bootenv() {
	# Reject invalid/default environments before asking fw_setenv to write.
	[ "$(fw_printenv -n bootcmd)" = 'run flash_flash' ] &&
	[ "$(fw_printenv -n check_image0)" = 'run switchbankA' ] &&
	[ "$(fw_printenv -n check_image1)" = 'run switchbankB;setenv update_chk 0;save' ] &&
	[ "$(fw_printenv -n check_image2)" = 'run switchbankB' ] &&
	[ "$(fw_printenv -n check_image3)" = 'run switchbankA;setenv update_chk 2;save' ]
}

# Initial installation additionally pins the recorded OEM command bodies. This
# is deliberately narrower than subsequent upgrades of a confirmed standard bank.
ax50_check_initial_bootenv() {
	ax50_check_bootenv &&
	[ "$(fw_printenv -n update_chk)" = 0 ] &&
	[ "$(fw_printenv -n ubi_init)" = 'setenv kernelA_id 0;setenv rootfsA_id 1;setenv firmwareA_id 2;setenv kernelB_id 3;setenv rootfsB_id 4;setenv firmwareB_id 5;setenv bootcoreA_id 6; setenv bootcoreB_id 7;setenv setbank check_image$(update_chk);run $(setbank);ubi part system_sw' ] &&
	[ "$(fw_printenv -n flash_flash)" = 'run run_bootcore;ubi read $(loadaddr) $(kernel_vol);run flashargs addmisc;bootm $(loadaddr);reset' ] &&
	[ "$(fw_printenv -n flashargs)" = 'setenv bootargs ubi.mtd=system_sw rootfsname=$(rootfsname) ro rootfstype=squashfs do_overlay' ] &&
	[ "$(fw_printenv -n addmisc)" = 'setenv bootargs $(bootargs) console=$(console),$(baudrate) ethaddr=$(ethaddr) panic=1 $(mtdparts) init=/etc/preinit active_bank=$(active_bank) update_chk=$(update_chk) maxcpus=4 pci=pcie_bus_perf ethwan=$(ethwan) ubootver=$(ver) mem=256M@512M Production=$(Production) ' ] &&
	[ "$(fw_printenv -n run_bootcore)" = 'run ubi_init; ubi read 0xA0400000 $(bootcore_vol) ; secboot load_os 0x88000000 0xA0400000 0x200000' ] &&
	[ "$(fw_printenv -n switchbankA)" = 'setenv active_bank A;setenv kernel_id $(kernelA_id);setenv rootfs_id $(rootfsA_id);setenv f_kernel_size f_kernel_sizeA;setenv kernel_vol kernelA;setenv rootfs_vol rootfsA;setenv firmware_vol firmwareA;setenv rootfsname rootfsA;setenv bootcore_vol bootcoreA' ] &&
	[ "$(fw_printenv -n switchbankB)" = 'setenv active_bank B;setenv kernel_id $(kernelB_id);setenv rootfs_id $(rootfsB_id);setenv f_kernel_size f_kernel_sizeB;setenv kernel_vol kernelB;setenv rootfs_vol rootfsB;setenv firmware_vol firmwareB;setenv rootfsname rootfsB;setenv bootcore_vol bootcoreB' ]
}

ax50_select_bank() {
	local value script result
	case "$1:$2" in
	A:stable) value=0 ;;
	B:trial) value=1 ;;
	B:stable) value=2 ;;
	A:trial) value=3 ;;
	*) return 1 ;;
	esac
	ax50_check_bootenv || return 1
	[ "$(fw_printenv -n update_chk)" = "$value" ] &&
		[ "$(fw_printenv -n loadaddr)" = 0x82000000 ] && return 0
	script=$(mktemp /tmp/ax50-env.XXXXXX) || return 1
	printf 'update_chk %s\nloadaddr 0x82000000\n' "$value" > "$script" || {
		rm -f "$script"
		return 1
	}
	fw_setenv -s "$script"
	result=$?
	rm -f "$script"
	[ "$result" = 0 ] &&
	[ "$(fw_printenv -n update_chk)" = "$value" ] &&
	[ "$(fw_printenv -n loadaddr)" = 0x82000000 ]
}

# Manufacturing identity is read on demand from the protected OEM data volume.
# It is never copied into a second configuration store.
ax50_factory_mac() {
	local dir mac result=0
	dir=$(mktemp -d /tmp/ax50-factory.XXXXXX) || return 1
	if ! mount -t ubifs -o ro ubi0:data_vol "$dir"; then
		rmdir "$dir"
		return 1
	fi
	if [ ! -L "$dir/default-mac" ] && [ -f "$dir/default-mac" ] &&
		[ "$(wc -c < "$dir/default-mac")" = 6 ]; then
		mac=$(get_mac_binary "$dir/default-mac" 0) || result=1
	else
		result=1
	fi
	umount "$dir" || result=1
	rmdir "$dir"
	[ "$result" = 0 ] && [ -n "$mac" ] &&
		[ "$mac" != 00:00:00:00:00:00 ] &&
		[ "$((0x${mac%%:*} & 1))" = 0 ] || return 1
	printf '%s\n' "$mac"
}
