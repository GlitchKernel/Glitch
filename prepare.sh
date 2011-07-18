make mrproper

echo "Old build cleaned"

if [ -f arch/arm/mach-s5pv210/mach-aries.c_backup ]
then
mv arch/arm/mach-s5pv210/mach-aries.c arch/arm/mach-s5pv210/mach-aries.c_telus
mv arch/arm/mach-s5pv210/mach-aries.c_backup arch/arm/mach-s5pv210/mach-aries.c
echo " "
echo "Found mach-aries.c_backup from failed Telus building"
echo "Switching files for clean build"
echo " "
fi

if [ -f drivers/misc/samsung_modemctl/built-in.443stock_samsung_modemctl ]
then
cp drivers/misc/samsung_modemctl/built-in.443stock_samsung_modemctl drivers/misc/samsung_modemctl/built-in.o
cp drivers/misc/samsung_modemctl/modemctl/built-in.443stock_modemctl drivers/misc/samsung_modemctl/modemctl/built-in.o
echo "Built-in.o modem files copied"
else
echo "***** built-in.443stock files are missing *****"
echo "******** Please build old CDMA or GSM *********"
fi

