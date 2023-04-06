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

define Build/sdcard-img-ext4
	SIGNATURE="$(IMG_PART_SIGNATURE)" \
	PARTOFFSET="$(PARTITION_OFFSET)" PADDING=1 \
		$(if $(filter $(1),efi),GUID="$(IMG_PART_DISKGUID)") $(SCRIPT_DIR)/gen_image_generic.sh \
		$@ \
		$(CONFIG_TARGET_KERNEL_PARTSIZE) $@.boot \
		$(CONFIG_TARGET_ROOTFS_PARTSIZE) $(IMAGE_ROOTFS) \
		256
endef

define Build/venice-boot-firmware
	wget http://dev.gateworks.com/venice/images/firmware-venice-$(SOC).img \
		-N -O $(BIN_DIR)/firmware-venice-$(SOC).img
	dd of=$@ if=$(BIN_DIR)/firmware-venice-$(SOC).img \
		bs=1k skip=$(SOC_BOOT_OFFSET_KB) seek=$(SOC_BOOT_OFFSET_KB) conv=notrunc
endef

define Device/Default
  KERNEL_NAME := Image
  KERNEL := kernel-bin
  DTS_DIR := $(LINUX_DIR)/arch/$(LINUX_KARCH)/boot/dts/freescale
endef

define Device/gateworks_venice
  $(call Device/Default)
  DEVICE_VENDOR := Gateworks
  DEVICE_MODEL := Venice
  BOOT_SCRIPT := gateworks_venice
  PARTITION_OFFSET := 16M
  DEVICE_DTS := \
	imx8mm-venice-gw71xx-0x \
	imx8mm-venice-gw72xx-0x \
	imx8mm-venice-gw73xx-0x \
	imx8mm-venice-gw7901 \
	imx8mm-venice-gw7902 \
	imx8mm-venice-gw7903 \
	imx8mm-venice-gw7904 \
	imx8mn-venice-gw7902
  DEVICE_PACKAGES := \
	kmod-hwmon-gsc kmod-rtc-ds1672 kmod-eeprom-at24 \
	kmod-gpio-button-hotplug kmod-leds-gpio kmod-pps-gpio \
	kmod-lan743x kmod-sky2 kmod-iio-st_accel-i2c \
	kmod-can kmod-can-flexcan kmod-can-mcp251x
  IMAGES := dtb img.gz
  IMAGE/dtb := install-dtb
  IMAGE/img.gz := boot-scr | boot-img-ext4 | sdcard-img-ext4 | venice-boot-firmware | gzip | append-metadata
endef

define Device/gateworks_venice_imx8mm
  SOC := imx8mm
  SUPPORTED_DEVICES := \
	gw,imx8mm-gw71xx-0x \
	gw,imx8mm-gw72xx-0x \
	gw,imx8mm-gw73xx-0x \
	gw,imx8mm-gw7901 \
	gw,imx8mm-gw7902 \
	gw,imx8mm-gw7903 \
	gw,imx8mm-gw7904
  SOC_BOOT_OFFSET_KB := 33
  DEVICE_VARIANT := IMX8MM eMMC
  $(call Device/gateworks_venice)
endef
TARGET_DEVICES += gateworks_venice_imx8mm

define Device/gateworks_venice_imx8mn
  SOC := imx8mn
  SUPPORTED_DEVICES := \
	gw,imx8mn-gw7902
  SOC_BOOT_OFFSET_KB := 32
  DEVICE_VARIANT := IMX8MN eMMC
  $(call Device/gateworks_venice)
endef
TARGET_DEVICES += gateworks_venice_imx8mn
