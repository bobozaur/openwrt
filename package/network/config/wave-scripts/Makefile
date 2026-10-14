# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=wave-scripts
PKG_RELEASE:=7
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/wave-scripts
  SECTION:=utils
  CATEGORY:=Network
  TITLE:=WAVE radio configuration support
  DEPENDS:=+wifi-scripts +iw +kmod-wave +@WIFI_SCRIPTS_UCODE
  PKGARCH:=all
endef

Build/Prepare:=
Build/Configure:=
Build/Compile:=

define Package/wave-scripts/install
	$(INSTALL_DIR) $(1)
	$(CP) ./files/* $(1)/
endef

$(eval $(call BuildPackage,wave-scripts))
