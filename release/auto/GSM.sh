#!/bin/bash

# Default GSM Kernel building (using galaxysmtd)

	export KBUILD_BUILD_VERSION="1"

# kernel option changes
OPTS="CONFIG_NETFILTER_XT_MATCH_MULTIPORT \
CONFIG_SYN_COOKIES \
CONFIG_IP_ADVANCED_ROUTER \
CONFIG_NLS_UTF8 \
CONFIG_TIMER_STATS \
CONFIG_MODVERSIONS
"
OPTSOFF="CONFIG_MAGIC_SYSRQ \
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

echo "copying config for SGS-OLDMODEM"
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

echo "building kernel"
make -j8

echo "creating boot.img"
#../../../device/samsung/aries-common/mkshbootimg.py release/boot.img arch/arm/boot/zImage ../../../out/target/product/galaxysmtd/ramdisk.img ../../../out/target/product/galaxysmtd/ramdisk-recovery.img

../../../device/samsung/aries-common/mkshbootimg.py release/boot.img arch/arm/boot/zImage release/auto/root/galaxysmtd/ramdisk.img release/auto/root/galaxysmtd/ramdisk-recovery.img

echo "launching packaging script"
./release/auto/doit_GSM.sh
