# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=ax50-caldata
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/ax50-caldata
  SECTION:=utils
  CATEGORY:=Base system
  TITLE:=Archer AX50 factory firmware and calibration readers
  DEPENDS:=@TARGET_lantiq_grx500 +zlib +liblzma
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -Wall -Wextra \
		$(PKG_BUILD_DIR)/ax50-caldata.c $(TARGET_LDFLAGS) -lz -o $(PKG_BUILD_DIR)/ax50-caldata
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -Wall -Wextra \
		$(PKG_BUILD_DIR)/ax50-gphy.c $(TARGET_LDFLAGS) -llzma -lz -o $(PKG_BUILD_DIR)/ax50-gphy
endef

define Package/ax50-caldata/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/hotplug.d/firmware $(1)/lib/firmware/lantiq
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ax50-caldata $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ax50-gphy $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/10-ax50-caldata $(1)/etc/hotplug.d/firmware/
	$(LN) /tmp/wave/cal_wlan0.bin $(1)/lib/firmware/cal_wlan0.bin
	$(LN) /tmp/wave/cal_wlan2.bin $(1)/lib/firmware/cal_wlan2.bin
	$(LN) /tmp/wave/cal_wlan4.bin $(1)/lib/firmware/cal_wlan4.bin
	$(LN) /tmp/grx500-phy11g-a21.bin $(1)/lib/firmware/lantiq/grx500-phy11g-a21.bin
endef

$(eval $(call BuildPackage,ax50-caldata))
