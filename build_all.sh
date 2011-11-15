#!/bin/bash

source ./verify_toolchain.sh

verify_toolchain

echo "building ALL variants of all kernels!"

echo "building initramfs"

./initramfs.sh

echo "Prepping for CDMA variant builds ... "
export CROSS_COMPILE=$CROSS_COMPILE_443
./443cdma.sh


echo "Building CDMA variant(s) .. "
export CROSS_COMPILE=$CROSS_COMPILE_GLITCH
./fascinate.sh

echo "Prepping for GSM builds .. "
export CROSS_COMPILE=$CROSS_COMPILE_443
./443gsm.sh

echo "Building GSM variants .. "
export CROSS_COMPILE=$CROSS_COMPILE_GLITCH
./cappy.sh
./galaxys.sh
./vibrant.sh
./telus.sh

# don't interfere with other stuff that might use this var
unset CROSS_COMPILE
