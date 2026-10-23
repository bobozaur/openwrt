# SPDX-License-Identifier: GPL-2.0-only

# The compiled EVA image is linked at 0x60020000. U-Boot decompresses through
# the uncached alias; 0x82000000 is only its compressed-image staging address.
# Store both bank wrappers as the kernel intermediate so ImageBuilder needs
# neither a compiler nor the ELF to compose an ordinary sysupgrade image.
define Build/grx500-kernels
	set -e; \
	mkdir -p $@.kernels; \
	trap 'rm -rf $@.kernels' EXIT; \
	entry=$$($(TARGET_CROSS)readelf -h $(KDIR)/vmlinux.elf | sed -n 's/.*Entry point address: *//p'); \
	base=$$($(TARGET_CROSS)objdump -h $(KDIR)/vmlinux.elf | awk '$$2 == ".text" { print "0x" $$4 }'); \
	[ "$$((base))" -eq "$$((0x60020000))" ]; \
	entry=$$(printf '0x%x' $$((0xa0020000 + entry - base))); \
	for bank in a b; do \
		case $$bank in a) kernel=kernelA;; b) kernel=kernelB;; esac; \
		cat $@ $(KDIR)/image-$(patsubst %-a,%,$(firstword $(DEVICE_DTS)))-$$bank.dtb > $@.kernels/raw; \
		[ "$$(stat -c %s $@.kernels/raw)" -lt "$$((0x02000000 - 0x20000))" ]; \
		$(STAGING_DIR_HOST)/bin/lzma e $@.kernels/raw -lc1 -lp2 -pb2 -d23 $@.kernels/lzma; \
		$(STAGING_DIR_HOST)/bin/mkimage -A mips -O linux -T kernel -C lzma \
			-a 0xa0020000 -e $$entry -n '$(VERSION_DIST) GRX500' \
			-d $@.kernels/lzma $@.kernels/$$kernel; \
		[ "$$(stat -c %s $@.kernels/$$kernel)" -le 3174400 ]; \
	done; \
	tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@$(SOURCE_DATE_EPOCH) \
		-C $@.kernels -cf $@.new kernelA kernelB; \
	mv $@.new $@
endef

# Use the matching embedded-initramfs ELF, including a per-device rootfs suffix.
# Both the expanded image and compressed upload must fit the established RAM
# handoff; a larger configuration needs fewer packages, not another load address.
define Build/grx500-initramfs
	set -e; \
	[ -z "$(CONFIG_TARGET_ROOTFS_INITRAMFS_SEPARATE)" ]; \
	mkdir -p $@.ram; \
	trap 'rm -rf $@.ram' EXIT; \
	kernel='$(IMAGE_KERNEL)'; \
	case "$$kernel" in $(KDIR)/$(KERNEL_INITRAMFS_NAME)*) ;; *) exit 1;; esac; \
	elf='$(KDIR)/$(KERNEL_INITRAMFS_NAME).elf'$${kernel#$(KDIR)/$(KERNEL_INITRAMFS_NAME)}; \
	entry=$$($(TARGET_CROSS)readelf -h "$$elf" | sed -n 's/.*Entry point address: *//p'); \
	base=$$($(TARGET_CROSS)objdump -h "$$elf" | awk '$$2 == ".text" { print "0x" $$4 }'); \
	[ "$$((base))" -eq "$$((0x60020000))" ]; \
	[ "$$((entry))" -ge "$$((base))" ]; \
	[ "$$((entry - base))" -lt "$$(stat -c %s $@)" ]; \
	entry=$$(printf '0x%x' $$((0xa0020000 + entry - base))); \
	cat $@ $(KDIR)/image-$(patsubst %-a,%,$(firstword $(DEVICE_DTS)))-initramfs.dtb > $@.ram/raw; \
	[ "$$(stat -c %s $@.ram/raw)" -lt "$$((0x02000000 - 0x20000))" ]; \
	$(STAGING_DIR_HOST)/bin/lzma e $@.ram/raw -lc1 -lp2 -pb2 -d23 $@.ram/lzma; \
	$(STAGING_DIR_HOST)/bin/mkimage -A mips -O linux -T kernel -C lzma \
		-a 0xa0020000 -e $$entry -n '$(VERSION_DIST) GRX500 RAM' \
		-d $@.ram/lzma $@.ram/uImage; \
	[ "$$(stat -c %s $@.ram/uImage)" -lt "$$((0x02000000))" ]; \
	mv $@.ram/uImage $@
endef

define Build/grx500-sysupgrade
	set -e; \
	mkdir -p $@.parts/sysupgrade-$(DEVICE_NAME); \
	trap 'rm -rf $@.parts' EXIT; \
	[ "$$(stat -c %s $(IMAGE_ROOTFS))" -le 26284032 ]; \
	tar -xf $(IMAGE_KERNEL) -C $@.parts/sysupgrade-$(DEVICE_NAME); \
	cp $(IMAGE_ROOTFS) $@.parts/sysupgrade-$(DEVICE_NAME)/root; \
	printf 'BOARD=$(DEVICE_NAME)\n' > $@.parts/sysupgrade-$(DEVICE_NAME)/CONTROL; \
	tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@$(SOURCE_DATE_EPOCH) \
		-C $@.parts -cf $@ sysupgrade-$(DEVICE_NAME)
endef

define Device/tplink_archer-ax50-v1
  DEVICE_VENDOR := TP-Link
  DEVICE_MODEL := Archer AX50
  DEVICE_VARIANT := v1
  DEVICE_DTS := grx500_tplink_archer-ax50-v1-a grx500_tplink_archer-ax50-v1-b \
	grx500_tplink_archer-ax50-v1-initramfs
  KERNEL_DEPENDS := $$(wildcard $(DTS_DIR)/grx500*.dts*)
  KERNEL_LOADADDR := 0xa0020000
  KERNEL := kernel-bin | grx500-kernels
  KERNEL_INITRAMFS := kernel-bin | grx500-initramfs
  KERNEL_INITRAMFS_SUFFIX := -uImage
  BLOCKSIZE := 128k
  PAGESIZE := 2048
  FILESYSTEMS := squashfs
  SUPPORTED_DEVICES := tplink,archer-ax50-v1
  DEVICE_PACKAGES := kmod-gpio-button-hotplug kmod-leds-gpio kmod-ledtrig-network \
	uboot-envtools ubi-utils ax50-caldata kmod-grx500-eth \
	kmod-wave wave-scripts wpad-mbedtls ucode-mod-uci \
	kmod-usb3 kmod-usb-dwc3-grx500 kmod-usb-ledtrig-usbport
  IMAGES := sysupgrade.bin
  IMAGE/sysupgrade.bin := grx500-sysupgrade | append-metadata
endef
TARGET_DEVICES += tplink_archer-ax50-v1
