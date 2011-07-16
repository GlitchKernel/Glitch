#!/bin/bash

cd ../../../

echo "Building initramfs files for all devices" && {

echo "--------------------Building for i9000------------------"
. build/envsetup.sh && lunch full_galaxysmtd-eng && make bootimage

echo "------------------Building for Captivate----------------"
. build/envsetup.sh && lunch full_captivatemtd-eng && make bootimage

echo "-------------------Building for Vibrant-----------------"
. build/envsetup.sh && lunch full_vibrantmtd-eng && make bootimage

#echo "------------------Building for Fascinate----------------"
#. build/envsetup.sh && lunch full_fascinatemtd-eng && make bootimage
}

echo "Done!"
