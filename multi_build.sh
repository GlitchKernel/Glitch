#!/bin/bash

export KBUILD_BUILD_VERSION="1"

declare -A phones codes

if [[ $# -gt 1 ]]; then
	phones[0]=$1
	codes[0]=$2
	echo "Forced build for $1"
	UPLOAD=0
	if [[ $# -eq 3 ]]; then
		UPLOAD=1
	fi
else
	UPLOAD=1
	phones[0]="GT-I9000"
	codes[0]="galaxys"
	phones[1]="GT-I9000B"
	codes[1]="galaxysb"
	phones[2]="Captivate"
	codes[2]="captivate"
	phones[3]="Vibrant"
	codes[3]="vibrant"
fi

# kernel option changes
OPTS="CONFIG_NETFILTER_XT_MATCH_MULTIPORT \
CONFIG_SYN_COOKIES \
CONFIG_TINY_RCU \
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
CONFIG_STRACKTRACE \
CONFIG_STACKTRACE_SUPPORT
"

OPTNEWVAL=""
RELVER=$(($(cat .relver)+1))

for i in ${!phones[@]}; do
	phone=${phones[$i]}
	code=${codes[$i]}
	echo "==== $phone ($code) ===="
	cp arch/arm/configs/aries_${code}_defconfig .config || { echo "failed config copy"; exit 1;}

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

	#"ok" to defaults
	while true; do echo; done | make oldconfig || { echo "failed config"; exit 1; }
	make -j4 || { echo "failed build"; exit 1; }

	../../../device/samsung/common/aries/mkshbootimg.py release/boot.img arch/arm/boot/zImage ../../../out/target/product/$code/ramdisk.cpio.gz ../../../out/target/product/$code/recovery.cpio.gz || { echo "failed mkshbootimg"; exit 1; }

	f=$(./release/doit.sh $phone $RELVER) || { echo "failed CWM"; exit 1; }
	if [[ "$UPLOAD" -eq 1 ]]; then
		./release/upload.sh $phone $f || { echo "failed upload"; exit 1; }
	else
		echo "Ready as $1 $2 $f"
	fi
done
echo $RELVER > .relver
echo "done."
