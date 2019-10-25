# Require Shell Expansion
SHELL=/bin/bash -o pipefail

# Set DL_DIR To symlink to
DL_DIR?=/usr/src/dl

# Set make arguments
J_ARG?=-j$(shell grep processor /proc/cpuinfo | wc -l)
O_ARGS?=
MAKEARGS=$(J_ARG) $(O_ARGS)

# Set misc. variables
PROFILE?=
GW_DEV_URL?=http://dev.gateworks.com/

all: imx6 octeontx

setup:
	@printf "Setting up OpenWrt build...\n"
	@if [ "$(DL_DIR)" -a -d "$(DL_DIR)" -a ! -e "../dl" ]; then \
		printf "Linking dl to $(DL_DIR)\n"; \
		ln -s $(DL_DIR) ../dl; \
	fi

	@printf "Updating Feeds...\n"
	@make -C ../ defconfig # first pkg feed update req's this
	make -C ../ package/symlinks || test 0

define make-profile
	@mkdir -p logs
	@if [ -r "configs/$1/diffconfig$(PROFILE)" ]; then \
		printf "Building $1\n"; \
		cp configs/$1/diffconfig$(PROFILE) ../.config; \
		make -C ../ defconfig; \
		make -C ../ $(MAKEARGS) V=s 2>&1 | tee logs/build_$1.log; \
	else \
		printf "ERROR: '%s' not found\n" "$(shell pwd)/configs/$1/.config$(PROFILE)"; \
		exit 1; \
	fi
endef

# Build for Ventana Product Family
imx6: setup
	$(call make-profile,$@)

# Build for Newport Product Family
octeontx: setup
	$(call make-profile,$@)

.PHONY:bootloader/ventana/u-boot.img
bootloader/ventana/u-boot.img:
	@printf "Fetching $@...\n"
	@mkdir -p bootloader/ventana
	(cd bootloader/ventana; \
	 wget -N $(GW_DEV_URL)/ventana/images/u-boot.img; \
	)

.PHONY:bootloader/ventana/SPL
bootloader/ventana/SPL:
	@printf "Fetching $@...\n"
	@mkdir -p bootloader/ventana
	(cd bootloader/ventana; \
	 wget -N $(GW_DEV_URL)/ventana/images/SPL; \
	)


# JTAG firmware images
.PHONY:images
images: images/ventana

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
			../../bin/targets/imx6/generic/gateworks-imx6-ventana-squashfs-nand.ubi \
			> ventana_normal.bin; \
		../scripts/mkimage_jtag \
			../bootloader/ventana/SPL \
			../bootloader/ventana/u-boot.img \
			../../bin/targets/imx6/generic/gateworks-imx6-ventana-large-squashfs-nand.ubi \
			> ventana_large.bin; \
		../scripts/makeimage-ventana-emmc \
			openwrt-ventana.img \
			../bootloader/ventana/SPL \
			../bootloader/ventana/u-boot.img \
			../../bin/targets/imx6/generic/gateworks-imx6-ventana-squashfs-bootfs.tar.gz \
			../../build_dir/target-arm_cortex-a9+neon_musl_eabi/linux-imx6/root.squashfs \
			16 \
			256 \
		&& gzip -f openwrt-ventana.img; \
	)

# clean all
.PHONY:dirclean
dirclean:
	@printf "Cleaning...\n"
	rm -rf bootloader
	rm -rf images
	rm -rf logs
