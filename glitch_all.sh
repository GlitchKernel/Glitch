#!/bin/bash

echo "building ALL variants of all kernels!"

#echo "Building initramfs"
#./initramfs.sh

echo "Building CDMA variant(s) .. "
./glitch.sh fascinate

echo "Building GSM variants .. "
./glitch.sh captivate
./glitch.sh galaxys
./glitch.sh vibrant

