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

#   Arch-dependent definitions
    case `uname -s` in
        Darwin)
            KERNEL_DIR="$(dirname "$(greadlink -f "$0")")"
            CROSS_PREFIX="$ANDROID_BUILD_TOP/prebuilt/darwin-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-"
            ;;
        *)
            KERNEL_DIR="$(dirname "$(readlink -f "$0")")"
            CROSS_PREFIX=$CROSS_PREFIX_443
            ;;
    esac

    BUILD_DIR="../glitch-build/kernel"
    MODULES=("crypto/ansi_cprng.ko" "crypto/md4.ko" "drivers/media/video/gspca/gspca_main.ko" "fs/cifs/cifs.ko" "fs/fuse/fuse.ko" "fs/nls/nls_utf8.ko")

    if [ x = "x$NO_CCACHE" ] && ccache -V &>/dev/null ; then
        CCACHE=ccache
        CCACHE_BASEDIR="$KERNEL_DIR"
        CCACHE_COMPRESS=1
        CCACHE_DIR="$CM9_REPO/kernel/samsung/.ccache"
        export CCACHE_DIR CCACHE_COMPRESS CCACHE_BASEDIR
    else
        CCACHE=""
    fi

}

build ()
{

formodules=$repo/kernel/samsung/glitch-build/kernel/$target

    local target=$target
    echo "Building for $target"
    local target_dir="$BUILD_DIR/$target"
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

# CM9 repo as target for ramdisk and recovery

$repo/device/samsung/aries-common/mkshbootimg.py $KERNEL_DIR/release/boot.img "$target_dir"/arch/arm/boot/zImage $repo/out/target/product/$target/ramdisk.img $repo/out/target/product/$target/ramdisk-recovery.img

# CM9 repo as target for ramdisk, backup for recovery

#$repo/device/samsung/aries-common/mkshbootimg.py $KERNEL_DIR/release/boot.img "$target_dir"/arch/arm/boot/zImage $repo/out/target/product/$target/ramdisk.img $KERNEL_DIR/release/auto/Glitch-Ramdisks/$target/ramdisk-recovery.img

# Backup as target for ramdisk and recovery

#$repo/device/samsung/aries-common/mkshbootimg.py $KERNEL_DIR/release/boot.img "$target_dir"/arch/arm/boot/zImage $KERNEL_DIR/release/auto/Glitch-Ramdisks/$target/ramdisk.img $KERNEL_DIR/release/auto/Glitch-Ramdisks/$target/ramdisk-recovery.img

echo "packaging it up"

cd release && {

mkdir -p ${target} || exit 1

REL=CM9-${target}-Glitch-$(date +%Y%m%d.%H%M).zip

	rm -r system 2> /dev/null
	mkdir  -p system/lib/modules || exit 1
	mkdir  -p system/etc/init.d || exit 1
	mkdir  -p system/etc/glitch-config || exit 1
	mkdir  -p system/lib/hw || exit 1
	echo "inactive" > system/etc/glitch-config/screenstate_scaling || exit 1
	echo "conservative" > system/etc/glitch-config/sleep_governor || exit 1

	# Copying modules
	cp logger.module system/lib/modules/logger.ko
	cp $formodules/crypto/ansi_cprng.ko system/lib/modules/ansi_cprng.ko
	#cp $formodules/crypto/md4.ko system/lib/modules/md4.ko
	cp $formodules/drivers/media/video/gspca/gspca_main.ko system/lib/modules/gspca_main.ko
	#cp $formodules/fs/cifs/cifs.ko system/lib/modules/cifs.ko
	cp $formodules/fs/fuse/fuse.ko system/lib/modules/fuse.ko
	cp $formodules/fs/nls/nls_utf8.ko system/lib/modules/nls_utf8.ko

	cp S99screenstate_scaling system/etc/init.d/ || exit 1
	#cp S98rfkill_didle system/etc/init.d/ || exit 1

if [ "$target" = fascinatemtd ] ; then
	cp 90call_vol_fascinate system/etc/init.d/ || exit 1
else
	cp 90call_vol system/etc/init.d/ || exit 1
fi
	cp logcat_module system/etc/init.d/ || exit 1
	cp lights.aries.so.BLN system/lib/hw/lights.aries.so
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
rm system/lib/hw/*
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

time {

for target in "${targets[@]}" ; do 
    build $target
done

}
