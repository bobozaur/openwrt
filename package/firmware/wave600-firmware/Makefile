# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=wave600-firmware
PKG_VERSION:=06.00.08.00.87
PKG_RELEASE:=2
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar
PKG_SOURCE_URL:=file://$(DL_DIR)
PKG_SOURCE_PROTO:=wave-firmware-local
PKG_HASH:=d0cca5515d97f5c5e1ab2f7cc861d70fc39cdfd9bb483d32fc0a7b5c955f401c
# No firmware redistribution grant was found with the OEM image.
PKG_LICENSE:=LicenseRef-WAVE-Firmware
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define DownloadMethod/wave-firmware-local
	echo 'Place $(FILE) in $(DL_DIR) (SHA256 $(HASH))'; false
endef

define Package/wave600-firmware
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Intel WAVE600 radio firmware
  DEPENDS:=@TARGET_lantiq_grx500
endef

define Package/wave600-firmware/description
 Radio microcode from the Archer AX50 v1 OEM 250917 image. Supply the original
 files listed in SHA256SUMS in a flat, sorted USTAR archive with mode 0644,
 uid/gid 0, empty owner names and mtime 0. Calibration is read from each device
 separately. This local-input recipe does not establish redistribution rights.
endef

define Build/Prepare
	$(MKHASH) sha256 $(DL_DIR)/$(PKG_SOURCE) | grep -qx $(PKG_HASH)
	mkdir -p $(PKG_BUILD_DIR)
	$(TAR) -xf $(DL_DIR)/$(PKG_SOURCE) -C $(PKG_BUILD_DIR)
	(cd $(PKG_BUILD_DIR); sha256sum -c $(CURDIR)/SHA256SUMS)
endef

Build/Configure:=

Build/Compile:=

define Package/wave600-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/*.bin $(1)/lib/firmware/
endef

$(eval $(call BuildPackage,wave600-firmware))
