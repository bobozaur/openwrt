# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=wave-scripts
PKG_RELEASE:=3
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/wave-scripts
  SECTION:=utils
  CATEGORY:=Network
  TITLE:=WAVE radio configuration support
  DEPENDS:=+wifi-scripts +iw +kmod-wave +hostapd-common +@WIFI_SCRIPTS_UCODE
  EXTRA_DEPENDS:=hostapd-common (>=2026.08.07~831364bf-r5), wifi-scripts (>=1.0-r5)
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
