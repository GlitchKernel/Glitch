#!/bin/bash

source ./verify_toolchain.sh
verify_toolchain

echo "Cleaning up .. "

export CROSS_COMPILE=$CROSS_COMPILE_GLITCH
make clean
make mrproper

rm -rf ./release/{Captivate,CDMA_OLDMODEM,Fascinate,GSM_OLDMODEM,i9k,TelusFascinate,Vibrant}

echo "building ALL variants of ALL kernels!"

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

