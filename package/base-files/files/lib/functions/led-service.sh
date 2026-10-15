# Copyright (C) 2008 OpenWrt.org

led_identity() {
	local entry
	entry="$(ls -di "/sys/class/leds/$1" 2>/dev/null)" || return 1
	set -- $entry
	printf '%s\n' "$1"
}

led_configured() {
	local sysfs
	config_get sysfs "$1" sysfs
	[ "$sysfs" != "$2" ] || led_has_config=1
}

restore_defaults() {
	local path name boot failsafe running upgrade led_has_config triggers ret=0
	. /lib/functions/leds.sh
	boot="$(get_dt_led boot)"
	failsafe="$(get_dt_led failsafe)"
	running="$(get_dt_led running)"
	upgrade="$(get_dt_led upgrade)"

	for path in /sys/class/leds/*; do
		name="${path##*/}"
		[ -z "$1" ] || [ "$name" = "$1" ] || continue
		[ -e "$path/trigger" ] || continue
		led_has_config=0
		config_foreach led_configured led "$name"
		[ "$led_has_config" = 0 ] || continue
		case "$name" in
			"$boot"|"$failsafe"|"$running"|"$upgrade") continue ;;
		esac
		# The kernel resolves its current driver default; never retain a PHY
		# trigger name across device removal and registration.
		triggers="$(cat "$path/trigger")" || {
			[ ! -e "$path/trigger" ] || ret=1
			continue
		}
		case " $triggers " in
			*" default "*) ;;
			*) continue ;;
		esac
		echo default > "$path/trigger" || ret=$?
	done
	return "$ret"
}

led_color_set() {
	local cfg="$1"
	local sysfs="$2"

	local max_b
	local colors
	local color
	local multi_intensity
	local value
	local write

	[ -e /sys/class/leds/${sysfs}/multi_intensity ] || return 0
	[ -e /sys/class/leds/${sysfs}/multi_index ] || return 0

	max_b="$(cat /sys/class/leds/${sysfs}/max_brightness)"
	colors="$(cat /sys/class/leds/${sysfs}/multi_index | tr " " "\n")"
	multi_intensity=""
	for color in $colors; do
		config_get value $1 "color_${color}" "0"
		[ "$value" -gt 0 ] && write=1
		[ "$value" -gt "$max_b" ] && value="$max_b"
		multi_intensity="${multi_intensity}${value} "
	done

	# Check if any color is configured
	[ "$write" = 1 ] || return 0
	# Remove last whitespace
	multi_intensity="${multi_intensity:0:-1}"

	echo "setting '${name}' led color to '${multi_intensity}'"
	echo "${multi_intensity}" > /sys/class/leds/${sysfs}/multi_intensity
}

