#
# Copyright (C) 2020 OpenWrt.org
#

get_magic_at() {
        local pos="$2"
        get_image "$1" | dd bs=1 count=2 skip="$pos" 2>/dev/null | hexdump -v -n 2 -e '1/1 "%02x"'
}

platform_export_bootdevice() {
	local cmdline bootdisk rootpart uuid blockdev uevent line class
	local MAJOR MINOR DEVNAME DEVTYPE

	if read cmdline < /proc/cmdline; then
		case "$cmdline" in
			*root=*)
				rootpart="${cmdline##*root=}"
				rootpart="${rootpart%% *}"
			;;
		esac

		case "$rootpart" in
			PARTUUID=[a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9]-[a-f0-9][a-f0-9])
				uuid="${rootpart#PARTUUID=}"
				uuid="${uuid%-[a-f0-9][a-f0-9]}"
				for blockdev in $(find /dev -type b); do
					# skip loop devs, partitions, and mmcblk*boot*
					case "${blockdev#/dev/}" in
						mmcblk[0-9]|[hsv]d[a-z]);;
						*) continue;;
					esac
					# check for disk uuid match
					set -- $(dd if=$blockdev bs=1 skip=440 count=4 2>/dev/null | hexdump -v -e '4/1 "%02x "')
					if [ "$4$3$2$1" = "$uuid" ]; then
						uevent="/sys/class/block/${blockdev##*/}/uevent"
						break
					fi
				done
			;;
			PARTUUID=????????-????-????-????-??????????02)
				uuid="${rootpart#PARTUUID=}"
				uuid="${uuid%02}00"
				for disk in $(find /dev -type b); do
					set -- $(dd if=$disk bs=1 skip=568 count=16 2>/dev/null | hexdump -v -e '8/1 "%02x "" "2/1 "%02x""-"6/1 "%02x"')
					if [ "$4$3$2$1-$6$5-$8$7-$9" = "$uuid" ]; then
						uevent="/sys/class/block/${disk##*/}/uevent"
						break
					fi
				done
			;;
			/dev/*)
				uevent="/sys/class/block/${rootpart##*/}/../uevent"
			;;
			0x[a-f0-9][a-f0-9][a-f0-9] | 0x[a-f0-9][a-f0-9][a-f0-9][a-f0-9] | \
			[a-f0-9][a-f0-9][a-f0-9] | [a-f0-9][a-f0-9][a-f0-9][a-f0-9])
				rootpart=0x${rootpart#0x}
				for class in /sys/class/block/*; do
					while read line; do
						export -n "$line"
					done < "$class/uevent"
					if [ $((rootpart/256)) = $MAJOR -a $((rootpart%256)) = $MINOR ]; then
						uevent="$class/../uevent"
					fi
				done
			;;
		esac

		if [ -e "$uevent" ]; then
			while read line; do
				export -n "$line"
			done < "$uevent"
			export BOOTDEV_MAJOR=$MAJOR
			export BOOTDEV_MINOR=$MINOR
			return 0
		fi
	fi

	return 1
}

platform_check_image() {
	local diskdev

	case "$(get_magic_at "$1" 510)" in
		55aa) ;;
		*)
			echo "Failed to verify MBR boot signature."
			return 1
		;;
	esac

	platform_export_bootdevice && export_partdevice diskdev 0 || {
		echo "Unable to determine upgrade device"
		return 1
	}

	return 0
}

platform_do_upgrade() {
	local diskdev

	platform_export_bootdevice && export_partdevice diskdev 0 || {
		echo "Unable to determine upgrade device"
		return 1
	}

	if [ -b "/dev/$diskdev" ]; then
		sync
		echo "Writing full disk image to /dev/$diskdev"
		get_image "$@" | dd of="/dev/$diskdev" bs=4096 conv=fsync
		sleep 1
	fi
}

platform_copy_config() {
	local partdev

	if export_partdevice partdev 1; then
		mount -t vfat -o rw,noatime "/dev/$partdev" /mnt
		cp -af "$UPGRADE_BACKUP" "/mnt/$BACKUP_FILE"
                umount /mnt
	fi
}
