# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=wave
PKG_VERSION:=6.0.8.87.7
PKG_RELEASE:=16
PKG_SOURCE_VERSION:=06.00.08.00.87.00.07.d24084bd3447
WAVE_SOURCE_BASE:=lq-wave-300-$(PKG_SOURCE_VERSION)
PKG_SOURCE:=ax50v1_GPL_3.tar.gz
PKG_SOURCE_URL:=https://static.tp-link.com/upload/gpl-code/2022/202211/20221107
PKG_HASH:=8858fcb08c10051b698c672112af82faf406983bc6cfc846b5ff23c09b0ce88b
WAVE_SOURCE_PATH:=ax50v1_GPL_3/intel_GRX350/sdk/7.5.1.50/UGW-7.5.1.50/dl
PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(WAVE_SOURCE_BASE).wls.src
PKG_BUILD_DEPENDS:=mac80211
PKG_EXTMOD_SUBDIRS:=build/tools/mtlkroot/linux build/wireless/driver/linux
PKG_FIXUP:=autoreconf
PKG_AUTOMAKE_PATHS:=.
# The vendor log preprocessor updates shared generated files without locks.
PKG_BUILD_PARALLEL:=0
# The selected driver and RF C sources refer to the module-root dual license.
# This describes the built module, not other contents of the vendor SDK.
PKG_LICENSE:=GPL-2.0-only BSD-2-Clause
PKG_LICENSE_FILES:=LICENSE wireless/driver/rflib/AGREEMENT

STAMP_CONFIGURED_DEPENDS:=$(STAGING_DIR)/usr/include/mac80211-backport/backport/autoconf.h

include $(INCLUDE_DIR)/package.mk

define KernelPackage/wave
  SUBMENU:=Wireless Drivers
  TITLE:=Intel WAVE600 wireless support
  DEPENDS:=@TARGET_lantiq_grx500 +kmod-mac80211 +kmod-crypto-arc4 +@DRIVER_11AC_SUPPORT +@DRIVER_11AX_SUPPORT
  FILES:= \
	$(PKG_BUILD_DIR)/build/tools/mtlkroot/mtlkroot.ko \
	$(PKG_BUILD_DIR)/build/wireless/driver/mtlk.ko
  AUTOLOAD:=$(call AutoLoad,50,mtlkroot mtlk)
  MODPARAMS.mtlk:=fastpath=0 dual_pci=0 rcvry_on=0 ahb_off=1 loggersid=255
endef

define KernelPackage/wave/description
 Intel WAVE600 driver using the standard cfg80211/mac80211 backports API.
 Builds the driver and RF library from TP-Link's pinned Archer AX50 v1 source
 release. Import matching stock radio firmware separately with
 wave600-firmware-importer before initializing the radios.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)/source
	$(TAR) -xzf $(DL_DIR)/$(PKG_SOURCE) -C $(PKG_BUILD_DIR)/source --strip-components=6 \
		$(WAVE_SOURCE_PATH)/$(WAVE_SOURCE_BASE).gpl.wls.src.tar.bz2 \
		$(WAVE_SOURCE_PATH)/$(WAVE_SOURCE_BASE).rflib.wls.src.tar.bz2
	$(TAR) -xjf $(PKG_BUILD_DIR)/source/$(WAVE_SOURCE_BASE).gpl.wls.src.tar.bz2 -C $(PKG_BUILD_DIR) --strip-components=1
	$(TAR) -xjf $(PKG_BUILD_DIR)/source/$(WAVE_SOURCE_BASE).rflib.wls.src.tar.bz2 -C $(PKG_BUILD_DIR) --strip-components=1
	rm -r $(PKG_BUILD_DIR)/source
	$(Build/Patch)
	$(CP) ./files/openwrt.env $(PKG_BUILD_DIR)/support/
endef

CONFIGURE_PATH:=build
CONFIGURE_CMD:=../configure
CONFIGURE_ARGS+= \
	--with-app-toolchain=$(TOOLCHAIN_DIR) \
	--with-kernel=$(LINUX_DIR) \
	--with-kernel-cross-compile=$(TARGET_CROSS)

define Build/Configure
	mkdir -p $(PKG_BUILD_DIR)/build
	$(CP) ./files/config $(PKG_BUILD_DIR)/build/.config
	awk -f $(PKG_BUILD_DIR)/support/make_cfg_header.awk \
		< $(PKG_BUILD_DIR)/build/.config > $(PKG_BUILD_DIR)/build/.config.h
	$(call Build/Configure/Default)
endef

WAVE_NOSTDINC:= \
	$(KERNEL_NOSTDINC_FLAGS) \
	-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211-backport \
	-I$(STAGING_DIR)/usr/include/mac80211/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211 \
	-include backport/autoconf.h -include backport/backport.h

# Keep the vendor log preprocessor in front of the native target compiler.
# The same staged wireless headers and symbol set are used by all three parts.
# Backports 7.2 crypto headers require anonymous struct members, as in its Kbuild.
# Collect errors from independent objects; any failed object still fails the module.
WAVE_KBUILD=$(KERNEL_MAKE_FLAGS) -k \
	CC="$(PKG_BUILD_DIR)/support/mtlkfront.sh $(TARGET_CC)" \
	NOSTDINC_FLAGS="$(WAVE_NOSTDINC)" \
	KCFLAGS="$(IREMAP_STAGING_DIR) -fms-extensions -Wno-error=missing-prototypes -Wno-error=old-style-declaration -Wno-error=enum-conversion -Wno-error=implicit-fallthrough"

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR)/build/tools/mtlkroot \
		KERNEL_MAKE_EXT_PARAMS='$(WAVE_KBUILD)' mtlkroot.ko
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR)/build/wireless/driver/rflib \
		KERNEL_MAKE_EXT_PARAMS='$(WAVE_KBUILD)' mtlk_rflib.a
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR)/build/wireless/driver \
		KERNEL_MAKE_EXT_PARAMS='$(WAVE_KBUILD)' mtlk.ko
endef

$(eval $(call KernelPackage,wave))
