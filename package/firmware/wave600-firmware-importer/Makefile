# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=wave600-firmware-importer
PKG_VERSION:=1
PKG_RELEASE:=4
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/wave600-firmware-importer
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Intel WAVE600 firmware importer
  DEPENDS:=@TARGET_lantiq_grx500 +ubi-utils +flock +uclient-fetch +libustream-mbedtls +ca-bundle +unzip +squashfs-tools-unsquashfs +@SQUASHFS_TOOLS_XZ_SUPPORT
endef

define Package/wave600-firmware-importer/description
 Import the pinned NETGEAR RAX40 V1 radio firmware and Archer AX50 V1 PSD
 with wave600-firmware-import. Existing verified AX50 PSD is reused; a fresh
 install also needs the pinned AX50 input. Without arguments, download only
 the required inputs. Local ZIP/raw-image inputs support offline import.
 The dedicated UBI store survives sysupgrade and factory reset. No radio
 firmware is distributed in this package. Reboot after import.
endef

Build/Compile:=

define Package/wave600-firmware-importer/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/share/$(PKG_NAME) \
		$(1)/etc/init.d $(1)/lib/firmware/updates $(1)/mnt/wave600_firmware
	$(INSTALL_BIN) ./files/wave600-firmware-import $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/SHA256SUMS ./files/SHA256SUMS.ax50 $(1)/usr/share/$(PKG_NAME)/
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
