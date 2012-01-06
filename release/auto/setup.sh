#!/bin/bash

# Available toolchains on the server :
#
# 4.4-2011.02
# 4.5-2011.12

# Version of the toolchain you want to use :

VERSION="4.4-2011.02"

function verify_toolchain()
{
  echo ""
  echo "checking 4.4.3 and ${VERSION} toolchains ... "
  echo ""

if test -d $CM9_REPO/kernel/samsung/glitch-build/modem/; then
echo "Modem folder is ok"
else
echo "Modem folder created"
    mkdir $CM9_REPO/kernel/samsung/glitch-build/modem
	fi

  if [ ! -d $CM9_REPO/kernel/samsung/glitch-build/toolchain/android-toolchain-eabi-${VERSION} ]; then

	if test -d $CM9_REPO/kernel/samsung/glitch-build/toolchain/; then
echo "You have a Glitch toolchain directory already ;)"

# To use if you want only one toolchain at a time for building Glitch kernel :

#echo "You have a Glitch toolchain directory already.. Cleaning"
#    rm -rf $CM9_REPO/kernel/samsung/glitch-build/toolchain/*

else

echo "Glitch toolchain directory created"
    mkdir $CM9_REPO/kernel/samsung/glitch-build/toolchain
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
			if tar -C $CM9_REPO/kernel/samsung/glitch-build/toolchain/ -xjf "$tarball"; then

	mv $CM9_REPO/kernel/samsung/glitch-build/toolchain/android-toolchain-eabi $CM9_REPO/kernel/samsung/glitch-build/toolchain/android-toolchain-eabi-${VERSION}

else

echo "############################################"
echo "#  Something went wrong ! Trying again ... #"
echo "############################################"
    rm -rf $CM9_REPO/kernel/samsung/glitch-build/toolchain/android-toolchain-eabi-${VERSION}
    rm "$tarball"
    source ./verify_toolchain.sh
    verify_toolchain
			fi
if test -s "$tarball"; then
echo "Cleaning downloaded file ... "
    rm "$tarball"
fi

  fi

  if [ ! -d $CM9_REPO/prebuilt/linux-x86/toolchain/arm-eabi-4.4.3 ]; then
    echo "------------------------------------------------"
    echo "Please install the CM9 build environment first !"
    echo "------------------------------------------------"
    exit 1
  fi

  export CROSS_PREFIX_GLITCH=$CM9_REPO/kernel/samsung/glitch-build/toolchain/android-toolchain-eabi-${VERSION}/bin/arm-eabi-

  export CROSS_PREFIX_443=$CM9_REPO/prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-
  
  echo "Toolchains are ready for Glitch building ! :)"
}
