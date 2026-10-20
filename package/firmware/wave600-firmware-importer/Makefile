# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=wave600-firmware-importer
PKG_VERSION:=1
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/wave600-firmware-importer
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Intel WAVE600 stock firmware importer
  DEPENDS:=@TARGET_lantiq_grx500 +uclient-fetch +libustream-mbedtls +ca-bundle +unzip +squashfs-tools-unsquashfs +@SQUASHFS_TOOLS_XZ_SUPPORT
endef

define Package/wave600-firmware-importer/description
 Import the original Archer AX50 V1 radio firmware with wave600-firmware-import.
 Run without arguments to download the pinned official 1.1.2 Build 20251022
 release, or supply its local ZIP archive or raw image. The imported files are
 preserved by normal sysupgrade backups. No radio firmware is distributed in
 this package. Reboot after import to initialize the radios.
endef

Build/Compile:=

define Package/wave600-firmware-importer/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/share/$(PKG_NAME) $(1)/lib/upgrade/keep.d
	$(INSTALL_BIN) ./files/wave600-firmware-import $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/SHA256SUMS $(1)/usr/share/$(PKG_NAME)/
	sed 's,^[^ ]*  ,/lib/firmware/,' ./files/SHA256SUMS > $(1)/lib/upgrade/keep.d/wave600-firmware
endef

$(eval $(call BuildPackage,wave600-firmware-importer))
