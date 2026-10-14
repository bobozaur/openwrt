#!/bin/sh
# Copyright (C) 2006-2019 OpenWrt.org

. /lib/functions/leds.sh

boot="$(get_dt_led boot)"
failsafe="$(get_dt_led failsafe)"
running="$(get_dt_led running)"
upgrade="$(get_dt_led upgrade)"

set_led_state() {
	status_led="$boot"

	case "$1" in
	preinit)
		status_led_blink_preinit
		;;
	failsafe)
		if [ -n "$status_led" ]; then
			status_led_off || return $?
		fi
		if [ -n "$running" ]; then
			status_led="$running"
			status_led_off || return $?
		fi
		status_led="$failsafe"
		status_led_blink_failsafe
		;;
	preinit_regular)
		status_led_blink_preinit_regular
		;;
	upgrade)
		if [ -n "$running" ]; then
			status_led="$running"
			status_led_off || return $?
		fi
		status_led="$upgrade"
		status_led_blink_preinit_regular
		;;
	done)
		if [ -n "$status_led" ]; then
			status_led_off || return $?
		fi
		if [ "$status_led" != "$running" ]; then
			status_led_restore_trigger "boot" || return $?
		fi
		if [ -n "$running" ]; then
			status_led="$running"
			status_led_on
		fi
		;;
	esac
}

set_state() {
	local ret

	[ -n "$boot" -o -n "$failsafe" -o -n "$running" -o -n "$upgrade" ] || return 0
	if [ "$LED_RUNTIME_LOCKED" != 1 ] && [ -x /usr/libexec/led-runtime ]; then
		if /usr/libexec/led-runtime diag "$1"; then
			return 0
		else
			ret=$?
		fi
		# The helper attempted this operation under its lock. Repeating it
		# outside the lock could overwrite a newly applied runtime pattern.
		[ "$ret" = 64 ] && return 1
	fi
	# Early boot or a missing runtime dependency must not suppress diagnostics.
	set_led_state "$1"
}
