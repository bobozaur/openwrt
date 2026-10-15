# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=wave
PKG_VERSION:=6.0.8.87.7
PKG_RELEASE:=6
PKG_SOURCE_VERSION:=06.00.08.00.87.00.07.d24084bd3447
WAVE_SOURCE_BASE:=lq-wave-300-$(PKG_SOURCE_VERSION)
PKG_SOURCE:=$(WAVE_SOURCE_BASE).gpl.wls.src.tar.bz2
PKG_SOURCE_URL:=file://$(DL_DIR)
PKG_SOURCE_PROTO:=wave-local
PKG_HASH:=c9dcd2b8a97d5cbc303303891daffbedb43acd918971175d980eca9e85212be4
PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(WAVE_SOURCE_BASE).wls.src
PKG_BUILD_DEPENDS:=mac80211
PKG_EXTMOD_SUBDIRS:=build/tools/mtlkroot/linux build/wireless/driver/linux
PKG_FIXUP:=autoreconf
PKG_AUTOMAKE_PATHS:=.
# The vendor log preprocessor updates shared generated files without locks.
PKG_BUILD_PARALLEL:=0
# The GPL archive offers GPLv2/BSD. The separately supplied RF archive also
# carries an AGREEMENT referring to separate terms; its scope is unresolved.
# Do not describe the combined module as wholly GPL or mirror its RF input.
PKG_LICENSE:=LicenseRef-WAVE
PKG_LICENSE_FILES:=LICENSE wireless/driver/rflib/AGREEMENT

STAMP_CONFIGURED_DEPENDS:=$(STAGING_DIR)/usr/include/mac80211-backport/backport/autoconf.h

include $(INCLUDE_DIR)/package.mk

# Missing restricted inputs must not fall back to public download mirrors.
# Existing inputs are hash-checked again at the extraction boundary below.
define DownloadMethod/wave-local
	echo 'Place $(FILE) in $(DL_DIR) (SHA256 $(HASH))'; false
endef

define Download/wave-rflib
  FILE:=$(WAVE_SOURCE_BASE).rflib.wls.src.tar.bz2
  URL:=file://$(DL_DIR)
  PROTO:=wave-local
  MIRROR:=0
  HASH:=c389f01f140b7cd6ed62b59a91eb0ad7aa51d9651763443860924fa5019d6745
endef
$(eval $(call Download,wave-rflib))

define KernelPackage/wave
  SUBMENU:=Wireless Drivers
  TITLE:=Intel WAVE600 wireless support
  DEPENDS:=@TARGET_lantiq_grx500 +kmod-mac80211 +kmod-crypto-arc4 +@DRIVER_11AC_SUPPORT +@DRIVER_11AX_SUPPORT +wave600-firmware
  EXTRA_DEPENDS+=,wave600-firmware (>=06.00.08.00.87-r1)
  FILES:= \
	$(PKG_BUILD_DIR)/build/tools/mtlkroot/mtlkroot.ko \
	$(PKG_BUILD_DIR)/build/wireless/driver/mtlk.ko
  AUTOLOAD:=$(call AutoLoad,50,mtlkroot mtlk)
  MODPARAMS.mtlk:=fastpath=0 dual_pci=0 rcvry_on=0 ahb_off=1 loggersid=255
endef

define KernelPackage/wave/description
 Intel WAVE600 driver using the standard cfg80211/mac80211 backports API.
 Place the matching GPL and RF source archives from TP-Link's Archer AX50 v1
 GPL release in the OpenWrt download directory. RF source licensing and radio
 firmware redistribution must be resolved before distributing a build.
endef

define Build/Prepare
	$(MKHASH) sha256 $(DL_DIR)/$(PKG_SOURCE) | grep -qx $(PKG_HASH)
	$(MKHASH) sha256 $(DL_DIR)/$(WAVE_SOURCE_BASE).rflib.wls.src.tar.bz2 | \
		grep -qx c389f01f140b7cd6ed62b59a91eb0ad7aa51d9651763443860924fa5019d6745
	mkdir -p $(PKG_BUILD_DIR)
	$(TAR) -xjf $(DL_DIR)/$(PKG_SOURCE) -C $(PKG_BUILD_DIR) --strip-components=1
	$(TAR) -xjf $(DL_DIR)/$(WAVE_SOURCE_BASE).rflib.wls.src.tar.bz2 -C $(PKG_BUILD_DIR) --strip-components=1
	$(Build/Patch)
	$(CP) ./files/openwrt.env $(PKG_BUILD_DIR)/support/
	$(CP) ./files/wave_pmf.[ch] $(PKG_BUILD_DIR)/wireless/driver/linux/
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
