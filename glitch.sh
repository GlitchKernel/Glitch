#!/bin/bash

# CM9 repo path :
repo=~/CM9

# Glitch kernel build-script parameters :
#
# "device" : build for a supported device (galaxys, captivate, fascinate, vibrant).
# You can list all devices you want to build, separated by a space.
#
# full : build everything (initramfs, 443 builds for gsm & cdma, all devices).
# clean : clean the build directory.

export CM9_REPO=$repo

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

CROSS_PREFIX=$CROSS_PREFIX_GLITCH

}

build ()
{

tempmodem=$repo/kernel/samsung/glitch-build/kernel/$target/drivers/misc/samsung_modemctl
temptvout=$repo/kernel/samsung/glitch-build/kernel/$target/drivers/media/video/samsung/tv20
MODEM_DIR=$repo/kernel/samsung/glitch-build/modem

    local target=$target
    echo "Building for $target"
    local target_dir="$BUILD_DIR/$target"
    local module
    rm -fr "$target_dir"
    mkdir -p "$target_dir/usr"
    cp "$KERNEL_DIR/usr/"*.list "$target_dir/usr"
    mkdir -p "$tempmodem/modemctl"
    mkdir -p "$temptvout"

echo "Copying 443 built-in files ..."
if [ "$target" = fascinate ] ; then
    cp $MODEM_DIR/built-in.443cdma_samsung_modemctl $tempmodem/built-in.o
else
    cp $MODEM_DIR/built-in.443gsm_samsung_modemctl $tempmodem/built-in.o
    cp $MODEM_DIR/built-in.443gsm_modemctl $tempmodem/modemctl/built-in.o
fi
    cp $MODEM_DIR/built-in.443_tvout $temptvout/built-in.o

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

# Backup as target for ramdisks

#$repo/device/samsung/aries-common/mkshbootimg.py $KERNEL_DIR/release/boot.img "$target_dir"/arch/arm/boot/zImage $KERNEL_DIR/release/auto/Glitch-Ramdisks/$target/ramdisk.img $KERNEL_DIR/release/auto/Glitch-Ramdisks/$target/ramdisk-recovery.img

echo "packaging it up"

cd release && {

mkdir -p ${target} || exit 1

REL=CM9-${target}-Glitch-$(date +%Y%m%d.%H%M).zip

	rm -r system 2> /dev/null
	mkdir  -p system/lib/modules || exit 1
	mkdir  -p system/etc/init.d || exit 1
	mkdir  -p system/etc/glitch-config || exit 1
	echo "inactive" > system/etc/glitch-config/screenstate_scaling || exit 1
	echo "conservative" > system/etc/glitch-config/sleep_governor || exit 1
	cp logger.module system/lib/modules/logger.ko
	for module in "${MODULES[@]}" ; do
		cp "$target_dir/$module" \; 2>/dev/null
	done
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
	mv ${REL}* $KERNEL_DIR/release/${target}/ || exit 1
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

if [ "$1" = full ] ; then
echo "Building ALL variants of all kernels!"

echo "Building initramfs"
./initramfs.sh

echo "Building 443 versions .."
./443glitch.sh cdma
./443glitch.sh gsm

echo "Building CDMA variant(s) .. "
./glitch.sh fascinate

echo "Building GSM variants .. "
./glitch.sh captivate
./glitch.sh galaxys
./glitch.sh vibrant
    exit 0
fi

targets=("$@"mtd)
if [ 0 = "${#targets[@]}" ] ; then
    targets=(captivate fascinate galaxys vibrant)
fi

START=$(date +%s)

for target in "${targets[@]}" ; do 
    build $target
done

END=$(date +%s)
ELAPSED=$((END - START))
E_MIN=$((ELAPSED / 60))
E_SEC=$((ELAPSED - E_MIN * 60))
printf "Elapsed: "
[ $E_MIN != 0 ] && printf "%d min(s) " $E_MIN
printf "%d sec(s)\n" $E_SEC