load_led() {
	local name
	local sysfs
	local trigger
	local dev
	local ports
	local mode
	local default
	local delayon
	local delayoff
	local interval
	local brightness
	local ret
	local identity

	config_get sysfs $1 sysfs
	config_get name $1 name "$sysfs"
	config_get trigger $1 trigger "none"
	config_get dev $1 dev
	config_get ports $1 port
	config_get mode $1 mode
	config_get_bool default $1 default "0"
	config_get delayon $1 delayon
	config_get delayoff $1 delayoff
	config_get interval $1 interval "50"
	config_get port_state $1 port_state
	config_get delay $1 delay "150"
	config_get message $1 message ""
	config_get gpio $1 gpio "0"
	config_get_bool inverted $1 inverted "0"
	config_get brightness $1 brightness

	[ "$2" ] && [ "$sysfs" != "$2" ] && return 0

	# execute application led trigger
	[ -f "/usr/libexec/led-trigger/${trigger}" ] && {
		. "/usr/libexec/led-trigger/${trigger}"
		return $?
	}

	[ "$trigger" = "usbdev" ] && {
		# Backward compatibility: translate to the new trigger
		trigger="usbport"
		# Translate port of root hub, e.g. 4-1 -> usb4-port1
		ports=$(echo "$dev" | sed -n 's/^\([0-9]*\)-\([0-9]*\)$/usb\1-port\2/p')
		# Translate port of extra hub, e.g. 2-2.4 -> 2-2-port4
		[ -z "$ports" ] && ports=$(echo "$dev" | sed -n 's/\./-port/p')
	}

	[ -e /sys/class/leds/${sysfs}/brightness ] && {
		echo "setting up led ${name}"
		identity="$(led_identity "$sysfs")" || {
			[ ! -e "/sys/class/leds/$sysfs" ] || return 1
			return 0
		}

		printf "%s %s %d" \
			"$sysfs" \
			"$(sed -ne 's/^.*\[\(.*\)\].*$/\1/p' /sys/class/leds/${sysfs}/trigger)" \
			"$(cat /sys/class/leds/${sysfs}/brightness)" \
				>> /var/run/led.state || return $?
		# Save default color if supported
		[ -e /sys/class/leds/${sysfs}/multi_intensity ] && {
			printf " %s" \
				"$(sed 's/\ /:/g' /sys/class/leds/${sysfs}/multi_intensity)" \
					>> /var/run/led.state || return $?
		} || {
			printf " -" >> /var/run/led.state || return $?
		}
		printf " %s\n" "$identity" >> /var/run/led.state || return $?

		if [ "$default" = 0 ]; then
			echo 0 >/sys/class/leds/${sysfs}/brightness || return $?
		fi

		[ "$default" = 1 ] && {
			[ -z "$brightness" ] && brightness="$(cat /sys/class/leds/${sysfs}/max_brightness)"
			echo "$brightness" > /sys/class/leds/${sysfs}/brightness || return $?
		}

		led_color_set "$1" "$sysfs" || return $?

		echo $trigger > /sys/class/leds/${sysfs}/trigger 2> /dev/null
		ret="$?"
		[ $ret = 0 ] || {
			echo >&2 "Skipping trigger '$trigger' for led '$name' due to missing kernel module"
			return 1
		}
		case "$trigger" in
		"heartbeat")
			echo "${inverted}" > "/sys/class/leds/${sysfs}/invert" || return $?
			;;

		"netdev")
			if [ -n "$dev" ]; then
				echo $dev > /sys/class/leds/${sysfs}/device_name || return $?
				for m in $mode; do
					if [ -e "/sys/class/leds/${sysfs}/$m" ]; then
						echo 1 > /sys/class/leds/${sysfs}/$m || return $?
					fi
				done
				echo $interval > /sys/class/leds/${sysfs}/interval 2>/dev/null || return $?
			fi
			;;

		"timer"|"oneshot")
			if [ -n "$delayon" ]; then
				echo $delayon > /sys/class/leds/${sysfs}/delay_on || return $?
			fi
			if [ -n "$delayoff" ]; then
				echo $delayoff > /sys/class/leds/${sysfs}/delay_off || return $?
			fi
			;;

		"usbport")
			local p

			for p in $ports; do
				echo 1 > /sys/class/leds/${sysfs}/ports/$p || return $?
			done
			;;

		"port_state")
			if [ -n "$port_state" ]; then
				echo $port_state > /sys/class/leds/${sysfs}/port_state || return $?
			fi
			;;

		"gpio")
			echo $gpio > /sys/class/leds/${sysfs}/gpio || return $?
			echo $inverted > /sys/class/leds/${sysfs}/inverted || return $?
			;;

		switch[0-9]*)
			local port_mask speed_mask

			config_get port_mask $1 port_mask
			if [ -n "$port_mask" ]; then
				echo $port_mask > /sys/class/leds/${sysfs}/port_mask || return $?
			fi
			config_get speed_mask $1 speed_mask
			if [ -n "$speed_mask" ]; then
				echo $speed_mask > /sys/class/leds/${sysfs}/speed_mask || return $?
			fi
			if [ -n "$mode" ]; then
				echo "$mode" > /sys/class/leds/${sysfs}/mode || return $?
			fi
			;;
		esac
	}
	return 0
}

