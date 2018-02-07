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
GW_SVN_URL?=http://svn.gateworks.com/

all: octeontx imx6 cns3xxx

setup:
	@printf "Setting up OpenWrt build...\n"
	@if [ "$(DL_DIR)" -a -d "$(DL_DIR)" -a ! -e "$(OPENWRT)/dl" ]; then \
		printf "Linking $(shell pwd)/$(OPENWRT)/dl to $(DL_DIR)\n"; \
		ln -s $(DL_DIR) $(OPENWRT)/dl; \
	fi

	@printf "Updating Feeds...\n"
	@make -C $(OPENWRT) defconfig # first pkg feed update req's this
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

# Build for Newport Product Family
octeontx: setup
	$(call make-profile,$@)

# Build for Ventana Product Family
imx6: setup
	$(call make-profile,$@)

# Build for Laguna Product Family
cns3xxx: setup
	$(call make-profile,$@)

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
			../$(OPENWRT)/bin/targets/imx6/generic/gateworks-imx6-ventana-squashfs-nand.ubi \
			> ventana_normal.bin; \
		../scripts/mkimage_jtag \
			../bootloader/ventana/SPL \
			../bootloader/ventana/u-boot.img \
			../$(OPENWRT)/bin/targets/imx6/generic/gateworks-imx6-ventana-large-squashfs-nand.ubi \
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
