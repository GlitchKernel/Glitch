#!/bin/bash

source ./verify_toolchain.sh

verify_toolchain

echo "building ALL variants of all kernels!"

echo "building initramfs"

./initramfs.sh


echo "Building CDMA variant(s) .. "
./fascinate.sh

echo "Building GSM variants .. "
./cappy.sh
./galaxys.sh
./vibrant.sh

