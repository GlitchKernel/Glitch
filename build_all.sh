#!/bin/bash

source ./verify_toolchain.sh

verify_toolchain

echo "building ALL variants of all kernels!"

echo "building initramfs"

./initramfs.sh

echo "Prepping for CDMA variant builds ... "
./443cdma.sh


echo "Building CDMA variant(s) .. "
./fascinate.sh

echo "Prepping for GSM builds .. "
./443gsm.sh

echo "Building GSM variants .. "
./cappy.sh
./galaxys.sh
./vibrant.sh
./telus.sh

