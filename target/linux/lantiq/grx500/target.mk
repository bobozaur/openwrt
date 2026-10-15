# SPDX-License-Identifier: GPL-2.0-only

ARCH:=mips
SUBTARGET:=grx500
BOARDNAME:=GRX500
FEATURES+=nand usb squashfs
CPU_TYPE:=interaptiv

DEFAULT_PACKAGES+=kmod-gpio-button-hotplug kmod-leds-gpio \
	uboot-envtools ubi-utils

define Target/Description
	Lantiq GRX500 with the interAptiv EVA memory map.
endef
