ARCH:=arm
BOARDNAME:=NXP i.MX with Cortex-A9
CPU_TYPE:=cortex-a9
CPU_SUBTYPE:=neon
KERNELNAME:=zImage dtbs
FEATURES+=ubifs

define Target/Description
	Build firmware images for NXP i.MX (Cortex-A9) based boards.
endef
