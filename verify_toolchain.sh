#!/bin/bash

# Available toolchains on the server :
# 
# 2009q3-67
# 4.5-2011.12

# Version of the toolchain you want to use :

VERSION=$9
[[ "$VERSION" == '' ]] && VERSION=2009q3-67

function verify_toolchain()
{
  echo ""
  echo "checking 4.4.3 and ${VERSION} toolchains ... "
  echo ""

  if [ ! -d ../glitch-build/toolchain/android-toolchain-eabi-${VERSION} ]; then

	if test -d ../glitch-build/toolchain/; then
echo "You have a Glitch toolchain directory already ;)"

# To use if you want only one toolchain at a time for building Glitch kernel :

#echo "You have a Glitch toolchain directory already.. Cleaning"
#    rm -rf ../glitch-toolchain/*

else

echo "Glitch toolchain directory created"
    mkdir ../glitch-build/toolchain/
	fi

# Downloading
echo ""
echo "Downloading the toolchain you asked for ... "
echo ""
    tarball="android-toolchain-eabi-${VERSION}-linux-x86.tar.bz2"
		if [ ! -f "$tarball" ]; then
 wget -c http://androtransfer.com/tk-glitch/toolchains/"$tarball"

		fi
echo ""
echo "Decompressing into Glitch toolchain folder... "
echo ""
			if tar -C ../glitch-build/toolchain/ -xjf "$tarball"; then
if test -d ../glitch-build/toolchain/arm-2009q3; then
	mv ../glitch-build/toolchain/arm-2009q3 ../glitch-build/toolchain/android-toolchain-eabi-${VERSION}
else
	mv ../glitch-build/toolchain/android-toolchain-eabi ../glitch-build/toolchain/android-toolchain-eabi-${VERSION}
fi

else

echo "############################################"
echo "#  Something went wrong ! Trying again ... #"
echo "############################################"
    rm -rf ../glitch-build/toolchain/android-toolchain-eabi-${VERSION}
    rm "$tarball"
    source ./verify_toolchain.sh
    verify_toolchain
			fi
if test -s "$tarball"; then
echo "Cleaning downloaded file ... "
    rm "$tarball"
fi

  fi

  if [ ! -d ../../../prebuilt/linux-x86/toolchain/arm-eabi-4.4.3 ]; then
    echo "------------------------------------------------"
    echo "Please install the CM9 build environment first !"
    echo "------------------------------------------------"
    exit 1
  fi

if VERSION=2009q3-67; then

  export CROSS_COMPILE_GLITCH=../glitch-build/toolchain/android-toolchain-eabi-${VERSION}/bin/arm-none-linux-gnueabi-
else
  export CROSS_COMPILE_GLITCH=../glitch-build/toolchain/android-toolchain-eabi-${VERSION}/bin/arm-eabi-
fi
  export CROSS_COMPILE_443=../../../prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-
  
  echo "Toolchains are ready for Glitch building ! :)"
}
