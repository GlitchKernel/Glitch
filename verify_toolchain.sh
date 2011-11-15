#!/bin/bash

function verify_toolchain()
{
  echo "checking toolchains ... "

  if [ ! -d android-toolchain-eabi ]; then
    tarball="android-toolchain-eabi-linaro-4.5-2011.10-1-2011-10-21_15-21-26-linux-x86.tar.bz2"
    if [ ! -f "$tarball" ]; then
      wget -c http://androtransfer.com/uploads/"$tarball"
    fi
    tar -xjf "$tarball"
  fi

  if [ ! -d ../../../prebuilt/linux-x86/toolchain/arm-eabi-4.4.3 ]; then
    echo "Please install the CM7 build environment first."
    exit 1
  fi

  export CROSS_COMPILE_GLITCH=`pwd`/android-toolchain-eabi/bin/arm-eabi-
  export CROSS_COMPILE_443=../../../prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-
  
  echo "toolchains ok"
}