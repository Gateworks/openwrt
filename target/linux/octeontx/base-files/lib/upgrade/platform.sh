# Copyright (C) 2023 OpenWrt.org

enable_image_metadata_check() {
	case "$(board_name)" in
	gw,gw610x|\
	gw,gw620x|\
	gw,gw630x|\
	gw,gw640x|\
	gw,gw6903)
		REQUIRE_IMAGE_METADATA=1
		;;
	esac
}
enable_image_metadata_check

platform_check_image() {
	local board=$(board_name)

	case "$board" in
	gw,gw610x|\
	gw,gw620x|\
	gw,gw630x|\
	gw,gw640x|\
	gw,gw6903)
		# verify MBR boot signature
		header=$(get_image "$@" | dd bs=1 count=2 skip=510 2>/dev/null | hexdump -v -n 8 -e '1/1 "%02x"')
		[ "$header" != "55aa" ] && {
			echo "Failed to verify MBR boot signature."
			return 1
		}
		# verify CVM_CLIB flash header at 64k
		header=$(get_image "$@" | dd bs=1 count=8 skip=65536 2>/dev/null | hexdump -v -n 8 -e '1/1 "%c"')
		[ "$header" != "CVM_CLIB" ] && {
			echo "Failed to verify OCTEONTX boot image."
			return 1
		}
		# verify FAT16 P1 starting at 0x100000
		header=$(get_image "$@" | dd bs=1 count=5 skip=$((0x100000+54)) 2>/dev/null | hexdump -v -n 5 -e '1/1 "%c"')
		[ "$header" != "FAT16" ] && {
			echo "Failed to verify boot firmware partition."
			return 1
		}
		return 0
		;;
	esac

	echo "Sysupgrade is not yet supported on $board."
	return 1
}

platform_do_upgrade() {
	local diskdev

	case "$(board_name)" in
	gw,gw610x|\
	gw,gw620x|\
	gw,gw630x|\
	gw,gw640x|\
	gw,gw6903)
		export_bootdevice && export_partdevice diskdev 0 || {
			echo "Unable to find root device."
			return 1
		}
		v "Updating /dev/$diskdev..."
		get_image "$@" | dd of="/dev/$diskdev" bs=4096 conv=fsync
		;;
	esac
}

platform_copy_config() {
	local partdev

	case "$(board_name)" in
	gw,gw610x|\
	gw,gw620x|\
	gw,gw630x|\
	gw,gw640x|\
	gw,gw6903)
		export_partdevice partdev 2 && {
			v "Storing $UPGRADE_BACKUP on /dev/$partdev..."
			mount -o rw,noatime "/dev/$partdev" /mnt
			cp -af "$UPGRADE_BACKUP" "/mnt/$BACKUP_FILE"
			umount /mnt
		}
		;;
	esac
}
