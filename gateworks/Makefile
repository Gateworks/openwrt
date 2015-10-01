# Require Shell Expansion
SHELL=/bin/bash -o pipefail

# Set DL_DIR To symlink to
DL_DIR?=/usr/src/dl

# Set make arguments
J_ARG?=-j$(shell grep processor /proc/cpuinfo | wc -l)
O_ARGS?=
MAKEARGS=$(J_ARG) $(O_ARGS)

# Set misc. variables
OPENWRT?=../
PROFILE?=
BUILD_BOOTLOADERS?=
GW_SVN_URL?=http://svn.gateworks.com/
IMX6_TOOLCHAIN?=toolchain-arm_cortex-a9+neon_gcc-5.2.0_musl-1.1.11_eabi
CNS3XXX_TOOLCHAIN?=toolchain-arm_mpcore+vfp_gcc-5.2.0_musl-1.1.11_eabi

all: imx6 cns3xxx

setup:
	@printf "Setting up OpenWrt build...\n"
	@if [ "$(DL_DIR)" -a -d "$(DL_DIR)" -a ! -e "$(OPENWRT)/dl" ]; then \
		printf "Linking $(shell pwd)/$(OPENWRT)/dl to $(DL_DIR)\n"; \
		ln -s $(DL_DIR) $(OPENWRT)/dl; \
	fi

	@printf "Updating Feeds...\n"
	@make -C $(OPENWRT) defconfig &>/dev/null # first pkg feed update req's this
	make -C $(OPENWRT)/ package/symlinks || test 0

define make-profile
	@mkdir -p logs
	@if [ -r "configs/$1/.config$(PROFILE)" ]; then \
		printf "Building $1\n"; \
		cp configs/$1/.config$(PROFILE) $(OPENWRT)/.config; \
		make -C $(OPENWRT) $(MAKEARGS) V=s 2>&1 | tee logs/build_$1.log; \
	else \
		printf "ERROR: '%s' not found\n" "$(shell pwd)/configs/$1/.config$(PROFILE)"; \
		exit 1; \
	fi
endef

# Build for Ventana Product Family
imx6: setup
	$(call make-profile,$@)

# Build for Laguna Product Family
cns3xxx: setup
	$(call make-profile,$@)

ifdef BUILD_BOOTLOADERS
# Build Ventana Product Family bootloader
bootloader/ventana:
	@printf "Fetching source for $@...\n"
	@mkdir -p bootloader
	git clone https://github.com/Gateworks/u-boot-imx6.git bootloader/ventana

bootloader/ventana/u-boot.img: bootloader/ventana
	@printf "Building $@...\n"
	( cd bootloader/ventana; \
		export STAGING_DIR=$$PATH:$$PWD/../../$(OPENWRT)/staging_dir; \
		PATH=$$STAGING_DIR/$(IMX6_TOOLCHAIN)/bin; \
		export CROSS_COMPILE=arm-openwrt-linux-; \
		make gwventana_config; \
		make $(MAKEARGS); \
	)

bootloader/ventana/SPL: bootloader/ventana/u-boot.img

bootloader/laguna:
	@printf "Fetching source for $@...\n"
	@mkdir -p bootloader
	svn co $(GW_SVN_URL)/laguna/u-boot-2008.10 $@

bootloader/laguna/u-boot_nor.bin: bootloader/laguna
	@printf "Building $@...\n"
	( cd bootloader/laguna; \
		export STAGING_DIR=$$PATH:$$PWD/../../$(OPENWRT)/staging_dir; \
		PATH=$$STAGING_DIR/$(CNS3XXX_TOOLCHAIN)/bin; \
		export CROSS_COMPILE=arm-openwrt-linux-; \
		make cavium_cns3000_config; \
		make $(MAKEARGS); \
		cp u-boot.bin u-boot_nor.bin; \
	)

bootloader/laguna/u-boot_spi.bin: bootloader/laguna
	@printf "Building $@...\n"
	( cd bootloader/laguna; \
		export STAGING_DIR=$$PATH:$$PWD/../../$(OPENWRT)/staging_dir; \
		PATH=$$STAGING_DIR/$(CNS3XXX_TOOLCHAIN)/bin; \
		make cavium_cns3000_spi_config; \
		export CROSS_COMPILE=arm-openwrt-linux-; \
		make $(MAKEARGS); \
		cp u-boot.bin u-boot_spi.bin; \
	)
else
.PHONY:bootloader/ventana/u-boot.img
bootloader/ventana/u-boot.img:
	rm -f bootloader/ventana/u-boot.img
	@printf "Fetching $@...\n"
	@mkdir -p bootloader/ventana
	svn export $(GW_SVN_URL)/ventana/images/u-boot.img \
			bootloader/ventana/u-boot.img

.PHONY:bootloader/ventana/SPL
bootloader/ventana/SPL:
	rm -f bootloader/ventana/SPL
	@printf "Fetching $@...\n"
	@mkdir -p bootloader/ventana
	svn export $(GW_SVN_URL)/ventana/images/SPL \
			bootloader/ventana/SPL


.PHONY:bootloader/laguna/u-boot_nor.bin
bootloader/laguna/u-boot_nor.bin:
	rm -f bootloader/laguna/u-boot_nor.bin
	@printf "Fetching $@...\n"
	@mkdir -p bootloader/laguna
	svn export $(GW_SVN_URL)/laguna/images/u-boot_nor.bin \
			bootloader/laguna/u-boot_nor.bin

.PHONY:bootloader/laguna/u-boot_spi.bin
bootloader/laguna/u-boot_spi.bin:
	rm -f bootloader/laguna/u-boot_spi.bin
	@printf "Fetching $@...\n"
	@mkdir -p bootloader/laguna
	svn export $(GW_SVN_URL)/laguna/images/u-boot_spi.bin \
			bootloader/laguna/u-boot_spi.bin
endif

# firmware images
.PHONY:images
images: images/ventana images/laguna

# JTAG firmware images for Ventana family
.PHONY:images/ventana
images/ventana: bootloader/ventana/SPL bootloader/ventana/u-boot.img
	@mkdir -p images
	(cd images; \
		../scripts/mkimage_jtag \
			../bootloader/ventana/SPL \
			../bootloader/ventana/u-boot.img \
			> ../bootloader/ventana/u-boot_spl.bin; \
		../scripts/mkimage_jtag \
			../bootloader/ventana/SPL \
			../bootloader/ventana/u-boot.img \
			../$(OPENWRT)/bin/imx6/openwrt-imx6-ventana-squashfs-nand_normal.ubi \
			> ventana_normal.bin; \
		../scripts/mkimage_jtag \
			../bootloader/ventana/SPL \
			../bootloader/ventana/u-boot.img \
			../$(OPENWRT)/bin/imx6/openwrt-imx6-ventana-squashfs-nand_large.ubi \
			> ventana_large.bin; \
	)

# JTAG firmware images for Laguna family
.PHONY:images/laguna
images/laguna: bootloader/laguna/u-boot_spi.bin bootloader/laguna/u-boot_nor.bin
	@mkdir -p images
	(cd images; \
		../scripts/makeimage laguna_nor \
			../bootloader/laguna/u-boot_nor.bin ../$(OPENWRT); \
		../scripts/makeimage laguna_spi \
			../bootloader/laguna/u-boot_spi.bin ../$(OPENWRT); \
	)

# clean all
.PHONY:dirclean
dirclean:
	@printf "Cleaning...\n"
	rm -rf bootloader
	rm -rf images
	rm -rf logs
