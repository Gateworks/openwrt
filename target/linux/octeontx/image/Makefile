# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2018 OpenWrt.org

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

define Image/Build/Initramfs
	$(CP) $(KERNEL_BUILD_DIR)/vmlinux $(BIN_DIR)/$(IMG_PREFIX)-vmlinux
	$(CP) $(KERNEL_BUILD_DIR)/vmlinux-initramfs $(BIN_DIR)/$(IMG_PREFIX)-vmlinux-initramfs
endef

define Image/Build
	$(call Image/Build/$(1))
	cp $(KDIR)/root.$(1) $(BIN_DIR)/$(IMG_PREFIX)-$(1).img
	cp $(KDIR)/vmlinux $(BIN_DIR)/$(IMG_PREFIX)-vmlinux
endef

define Build/boot-scr
	rm -f $@-boot.scr
	mkimage -A arm64 -O linux -T script -C none -a 0 -e 0 \
		-d bootscript-$(BOOT_SCRIPT) $@-boot.scr
endef

define Build/boot-img-ext4
	rm -fR $@.boot
	mkdir -p $@.boot
	$(foreach dts,$(DEVICE_DTS), $(CP) $(KDIR)/image-$(dts).dtb $@.boot/$(dts).dtb;)
	$(CP) $(IMAGE_KERNEL) $@.boot/$(KERNEL_NAME)
	-$(CP) $@-boot.scr $@.boot/boot.scr
	make_ext4fs -J -L kernel -l $(CONFIG_TARGET_KERNEL_PARTSIZE)M \
		$(if $(SOURCE_DATE_EPOCH),-T $(SOURCE_DATE_EPOCH)) \
		$@.bootimg $@.boot
endef

define Build/newport-firmware-image
	./gen_newport_img.sh $@ \
		$@.bootimg \
		$(IMAGE_ROOTFS) \
		$(CONFIG_TARGET_KERNEL_PARTSIZE) \
		$(CONFIG_TARGET_ROOTFS_PARTSIZE)
endef

define Device/gateworks_newport
  KERNEL_NAME := Image
  KERNEL := kernel-bin
  DEVICE_VENDOR := Gateworks
  DEVICE_MODEL := Newport
  BOOT_SCRIPT := gateworks_newport
  PARTITION_OFFSET := 16M
  DEVICE_PACKAGES := \
	kmod-hwmon-gsc kmod-gpio-pca953x kmod-rtc-ds1672 kmod-eeprom-at24 \
	kmod-gpio-button-hotplug kmod-leds-gpio kmod-pps-gpio \
	kmod-hwmon-max6642 \
	kmod-can kmod-can-mcp251x kmod-can-raw kmod-iio-st_accel-i2c
  SUPPORTED_DEVICES := \
	gw,gw610x \
	gw,gw620x \
	gw,gw630x \
	gw,gw640x \
	gw,gw6903
  IMAGES := img.gz
  IMAGE/img.gz := boot-scr | boot-img-ext4 | newport-firmware-image | gzip | append-metadata
endef
TARGET_DEVICES += gateworks_newport

$(eval $(call BuildImage))
