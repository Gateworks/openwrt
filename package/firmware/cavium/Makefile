#
# Copyright (C) 2019 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=cavium-cpt-firmware
PKG_RELEASE:=1


include $(INCLUDE_DIR)/package.mk

define Download/cavium-cpt-firmware-ae
  FILE:=cpt8x-mc-ae.out
  URL:=http://dev.gateworks.com/images/
  HASH:=39063984d5f47e997ec2f15cef87394d719501e53ed121e0373161b86e72ad22
endef

define Download/cavium-cpt-firmware-se
  FILE:=cpt8x-mc-se.out
  URL:=http://dev.gateworks.com/images/
  HASH:=7927b69eb693b7de67a3cc867083c1f5a3f2c3c586cfa11a542e6551091f7cc7
endef

define Package/cavium-cpt-firmware
  TITLE:=Firmware for Cavium CPT Crypto
  SECTION:=firmware
  CATEGORY:=Firmware
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Compile

endef

define Package/cavium-cpt-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/
	$(INSTALL_DATA) $(DL_DIR)/cpt8x-mc-ae.out $(1)/lib/firmware/
	$(INSTALL_DATA) $(DL_DIR)/cpt8x-mc-se.out $(1)/lib/firmware/
endef

$(eval $(call BuildPackage,cavium-cpt-firmware))
$(eval $(call Download,cavium-cpt-firmware-ae))
$(eval $(call Download,cavium-cpt-firmware-se))
