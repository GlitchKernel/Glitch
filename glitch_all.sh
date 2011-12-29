#!/bin/bash

source ./verify_toolchain.sh

verify_toolchain

echo "building ALL variants of all kernels!"

echo "building initramfs"

#./initramfs.sh


echo "Building CDMA variant(s) .. "
./build.sh fascinate

echo "Building GSM variants .. "
./build.sh captivate
./build.sh galaxys
./build.sh vibrant

