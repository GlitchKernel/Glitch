#!/bin/bash

# need these lines for make mrproper to work right when called by itself
source ./verify_toolchain.sh
verify_toolchain

export CROSS_COMPILE=$CROSS_COMPILE_GLITCH

make mrproper

echo "Old build cleaned"

if [ -f drivers/misc/samsung_modemctl/built-in.443cdma_samsung_modemctl ]
then
cp drivers/misc/samsung_modemctl/built-in.443cdma_samsung_modemctl drivers/misc/samsung_modemctl/built-in.o
#cp drivers/misc/samsung_modemctl/modemctl/built-in.443cdma_modemctl drivers/misc/samsung_modemctl/modemctl/built-in.o
echo "Built-in.o modem files for CDMA copied"
else
echo "***** built-in.443cdma files are missing *****"
echo "******** Please build old CDMA *********"
fi

