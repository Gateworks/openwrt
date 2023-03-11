#!/bin/sh

set -x
[ $# -eq 5 ] || {
    echo "SYNTAX: $0 <file> <bootfs image> <rootfs image> <bootfs size> <rootfs size>"
    exit 1
}

OUTPUT="$1"
BOOTFSIMAGE="$2"
ROOTFSIMAGE="$3"
BOOTFSSIZE="$4"
ROOTFSSIZE="$5"

FIRMWARE=$(dirname $OUTPUT)/firmware-newport.img
(cd $(dirname $OUTPUT); wget -N http://dev.gateworks.com/newport/boot_firmware/firmware-newport.img)

head=16
sect=63

# verify MBR boot signature
header=$(dd if=$FIRMWARE bs=1 count=2 skip=510 2>/dev/null | hexdump -v -n 8 -e '1/1 "%02x"')
[ "$header" != "55aa" ] && {
	echo "Failed to verify MBR boot signature."
	exit 1
}
# verify CVM_CLIB flash header at 64k
header=$(dd if=$FIRMWARE bs=1 count=8 skip=65536 2>/dev/null | hexdump -v -n 8 -e '1/1 "%c"')
[ "$header" != "CVM_CLIB" ] && {
	echo "Failed to verify OCTEONTX boot image."
	exit 1
}
# verify FAT16 P1 starting at 0x100000
header=$(dd if=$FIRMWARE bs=1 count=5 skip=$((0x100000+54)) 2>/dev/null | hexdump -v -n 5 -e '1/1 "%c"')
[ "$header" != "FAT16" ] && {
	echo "Failed to verify boot firmware partition."
	exit 1
}

# get p1 ofset/size and extract it
set $(hexdump -v -n 8 -s 454 -e '"%d "' $FIRMWARE)
lba="$1"
num="$2"
# create MBR partition table:
#  - keeping boot firmware P1 intact
#  - adding P2 bootfs
#  - adding P3 rootfs
set $(ptgen -o .mbr -h $head -s $sect -l 256 -a 2 -t 1 -p $((num/2))k@$((lba/2))k -t 83 -p ${BOOTFSSIZE}m@16m -t 83 -p ${ROOTFSSIZE}m ${SIGNATURE:+-S 0x$SIGNATURE})

BOOTOFFSET="$(($3 / 512))"
BOOTSIZE="$(($4 / 512))"
ROOTFSOFFSET="$(($5 / 512))"
ROOTFSSIZE="$(($6 / 512))"

# start with boot firmware
cp $FIRMWARE $OUTPUT
# copy in new MBR
dd of="$OUTPUT" bs=512 if=.mbr conv=notrunc
# copy in bootfs image
dd of="$OUTPUT" bs=512 if="$BOOTFSIMAGE" seek="$BOOTOFFSET" conv=notrunc
# zero out rootfs image (to avoid leaving behind old overlay fs)
dd of="$OUTPUT" bs=512 if=/dev/zero seek="$ROOTFSOFFSET" conv=notrunc count="$ROOTFSSIZE"
# copy in rootfs image
dd of="$OUTPUT" bs=512 if="$ROOTFSIMAGE" seek="$ROOTFSOFFSET" conv=notrunc

# cleanup
rm .mbr
