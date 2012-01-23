#!/bin/bash

# CM9 repo path :
repo=~/CM9

# Glitch kernel "443" build-script parameters :
#
# gsm : build for gsm.
# cdma : build for cdma.
# clean : clean the build directory.

export CM9_REPO=$repo

glitch443=$repo/kernel/samsung/glitch-build/kernel/CDMA-GSM
MODEM_DIR=$repo/kernel/samsung/glitch-build/modem

source ./release/auto/setup.sh
verify_toolchain

setup ()
{
    if [ x = "x$CM9_REPO" ] ; then
        echo "Android build environment must be configured"
        exit 1
    fi
    . "$CM9_REPO"/build/envsetup.sh

    KERNEL_DIR="$(dirname "$(readlink -f "$0")")"
    BUILD_DIR="../glitch-build/kernel"
    MODULES=("crypto/ansi_cprng.ko" "crypto/md4.ko" "drivers/media/video/gspca/gspca_main.ko" "fs/cifs/cifs.ko" "fs/fuse/fuse.ko" "fs/nls/nls_utf8.ko")

    if [ x = "x$NO_CCACHE" ] && ccache -V &>/dev/null ; then
        CCACHE=ccache
        CCACHE_BASEDIR="$KERNEL_DIR"
        CCACHE_COMPRESS=1
        CCACHE_DIR="$BUILD_DIR/.ccache"
        export CCACHE_DIR CCACHE_COMPRESS CCACHE_BASEDIR
    else
        CCACHE=""
    fi

CROSS_PREFIX=$CROSS_PREFIX_443

}

