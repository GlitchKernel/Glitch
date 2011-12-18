#!/bin/bash

# This script will build for all listed devices by default

# To build for one device only, use ./initramfs.sh "phone name" (without quote)
# Example : ./initramfs.sh captivate

declare -A phone

if [[ $# -gt 0 ]]; then
	phones[0]=$1
	echo " "
	echo "================Force building for $1 only=================="

else
	phones[0]="galaxys"
	phones[1]="fascinate"
fi

cd ../../../

for i in ${!phones[@]}; do
	phone=${phones[$i]}

echo "========Building ramdisk.img and recovery.img for ${phone}========"
echo ""
. build/envsetup.sh && lunch full_${phone}mtd-eng && make bootimage
done

echo "Done!"
