# SPDX-License-Identifier: GPL-2.0-only

. /lib/functions/ax50.sh

REQUIRE_IMAGE_METADATA=1
RAMFS_COPY_BIN='sha256sum head mktemp'

AX50_IMAGE_DIR=sysupgrade-tplink_archer-ax50-v1

platform_check_image() {
	local image="$1" list entry count size magic
	[ "$(board_name)" = tplink,archer-ax50-v1 ] || return 1
	list=$(tar -tf "$image") || return 1
	# Extract by exact member name; reject duplicates and additional payloads.
	for entry in "$AX50_IMAGE_DIR/" "$AX50_IMAGE_DIR/CONTROL" \
		"$AX50_IMAGE_DIR/kernelA" "$AX50_IMAGE_DIR/kernelB" "$AX50_IMAGE_DIR/root"; do
		count=$(printf '%s\n' "$list" | grep -Fxc "$entry")
		[ "$count" = 1 ] || return 1
	done
	[ "$(printf '%s\n' "$list" | wc -l)" = 5 ] &&
	[ "$(tar -xOf "$image" "$AX50_IMAGE_DIR/CONTROL")" = \
		'BOARD=tplink_archer-ax50-v1' ] || return 1
	for entry in kernelA kernelB root; do
		size=$(tar -xOf "$image" "$AX50_IMAGE_DIR/$entry" | wc -c)
		magic=$(tar -xOf "$image" "$AX50_IMAGE_DIR/$entry" | \
			hexdump -v -n 4 -e '4/1 "%02x"')
		case "$entry:$magic" in
		kernel*:27051956) [ "$size" -gt 64 ] && [ "$size" -le 3174400 ] || return 1 ;;
		root:68737173) [ "$size" -ge 96 ] && [ "$size" -le 26284032 ] || return 1 ;;
		*) return 1 ;;
		esac
	done
	if ax50_is_initial_install; then
		ax50_check_initial_layout && ax50_check_initial_bootenv
	else
		ax50_check_layout && ax50_check_bootenv
	fi
}

platform_pre_upgrade() {
	if ax50_is_initial_install; then
		# OEM settings are not a standard OpenWrt configuration backup.
		[ -z "$UPGRADE_BACKUP" ] && ax50_check_initial_layout &&
			ax50_check_initial_bootenv || exit 1
		return 0
	fi
	# Identify the confirmed booted SquashFS, including when stock extroot
	# supplies the writable root. Only the inactive bank is written in stage2.
	local bank expected
	bank=$(ax50_running_bank) || exit 1
	case "$bank" in A) expected=0 ;; B) expected=2 ;; esac
	ax50_check_lower_root && ax50_check_bootenv &&
		[ "$(fw_printenv -n update_chk)" = "$expected" ] || exit 1
}

ax50_volume_unmounted() {
	local id="$1" name dev block
	name=$(cat "/sys/class/ubi/ubi0_$id/name") || return 1
	# UBIFS uses the character volume; SquashFS uses its ubiblock child.
	awk -v dev="/dev/ubi0_$id" -v name="ubi0:$name" '
		$1 == dev || $1 == name {found=1} END {exit found ? 1 : 0}' /proc/mounts || return 1
	block="/sys/class/block/ubiblock0_$id/dev"
	if [ -e "$block" ]; then
		dev=$(cat "$block") || return 1
		awk -v dev="$dev" '$3 == dev {found=1} END {exit found ? 1 : 0}' \
			/proc/self/mountinfo || return 1
	fi
}

ax50_write_volume() {
	local file="$1" id="$2" size capacity expected actual
	size=$(wc -c < "$file")
	capacity=$(( $(cat "/sys/class/ubi/ubi0_$id/reserved_ebs") * 126976 ))
	[ "$size" -gt 0 ] && [ "$size" -le "$capacity" ] || return 1
	expected=$(sha256sum "$file" | cut -d ' ' -f 1)
	ubiupdatevol "/dev/ubi0_$id" "$file" || return 1
	actual=$(head -c "$size" "/dev/ubi0_$id" | sha256sum | cut -d ' ' -f 1)
	[ "$actual" = "$expected" ]
}

ax50_initial_remove_overlay() {
	local id="$1" name="$2" dev="$3"
	# Only a successful creation in this invocation grants removal ownership.
	[ -n "$dev" ] && ax50_check_core_layout &&
		ax50_check_volume "$id" "$name" 128 &&
		[ "$(cat "/sys/class/ubi/ubi0_$id/dev")" = "$dev" ] &&
		ax50_volume_unmounted "$id" || return 1
	[ ! -e "/sys/class/block/ubiblock0_$id" ] || return 1
	ubirmvol /dev/ubi0 -n "$id" && [ ! -e "/sys/class/ubi/ubi0_$id" ]
}

ax50_initial_install_cleanup() {
	local result="$1" failed=0
	trap - EXIT HUP INT TERM
	# A failed mount command can still require cleanup. Never traverse a
	# mounted overlay while removing temporary files or a newly made volume.
	if awk -v target="$dir/overlay" '$2 == target {found=1}
		END {exit found ? 0 : 1}' /proc/mounts; then
		if awk -v target="$dir/overlay" '$2 == target {
			count++; if (($1 == "ubi0:ax50_overlayB" || $1 == "/dev/ubi0_7") &&
				$3 == "ubifs") matched++
		} END {exit count == 1 && matched == 1 ? 0 : 1}' /proc/mounts; then
			umount "$dir/overlay" || failed=1
		else
			failed=1
		fi
	fi
	if [ "$selection_attempted" = 0 ]; then
		# Unknown selector state must retain the candidate, just like an
		# uncertain selector submission. A fallback must keep its overlays.
		if ax50_is_initial_install && ax50_check_initial_bootenv; then
			if [ "$created_b" = 1 ]; then
				ax50_initial_remove_overlay 7 ax50_overlayB "$dev_b" || failed=1
			fi
			if [ "$created_a" = 1 ]; then
				ax50_initial_remove_overlay 6 ax50_overlayA "$dev_a" || failed=1
			fi
		else
			failed=1
		fi
	fi
	if [ "$failed" != 0 ]; then
		echo 'AX50 initial-install cleanup incomplete; retain the recovery backup' >&2
		result=1
	fi
	exit "$result"
}

