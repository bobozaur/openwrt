# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=wave600-firmware
PKG_VERSION:=1.0.6.106
PKG_RELEASE:=1
PKG_LICENSE:=LicenseRef-NETGEAR-RAX40-Firmware LicenseRef-TP-Link-Archer-AX50-Firmware
PKGARCH:=all

NETGEAR_ARCHIVE:=RAX40-V1.0.6.106_1.0.1.zip
NETGEAR_IMAGE_HASH:=f05acc7e03be14014c70746c896fcf6d0a4a00a95a9f12797700c2b1c75be417
AX50_ARCHIVE:=Archer_AX50_V1_251022.zip
AX50_IMAGE_HASH:=cf887d15c65108ed6bfcd9bd26d5dcde93dc2a6544cf6795260ff57d60f07119

include $(INCLUDE_DIR)/package.mk

define Download/netgear
  FILE:=$(NETGEAR_ARCHIVE)
  URL:=https://www.downloads.netgear.com/files/GDC/RAX40
  HASH:=e91470e1d264536edaf06732de8c552753908e247c03f98f769faf9a5eb016cf
endef
$(eval $(call Download,netgear))

define Download/ax50
  FILE:=$(AX50_ARCHIVE)
  URL:=https://static.tp-link.com/upload/firmware/2025/202511/20251121
  URL_FILE:=Archer%20AX50_V1_251022.zip
  HASH:=755156fc9897092189f551107fd05adff92439cd8039837b559d0002e8cf918f
endef
$(eval $(call Download,ax50))

define Package/wave600-firmware
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Intel WAVE600 WRX600 A0 firmware
  DEPENDS:=@TARGET_lantiq_grx500
  CONFLICTS:=wave600-firmware-importer
endef

define Package/wave600-firmware/description
 Original NETGEAR RAX40 V1 1.0.6.106 radio firmware with the Archer AX50 V1
 1.1.2 Build 20251022 PSD. The build downloads the pinned vendor archives,
 verifies their images and complete nine-file cohort, and includes the
 original files in the image. No runtime download or firmware store is needed.
 Vendor firmware terms remain applicable; the LicenseRef markers grant no rights.
endef

# unzip is an ordinary host prerequisite. The standard tools build supplies
# unsquashfs4 with the XZ support needed by both pinned vendor images.
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)/firmware
	unzip -p $(DL_DIR)/$(AX50_ARCHIVE) '*.bin' > $(PKG_BUILD_DIR)/ax50.bin
	[ "$$$$($(MKHASH) sha256 $(PKG_BUILD_DIR)/ax50.bin)" = "$(AX50_IMAGE_HASH)" ]
	$(STAGING_DIR_HOST)/bin/unsquashfs4 -cat -processors 1 -o 286889 \
		$(PKG_BUILD_DIR)/ax50.bin lib/firmware/PSD.bin > $(PKG_BUILD_DIR)/firmware/PSD.bin
	rm $(PKG_BUILD_DIR)/ax50.bin
	unzip -p $(DL_DIR)/$(NETGEAR_ARCHIVE) '*.img' > $(PKG_BUILD_DIR)/netgear.img
	[ "$$$$($(MKHASH) sha256 $(PKG_BUILD_DIR)/netgear.img)" = "$(NETGEAR_IMAGE_HASH)" ]
	set -e; while read -r hash file; do \
		[ "$$$$file" = PSD.bin ] || \
			$(STAGING_DIR_HOST)/bin/unsquashfs4 -cat -processors 1 -o 209 \
				$(PKG_BUILD_DIR)/netgear.img "opt/lantiq/wave/images/$$$$file" \
				> "$(PKG_BUILD_DIR)/firmware/$$$$file"; \
	done < ./files/SHA256SUMS
	rm $(PKG_BUILD_DIR)/netgear.img
	set -e; while read -r hash file; do \
		[ "$$$$($(MKHASH) sha256 "$(PKG_BUILD_DIR)/firmware/$$$$file")" = "$$$$hash" ]; \
	done < ./files/SHA256SUMS
endef

Build/Configure:=
Build/Compile:=

define Package/wave600-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware $(1)/lib/preinit $(1)/usr/share/$(PKG_NAME)
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/firmware/*.bin $(1)/lib/firmware/
	$(INSTALL_DATA) ./files/SHA256SUMS $(1)/usr/share/$(PKG_NAME)/
	$(INSTALL_DATA) ./files/81_wave600_firmware $(1)/lib/preinit/
endef

$(eval $(call BuildPackage,wave600-firmware))
