# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=wave600-firmware-importer
PKG_VERSION:=1
PKG_RELEASE:=3
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/wave600-firmware-importer
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Intel WAVE600 stock firmware importer
  DEPENDS:=@TARGET_lantiq_grx500 +ubi-utils +flock +uclient-fetch +libustream-mbedtls +ca-bundle +unzip +squashfs-tools-unsquashfs +@SQUASHFS_TOOLS_XZ_SUPPORT
endef

define Package/wave600-firmware-importer/description
 Import the original Archer AX50 V1 radio firmware with wave600-firmware-import.
 Run without arguments to download the pinned official 1.1.2 Build 20251022
 release, or supply its local ZIP archive or raw image. The imported files are
 stored in a dedicated UBI volume which survives sysupgrade and factory reset.
 Existing verified imports migrate automatically. No radio firmware is
 distributed in this package. Reboot after import to initialize the radios.
endef

Build/Compile:=

define Package/wave600-firmware-importer/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/share/$(PKG_NAME) \
		$(1)/etc/init.d $(1)/lib/firmware/updates $(1)/mnt/wave600_firmware
	$(INSTALL_BIN) ./files/wave600-firmware-import $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/SHA256SUMS $(1)/usr/share/$(PKG_NAME)/
	$(INSTALL_BIN) ./files/wave600-firmware.init $(1)/etc/init.d/wave600-firmware
	while read -r hash file; do \
		ln -s /mnt/wave600_firmware/firmware/$$$$file $(1)/lib/firmware/updates/$$$$file; \
	done < ./files/SHA256SUMS
endef

define Package/wave600-firmware-importer/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] || {
	# Upgrades do not automatically enable newly added init scripts.
	/etc/init.d/wave600-firmware enable &&
		/usr/sbin/wave600-firmware-import --migrate
}
endef

$(eval $(call BuildPackage,wave600-firmware-importer))