load_led_checked() {
	# A later filtered-out section must not hide a selected LED's failure.
	load_led "$@" || led_start_ret=$?
}

led_turnoff() {
	local ret=0
	for led in `ls /sys/class/leds/`; do
		[ -z "$1" ] || [ "$led" = "$1" ] || continue
		echo 0 > /sys/class/leds/$led/brightness || ret=$?
	done
	return "$ret"
}

led_turnon() {
	led_turnoff || return $?
	[ "$(uci -q get system.@system[-1].leds_off)" != 1 ] || return 0
	. /etc/diag.sh
	set_led_state done || return $?
	led_start
}

led_blink() {
	local ret=0
	for led in `ls /sys/class/leds/`; do
		echo 0 > /sys/class/leds/$led/brightness || ret=$?
		echo timer > /sys/class/leds/$led/trigger || ret=$?
	done
	return "$ret"
}

led_start() {
	local led_start_ret=0 ret
	if [ "$ACTION" = add ] && [ -n "$1" ] && [ "$DEVICENAME" = "$1" ] && [ -s /var/run/led.state ]; then
		# A fresh registration invalidates legacy rows even while LEDs stay
		# off; a later turnon must not replay the previous device's trigger.
		LED_CACHE_NAME="$1" awk '$1 != ENVIRON["LED_CACHE_NAME"] || NF >= 5' \
			/var/run/led.state > /var/run/led.state.new || led_start_ret=$?
		if [ "$led_start_ret" = 0 ]; then
			mv /var/run/led.state.new /var/run/led.state || led_start_ret=$?
		fi
	fi
	[ "$(uci -q get system.@system[-1].leds_off)" = '1' ] && {
		# Cache I/O must not prevent the requested physical off operation.
		led_turnoff "$1" || {
			ret=$?
			[ "$led_start_ret" != 0 ] || led_start_ret="$ret"
		}
		return "$led_start_ret"
	}
	[ "$led_start_ret" = 0 ] || return "$led_start_ret"
	[ -e /sys/class/leds/ ] && {
		config_load system || return $?
		restore_defaults "$1" || led_start_ret=$?
		[ -s /var/run/led.state ] && {
			local led trigger brightness color identity current_identity
			while read led trigger brightness color identity; do
				[ "$1" ] && [ "$1" != "$led" ] && continue
				# The same class name can identify a new removable device.
				# Legacy rows are usable until a fresh hotplug registration.
				if [ -n "$identity" ]; then
					current_identity="$(led_identity "$led")" || continue
					[ "$identity" = "$current_identity" ] || continue
				elif [ "$ACTION" = add ] && [ "$DEVICENAME" = "$led" ]; then
					continue
				fi
				if [ -e "/sys/class/leds/$led/trigger" ]; then
					echo "$trigger" > "/sys/class/leds/$led/trigger" || return $?
				fi

				if [ -e "/sys/class/leds/$led/brightness" ]; then
					echo "$brightness" > "/sys/class/leds/$led/brightness" || return $?
				fi

				if [ -n "$color" ] && [ "$color" != - ] && [ -e "/sys/class/leds/$led/multi_intensity" ]; then
					echo "$color" | sed 's/:/\ /g' > \
						"/sys/class/leds/$led/multi_intensity" || return $?
				fi
			done < /var/run/led.state
			if [ "$1" ]; then
				awk -v led="$1" '$1 != led' /var/run/led.state > /var/run/led.state.new || return $?
				mv /var/run/led.state.new /var/run/led.state || return $?
			else
				rm /var/run/led.state || return $?
			fi
		}

		config_foreach load_led_checked led "$1"
		return "$led_start_ret"
	}
}

# Raw LED operations. A policy owner calls this only while holding its lock;
# these functions never delegate back to the optional policy executable.
led_apply() {
	local action="$1"
	shift
	case "$action" in
		start) led_start "$@" ;;
		turnon) led_turnon "$@" ;;
		turnoff) led_turnoff "$@" ;;
		blink) led_blink "$@" ;;
		*) return 1 ;;
	esac
}