build ()
{

formodules=$repo/kernel/samsung/glitch-build/kernel/CDMA-GSM

    local target="$1"
    echo "Building for $target"
    local target_dir="$BUILD_DIR/CDMA-GSM"
    local module
    rm -fr "$target_dir"
    mkdir -p "$target_dir/usr"
    cp "$KERNEL_DIR/usr/"*.list "$target_dir/usr"
    sed "s|usr/|$KERNEL_DIR/usr/|g" -i "$target_dir/usr/"*.list
    mka -C "$KERNEL_DIR" O="$target_dir" aries_${target}_defconfig HOSTCC="$CCACHE gcc"
    mka -C "$KERNEL_DIR" O="$target_dir" HOSTCC="$CCACHE gcc" CROSS_COMPILE="$CCACHE $CROSS_PREFIX" zImage modules

[[ -d release ]] || {
	echo "must be in kernel root dir"
	exit 1;
}

echo "creating boot.img"

# CM9 repo as target for ramdisks

$repo/device/samsung/aries-common/mkshbootimg.py $KERNEL_DIR/release/boot.img "$target_dir"/arch/arm/boot/zImage $repo/out/target/product/$target/ramdisk.img $repo/out/target/product/$target/ramdisk-recovery.img

# Backup (Glitch kernel source) as target for ramdisks

#$repo/device/samsung/aries-common/mkshbootimg.py $KERNEL_DIR/release/boot.img "$target_dir"/arch/arm/boot/zImage $KERNEL_DIR/release/auto/Glitch-Ramdisks/$target/ramdisk.img $KERNEL_DIR/release/auto/Glitch-Ramdisks/$target/ramdisk-recovery.img

echo "packaging it up"

cd release && {

mkdir -p CDMA-GSM || exit 1

REL=CM9-$target-443-Glitch-$(date +%Y%m%d.%H%M).zip

	rm -r system 2> /dev/null
	mkdir  -p system/lib/modules || exit 1
	mkdir  -p system/etc/init.d || exit 1
	mkdir  -p system/etc/glitch-config || exit 1
	echo "inactive" > system/etc/glitch-config/screenstate_scaling || exit 1
	echo "conservative" > system/etc/glitch-config/sleep_governor || exit 1

	# Copying modules
	cp logger.module system/lib/modules/logger.ko
	cp $formodules/crypto/ansi_cprng.ko system/lib/modules/ansi_cprng.ko
	cp $formodules/crypto/md4.ko system/lib/modules/md4.ko
	cp $formodules/drivers/media/video/gspca/gspca_main.ko system/lib/modules/gspca_main.ko
	cp $formodules/fs/cifs/cifs.ko system/lib/modules/cifs.ko
	cp $formodules/fs/fuse/fuse.ko system/lib/modules/fuse.ko
	cp $formodules/fs/nls/nls_utf8.ko system/lib/modules/nls_utf8.ko

	cp S99screenstate_scaling system/etc/init.d/ || exit 1
	cp 90call_vol system/etc/init.d/ || exit 1
	cp logcat_module system/etc/init.d/ || exit 1
	mkdir -p system/bin
	cp bin/* system/bin/
	
	# optional folders to go into system
	if [ -d app ]; then
		cp -r app system || exit 1
	fi
	
	# stuff in vendor (apparently) overrides other stuff
	if [ -d vendor ]; then
		cp -r vendor system || exit 1
	fi
	
	if [ -d lib ]; then
		cp -r lib system || exit 1
	fi
	
	zip -q -r ${REL} system boot.img META-INF script bml_over_mtd bml_over_mtd.sh || exit 1
	sha256sum ${REL} > ${REL}.sha256sum
	mv ${REL}* $KERNEL_DIR/release/CDMA-GSM/ || exit 1
} || exit 1

rm boot.img
rm system/lib/modules/*
rm system/etc/init.d/*
echo ${REL}

}
    
setup

if [ "$1" = clean ] ; then
    rm -fr "$BUILD_DIR"/*
    echo "Old build cleaned"
    exit 0
fi

START=$(date +%s)

if [ "$1" = "gsm" ] ; then

	if test -s Makefile_backup -a -s arch/arm/Makefile_backup -a -d drivers/misc/samsung_modemctl_backup -a -d drivers/media/video/samsung/tv20_backup; then

		mv Makefile Makefile_opti
		mv Makefile_backup Makefile

		mv arch/arm/Makefile arch/arm/Makefile_opti
		mv arch/arm/Makefile_backup arch/arm/Makefile

		mv drivers/misc/samsung_modemctl drivers/misc/samsung_modemctl_opti
		mv drivers/misc/samsung_modemctl_backup drivers/misc/samsung_modemctl

		mv drivers/media/video/samsung/tv20 drivers/media/video/samsung/tv20_opti
		mv drivers/media/video/samsung/tv20_backup drivers/media/video/samsung/tv20

		mv drivers/usb/host/s3c-otg drivers/usb/host/s3c-otg_opti
		mv drivers/usb/host/s3c-otg_backup drivers/usb/host/s3c-otg

		echo "Switching done. Building..."
		echo ""

		build galaxysmtd

	echo "Building done. Reswitching..." && {

		mv $KERNEL_DIR/Makefile $KERNEL_DIR/Makefile_backup
		mv $KERNEL_DIR/Makefile_opti $KERNEL_DIR/Makefile

		mv $KERNEL_DIR/arch/arm/Makefile $KERNEL_DIR/arch/arm/Makefile_backup
		mv $KERNEL_DIR/arch/arm/Makefile_opti $KERNEL_DIR/arch/arm/Makefile

		mv $glitch443/drivers/misc/samsung_modemctl/built-in.o $MODEM_DIR/built-in.443gsm_samsung_modemctl
		mv $glitch443/drivers/misc/samsung_modemctl/modemctl/built-in.o $MODEM_DIR/built-in.443gsm_modemctl

		mv $KERNEL_DIR/drivers/misc/samsung_modemctl $KERNEL_DIR/drivers/misc/samsung_modemctl_backup
		mv $KERNEL_DIR/drivers/misc/samsung_modemctl_opti $KERNEL_DIR/drivers/misc/samsung_modemctl

		mv $glitch443/drivers/media/video/samsung/tv20/built-in.o $MODEM_DIR/built-in.443_tvout

		mv $KERNEL_DIR/drivers/media/video/samsung/tv20 $KERNEL_DIR/drivers/media/video/samsung/tv20_backup
		mv $KERNEL_DIR/drivers/media/video/samsung/tv20_opti $KERNEL_DIR/drivers/media/video/samsung/tv20

		mv $glitch443/drivers/usb/host/s3c-otg/built-in.o $MODEM_DIR/built-in.443_usbhost

		mv $KERNEL_DIR/drivers/usb/host/s3c-otg $KERNEL_DIR/drivers/usb/host/s3c-otg_backup
		mv $KERNEL_DIR/drivers/usb/host/s3c-otg_opti $KERNEL_DIR/drivers/usb/host/s3c-otg
		}

	echo "Done! now preparing for next build..." && {

rm -fr "$BUILD_DIR"/*
echo "Old build cleaned"

if [ -f $MODEM_DIR/built-in.443gsm_samsung_modemctl ]
then
echo "Built-in.o modem files for GSM copied"
else
echo "***** built-in.443gsm files are missing *****"
echo "******** Please build old GSM *********"
fi

if [ -f $MODEM_DIR/built-in.443_tvout ]
then
echo "Built-in.o tv-out file copied"
else
echo "***** built-in.443_tvout file is missing *****"
echo "******** Please build old tv-out *********"
fi

if [ -f $MODEM_DIR/built-in.443_usbhost ]
then
echo "Built-in.o usbhost file copied"
else
echo "***** built-in.443_usbhost file is missing *****"
echo "******** Please build old tv-out *********"
fi
	}

else

	echo "***********************************************"
	echo "There's a backup missing, if needed, reverting changes to prepare rebuild.. "
	echo "***********************************************"
fi

if test -s $KERNEL_DIR/Makefile_opti -a -s $KERNEL_DIR/arch/arm/Makefile_opti -a -d $KERNEL_DIR/drivers/misc/samsung_modemctl_opti; then

		mv $KERNEL_DIR/Makefile $KERNEL_DIR/Makefile_backup
		mv $KERNEL_DIR/Makefile_opti $KERNEL_DIR/Makefile 

		mv $KERNEL_DIR/arch/arm/Makefile $KERNEL_DIR/arch/arm/Makefile_backup
		mv $KERNEL_DIR/arch/arm/Makefile_opti $KERNEL_DIR/arch/arm/Makefile

		mv $KERNEL_DIR/drivers/misc/samsung_modemctl $KERNEL_DIR/drivers/misc/samsung_modemctl_backup
		mv $KERNEL_DIR/drivers/misc/samsung_modemctl_opti $KERNEL_DIR/drivers/misc/samsung_modemctl

		mv $KERNEL_DIR/drivers/media/video/samsung/tv20 $KERNEL_DIR/drivers/media/samsung/tv20_backup
		mv $KERNEL_DIR/drivers/media/video/samsung/tv20_opti $KERNEL_DIR/drivers/media/samsung/tv20

		mv $KERNEL_DIR/drivers/usb/host/s3c-otg $KERNEL_DIR/drivers/usb/host/s3c-otg_backup
		mv $KERNEL_DIR/drivers/usb/host/s3c-otg_opti $KERNEL_DIR/drivers/usb/host/s3c-otg

	echo "Let's restart the process"
	./443glitch.sh gsm

fi

else
		if [ "$1" = "cdma" ] ; then

			if test -s Makefile_backup -a -s arch/arm/Makefile_backup -a -d drivers/misc/samsung_modemctl_backup -a -d drivers/media/video/samsung/tv20_backup; then

		mv Makefile Makefile_opti
		mv Makefile_backup Makefile

		mv arch/arm/Makefile arch/arm/Makefile_opti
		mv arch/arm/Makefile_backup arch/arm/Makefile

		mv drivers/misc/samsung_modemctl drivers/misc/samsung_modemctl_opti
		mv drivers/misc/samsung_modemctl_backup drivers/misc/samsung_modemctl

		mv drivers/media/samsung/tv20 drivers/media/video/samsung/tv20_opti
		mv drivers/media/samsung/tv20_backup drivers/media/video/samsung/tv20

		mv drivers/usb/host/s3c-otg drivers/usb/host/s3c-otg_opti
		mv drivers/usb/host/s3c-otg_backup drivers/usb/host/s3c-otg

	echo "Switching done. Building..."
	echo ""

		build fascinatemtd

	echo "Building done. Reswitching..." && {

		mv $KERNEL_DIR/Makefile $KERNEL_DIR/Makefile_backup
		mv $KERNEL_DIR/Makefile_opti $KERNEL_DIR/Makefile

		mv $KERNEL_DIR/arch/arm/Makefile $KERNEL_DIR/arch/arm/Makefile_backup
		mv $KERNEL_DIR/arch/arm/Makefile_opti $KERNEL_DIR/arch/arm/Makefile

		mv $glitch443/drivers/misc/samsung_modemctl/built-in.o $MODEM_DIR/built-in.443cdma_samsung_modemctl

		mv $KERNEL_DIR/drivers/misc/samsung_modemctl $KERNEL_DIR/drivers/misc/samsung_modemctl_backup
		mv $KERNEL_DIR/drivers/misc/samsung_modemctl_opti $KERNEL_DIR/drivers/misc/samsung_modemctl

		mv $glitch443/drivers/media/video/samsung/tv20/built-in.o $MODEM_DIR/built-in.443_tvout

		mv $KERNEL_DIR/drivers/media/video/samsung/tv20 $KERNEL_DIR/drivers/media/samsung/tv20_backup
		mv $KERNEL_DIR/drivers/media/video/samsung/tv20_opti $KERNEL_DIR/drivers/media/samsung/tv20

		mv $glitch443/drivers/usb/host/s3c-otg/built-in.o $MODEM_DIR/built-in.443_usbhost

		mv $KERNEL_DIR/drivers/usb/host/s3c-otg $KERNEL_DIR/drivers/usb/host/s3c-otg_backup
		mv $KERNEL_DIR/drivers/usb/host/s3c-otg_opti $KERNEL_DIR/drivers/usb/host/s3c-otg
		}

	echo "Done! now preparing for next build..." && {

rm -fr "$BUILD_DIR"/*
echo "Old build cleaned"

if [ -f $MODEM_DIR/built-in.443cdma_samsung_modemctl ]
then
echo "Built-in.o modem files for CDMA copied"
else
echo "***** built-in.443cdma files are missing *****"
echo "******** Please build old CDMA *********"
fi

if [ -f $MODEM_DIR/built-in.443_tvout ]
then
echo "Built-in.o tv-out file copied"
else
echo "***** built-in.443_tvout file is missing *****"
echo "******** Please build old tv-out *********"
fi

if [ -f $MODEM_DIR/built-in.443_usbhost ]
then
echo "Built-in.o usbhost file copied"
else
echo "***** built-in.443_usbhost file is missing *****"
echo "******** Please build old tv-out *********"
fi
	}

else

	echo "***********************************************"
	echo "There's a backup missing, if needed, reverting changes to prepare rebuild.. "
	echo "***********************************************"

fi

if test -s $KERNEL_DIR/Makefile_opti -a -s $KERNEL_DIR/arch/arm/Makefile_opti -a -d $KERNEL_DIR/drivers/misc/samsung_modemctl_opti; then

		mv $KERNEL_DIR/Makefile $KERNEL_DIR/Makefile_backup
		mv $KERNEL_DIR/Makefile_opti $KERNEL_DIR/Makefile 

		mv $KERNEL_DIR/arch/arm/Makefile $KERNEL_DIR/arch/arm/Makefile_backup
		mv $KERNEL_DIR/arch/arm/Makefile_opti $KERNEL_DIR/arch/arm/Makefile

		mv $KERNEL_DIR/drivers/misc/samsung_modemctl $KERNEL_DIR/drivers/misc/samsung_modemctl_backup
		mv $KERNEL_DIR/drivers/misc/samsung_modemctl_opti $KERNEL_DIR/drivers/misc/samsung_modemctl

		mv $KERNEL_DIR/drivers/media/video/samsung/tv20 $KERNEL_DIR/drivers/media/samsung/tv20_backup
		mv $KERNEL_DIR/drivers/media/video/samsung/tv20_opti $KERNEL_DIR/drivers/media/samsung/tv20

		mv $KERNEL_DIR/drivers/usb/host/s3c-otg $KERNEL_DIR/drivers/usb/host/s3c-otg_backup
		mv $KERNEL_DIR/drivers/usb/host/s3c-otg_opti $KERNEL_DIR/drivers/usb/host/s3c-otg

	echo "Let's restart the process"
	./443glitch.sh cdma

		fi

	fi
fi

END=$(date +%s)
ELAPSED=$((END - START))
E_MIN=$((ELAPSED / 60))
E_SEC=$((ELAPSED - E_MIN * 60))
printf "Elapsed: "
[ $E_MIN != 0 ] && printf "%d min(s) " $E_MIN
printf "%d sec(s)\n" $E_SEC
