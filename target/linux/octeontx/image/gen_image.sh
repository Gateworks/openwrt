#!/bin/sh

set -x

OUTPUT="$1"
KERNELIMAGE="$2"
ROOTFSIMAGE="$3"

BOOTFW=http://dev.gateworks.com/newport/boot_firmware/firmware-newport.img
LOADADDR=0x20080000

rm -f "$OUTPUT"

hex_le32_to_cpu() {
	[ "$(echo 01 | hexdump -v -n 2 -e '/2 "%x"')" = "3031" ] && {
		echo "${1:0:2}${1:8:2}${1:6:2}${1:4:2}${1:2:2}"
		return
	}
	echo "$@"
}

# fetch latest boot firmware
wget -N $BOOTFW -O ${TMPDIR}/firmware-newport.img
cp ${TMPDIR}/firmware-newport.img .img

# create kernel.itb and .scr
gzip $KERNELIMAGE -c > .vmlinux.gz
mkits.sh \
	-A arm64 -a $LOADADDR -e $LOADADDR \
	-o .its -k .vmlinux.gz -C gzip \
	-v "OpenWrt" -c "config@1"
mkimage -f .its .itb
mkimage -T script -d ./bootscript_newport .scr

# get p1 ofset/size and extract it
set $(hexdump -v -n 12 -s $((0x1b2 + 1 * 16)) -e '3/4 "0x%08X "' .img)
ptype="$(( $(hex_le32_to_cpu $1) % 256))"
lba="$(( $(hex_le32_to_cpu $2) ))"
num="$(( $(hex_le32_to_cpu $3) ))"
[ $ptype -eq 1 ] || {
	echo "Error: invalid partition type in boot firmware"
	exit 1
}
dd if=.img of=.fatfs bs=512 skip=$lba count=$num

# inject kernel/scr
mcopy -i .fatfs .itb ::kernel.itb
mcopy -i .fatfs .scr ::newport.scr

# copy it back
dd if=.fatfs of=.img bs=512 seek=$lba

# concatenate rootfs and some padding (to avoid leaving behind old overlay fs)
(
	cat $ROOTFSIMAGE
	dd if=/dev/zero bs=128k count=1 2>/dev/null
) | dd of=.img bs=16M seek=1

# compress
gzip .img
mv .img.gz $OUTPUT

# cleanup
rm -f .vmlinux.gz .its .itb .scr .fatfs
