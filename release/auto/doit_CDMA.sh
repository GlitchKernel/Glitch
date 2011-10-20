#!/bin/bash

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
	mkdir  -p system/etc/init.d || exit 1
	mkdir  -p system/etc/glitch-config || exit 1
	echo "active" > system/etc/glitch-config/screenstate_scaling || exit 1
	echo "conservative" > system/etc/glitch-config/sleep_governor || exit 1
	cp logger.module system/lib/modules/logger.ko
	cd ../
		find . -name "*.ko" -exec cp {} release/system/lib/modules/ \; 2>/dev/null || exit 1
	cd release
	cp lights.aries.so system/lib/hw/ || exit 1
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
	mv ${REL}* ${TYPE}_OLDMODEM || exit 1
} || exit 1

rm boot.img
rm system/lib/modules/*
rm system/lib/hw/*
rm system/etc/init.d/*
echo ${REL}
exit 0
