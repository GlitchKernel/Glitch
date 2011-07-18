#!/bin/bash

# Default CDMA Kernel building (using fascinatemtd)

	export KBUILD_BUILD_VERSION="1"

# kernel option changes
OPTS="CONFIG_NETFILTER_XT_MATCH_MULTIPORT \
CONFIG_SYN_COOKIES \
CONFIG_IP_ADVANCED_ROUTER \
CONFIG_NLS_UTF8 \
CONFIG_TIMER_STATS \
CONFIG_MODVERSIONS
"
OPTSOFF="CONFIG_TREE_RCU \
CONFIG_LOCALVERSION_AUTO \
PHONET \
CONFIG_MAGIC_SYSRQ \
CONFIG_DEBUG_FS \
CONFIG_DETECT_HUNG_TASK \
CONFIG_SCHED_DEBUG \
CONFIG_DEBUG_RT_MUTEXES \
CONFIG_DEBUG_SPINLOCK \
CONFIG_DEBUG_MUTEXES \
CONFIG_DEBUG_SPINLOCK_SLEEP \
CONFIG_DEBUG_BUGVERBOSE \
CONFIG_DEBUG_INFO \
CONFIG_FTRACE \
CONFIG_STACKTRACE \
CONFIG_STACKTRACE_SUPPORT
"
OPTNEWVAL=""
RELVER=$(($(cat .relver)+1))

echo "copying config for FASCINATE-OLDMODEM"
cp arch/arm/configs/aries_galaxysmtd_defconfig .config

echo "Enabling extra config options..."
for o in $OPTS; do
#check if option is already present
egrep -q ^${o} .config || {
echo "+ ${o} "
#check if option exists (if so, replace)
grep -q "\# ${o} is not set" .config
if [[ $? -eq 0 ]]; then
sed -i "s/\#\ ${o}\ is\ not\ set/${o}=y/" .config
else
echo "${o}=y" >> .config
fi
}
done

echo "Changing some config values..."
for o in $OPTNEWVAL; do
echo "~ ${o}"
c=$(echo ${o}|cut -d '=' -f 1)
sed -i "s/^${c}=[^*]*$/${o}/" .config
done

echo "Disabling some config options..."
for o in $OPTSOFF; do
echo "- ${o}"
sed -i "s/^${o}=[y|m]$/\# ${o}\ is\ not\ set/" .config
done

if [ -f arch/arm/mach-s5pv210/mach-aries.c_backup ]
then
mv arch/arm/mach-s5pv210/mach-aries.c arch/arm/mach-s5pv210/mach-aries.c_telus
mv arch/arm/mach-s5pv210/mach-aries.c_backup arch/arm/mach-s5pv210/mach-aries.c
echo " "
echo "Found mach-aries.c_backup from failed Telus building"
echo "Switching files for clean build"
echo " "
fi

echo "building kernel"
make -j8

echo "creating boot.img"
../../../device/samsung/aries-common/mkshbootimg.py release/boot.img arch/arm/boot/zImage ../../../out/target/product/galaxysmtd/ramdisk.img ../../../out/target/product/galaxysmtd/ramdisk-recovery.img

# DOIT

[[ -d release ]] || {
	echo "must be in kernel root dir"
	exit 1;
}

echo "packaging it up"

TYPE=$1
[[ "$TYPE" == '' ]] && TYPE=CDMA

cd release && {

mkdir -p ${TYPE}_OLDMODEM || exit 1

REL=CM7${TYPE}-Glitch-DEV-$(date +%Y%m%d_%H%M)-OLDMODEM.zip

	rm -r system 2> /dev/null
	mkdir  -p system/lib/modules || exit 1
	mkdir  -p system/lib/hw || exit 1
	mkdir  -p system/etc/init.d || exit 1
	cp logger.module system/lib/modules/logger.ko
	cd ../
		find . -name "*.ko" -exec cp {} release/system/lib/modules/ \; 2>/dev/null || exit 1
	cd release
	cp 90screenstate_scaling system/etc/init.d/ || exit 1
	cp lights.aries.so system/lib/hw/ || exit 1
	cp logcat_module system/etc/init.d/ || exit 1
	mkdir -p system/bin
	cp bin/* system/bin/
	zip -q -r ${REL} system boot.img META-INF script bml_over_mtd bml_over_mtd.sh || exit 1
	sha256sum ${REL} > ${REL}.sha256sum
	mv ${REL}* ${TYPE} || exit 1
} || exit 1

rm system/lib/modules/*
rm system/lib/hw/*
rm system/etc/init.d/*
echo ${REL}
exit 0
