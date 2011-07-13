#!/bin/bash

[[ -d release ]] || {
	echo "must be in kernel root dir"
	exit 1;
}

echo "packaging it up"

TYPE=$1
[[ "$TYPE" == '' ]] && TYPE=VIBRANT

declare -i RELVER=0

# until test ! -s release/${TYPE}/${REL}; do
REL=CM7_${TYPE}_Glitch-kernel_$(date +%Y%m%d_r)${RELVER}_update.zip
RELVER+=1
# done

rm -r release/system 2> /dev/null
mkdir  -p release/system/lib/modules || exit 1
mkdir  -p release/system/etc/init.d || exit 1
cp release/logger.module release/system/lib/modules/logger.ko
find . -name "*.ko" -exec cp {} release/system/lib/modules/ \; 2>/dev/null || exit 1

cd release && {
	cp 90screenstate_scaling system/etc/init.d/ || exit 1
	cp logcat_module system/etc/init.d/ || exit 1
	mkdir -p system/bin
	cp bin/* system/bin/
	zip -q -r ${REL} system boot.img META-INF bml_over_mtd bml_over_mtd.sh || exit 1
	sha256sum ${REL} > ${REL}.sha256sum
	mkdir -p ${TYPE} || exit 1
	mv ${REL}* ${TYPE} || exit 1
} || exit 1

rm system/lib/modules/*
rm system/etc/init.d/*
echo ${REL}
exit 0
