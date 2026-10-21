# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=ax50-leds
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_MAINTAINER:=Bogdan Mircea <mirceapetrebogdan@gmail.com>

include $(INCLUDE_DIR)/package.mk

define Package/ax50-leds
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=TP-Link Archer AX50 LED policy
  DEPENDS:=@TARGET_lantiq_grx500 +ucode +ucode-mod-fs +ucode-mod-ubus +ucode-mod-uci +ucode-mod-uloop +pingcheck
endef

define Package/ax50-leds/description
  Arbitrate the Archer AX50's WPS and Internet indicators with configured LEDs,
  global off and boot diagnostics. Hostapd and pingcheck own the source state.
endef

define Build/Compile
endef

define Package/ax50-leds/install
	$(CP) ./files/* $(1)/
	$(LN) ax50-leds $(1)/usr/libexec/led-policy
endef

$(eval $(call BuildPackage,ax50-leds))
