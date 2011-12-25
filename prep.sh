#!/bin/bash

# need these lines for make mrproper to work right
source ./verify_toolchain.sh
verify_toolchain

export CROSS_COMPILE=$CROSS_COMPILE_GLITCH

make mrproper

echo "Old build cleaned"