ax50_initial_install() {
	local dir id preserved created_a=0 created_b=0 dev_a= dev_b= selection_attempted=0
	ax50_is_initial_install && [ "$(board_name)" = tplink,archer-ax50-v1 ] &&
		[ -z "$UPGRADE_BACKUP" ] && ax50_check_initial_layout &&
		ax50_check_initial_bootenv || return 1
	for id in 0 1 2 3 4; do
		ax50_volume_unmounted "$id" || return 1
	done
	dir=$(mktemp -d /tmp/ax50-install.XXXXXX) || return 1
	(
		set -e
		trap 'ax50_initial_install_cleanup "$?"' EXIT
		trap 'exit 1' HUP INT TERM
		tar -xOf "$1" "$AX50_IMAGE_DIR/root" > "$dir/root"
		tar -xOf "$1" "$AX50_IMAGE_DIR/kernelB" > "$dir/kernel"
		# Hash whole logical volumes, not just their image prefixes. No copy
		# of fallback A or shared factory data is staged or written to NAND.
		preserved=$(sha256sum /dev/ubi0_0 /dev/ubi0_1 /dev/ubi0_4)
		ubimkvol /dev/ubi0 -n 6 -N ax50_overlayA -t dynamic -s 16252928
		created_a=1
		dev_a=$(cat /sys/class/ubi/ubi0_6/dev)
		ax50_check_volume 6 ax50_overlayA 128
		ubimkvol /dev/ubi0 -n 7 -N ax50_overlayB -t dynamic -s 16252928
		created_b=1
		dev_b=$(cat /sys/class/ubi/ubi0_7/dev)
		ax50_check_layout
		if [ -e /sys/class/block/ubiblock0_3 ]; then
			ubiblock -r /dev/ubi0_3
		fi
		ax50_write_volume "$dir/root" 3
		ax50_write_volume "$dir/kernel" 2
		# The new B overlay has no OEM configuration to import. Its first
		# native UBIFS mount creates only this explicitly allocated filesystem.
		mkdir "$dir/overlay"
		mount -t ubifs ubi0:ax50_overlayB "$dir/overlay"
		sync
		umount "$dir/overlay"
		[ "$(sha256sum /dev/ubi0_0 /dev/ubi0_1 /dev/ubi0_4)" = "$preserved" ]
		ax50_check_layout
		ax50_check_initial_bootenv
		# A lost acknowledgement may still have selected B. Retain both new
		# overlays after this point, even when fw_setenv/readback fails.
		selection_attempted=1
		ax50_select_bank B trial
	)
}

platform_do_upgrade() {
	if ax50_is_initial_install; then
		local install_result
		ax50_initial_install "$1"
		install_result=$?
		# do_stage2 does not inspect a platform hook's return value. Do not
		# report generic success/reboot after a failed initial transaction.
		[ "$install_result" = 0 ] || exit "$install_result"
		return 0
	fi
	local bank target kernel root overlay expected dir
	bank=$(ax50_running_bank) || return 1
	case "$bank" in
	A) target=B; kernel=2; root=3; overlay=7; expected=0 ;;
	B) target=A; kernel=0; root=1; overlay=6; expected=2 ;;
	*) return 1 ;;
	esac
	ax50_check_layout && ax50_check_bootenv &&
		[ "$(fw_printenv -n update_chk)" = "$expected" ] &&
		ax50_volume_unmounted "$kernel" && ax50_volume_unmounted "$root" &&
		ax50_volume_unmounted "$overlay" || return 1
	dir=$(mktemp -d /tmp/ax50-upgrade.XXXXXX) || return 1
	# Keep errors inside this subshell fatal. do_stage2 always reboots after
	# returning, leaving the unchanged stable selector as the fallback.
	(
		set -e
		tar -xOf "$1" "$AX50_IMAGE_DIR/root" > "$dir/root"
		tar -xOf "$1" "$AX50_IMAGE_DIR/kernel$target" > "$dir/kernel"
		if [ -e "/sys/class/block/ubiblock0_$root" ]; then
			ubiblock -r "/dev/ubi0_$root"
		fi
		ax50_write_volume "$dir/root" "$root"
		ax50_write_volume "$dir/kernel" "$kernel"
		# Empty only the inactive standard overlay. Mounting creates UBIFS on
		# this already allocated volume; shared UBI and OEM data are untouched.
		ubiupdatevol -t "/dev/ubi0_$overlay"
		mkdir "$dir/overlay"
		mount -t ubifs "ubi0:ax50_overlay$target" "$dir/overlay"
		if [ -n "$UPGRADE_BACKUP" ]; then
			cp "$UPGRADE_BACKUP" "$dir/overlay/sysupgrade.tgz"
		fi
		sync
		umount "$dir/overlay"
		ax50_select_bank "$target" trial
	)
}
