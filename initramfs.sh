#!/bin/bash

# Vibrant needs a fix on CM7 repo for now. Navigate to device/samsung/vibrantmtd/, and open full_vibrantmtd.mk. On line 34, PRODUCT_DEVICE := SGH-T959 should be changed to PRODUCT_DEVICE := vibrantmtd.

# Building for all devices by default.

# To build for one device only, use ./initramfs.sh "phone name" (without quote).
# Example : ./initramfs.sh captivate

declare -A phone

if phones[0]=$1; then
echo ""
echo "--------------Forcing for $1 only"

else

phones[0]="galaxys"
phones[1]="captivate"
phones[2]="vibrant"
phones[3]="fascinate"

fi

cd ../../../

for i in ${!phones[@]}; do
	phone=${phones[$i]}

echo "--------------Building ramdisk.img and recovery.img for ${phone}"
echo ""
. build/envsetup.sh && lunch full_${phone}mtd-eng && make bootimage
done

echo "Done!"
