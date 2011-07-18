#!/bin/bash

make mrproper

if test -s Makefile_backup -a -s arch/arm/Makefile_backup -a -d drivers/misc/samsung_modemctl_backup; then

		mv Makefile Makefile_opti
		mv Makefile_backup Makefile

		mv arch/arm/Makefile arch/arm/Makefile_opti
		mv arch/arm/Makefile_backup arch/arm/Makefile

		mv drivers/misc/samsung_modemctl drivers/misc/samsung_modemctl_opti
		mv drivers/misc/samsung_modemctl_backup drivers/misc/samsung_modemctl

	echo "Switching done. Building..." && {
	
	. release/auto/GSM.sh

	}

	# Keep the created old-toolchain kernel for potential use - deleting sha256sum
	
	cd release/GSM_OLDMODEM && {
	declare -i RELVER=0

	until test ! -s ${REL}; do
		REL=CM7${TYPE}-Glitch-DEV-$(date +%Y%m%d_%H%M_r)${RELVER}-OLDMODEM.zip
		RELVER+=1
		REL=CM7${TYPE}-Glitch-DEV-$(date +%Y%m%d_%H%M_r)${RELVER}-OLDMODEM.zip
	done
	
	rm ${REL_}.sha256sum
	cd ../../
	}
	#Deleted	
	
	echo "Building done. Reswitching..." && {

		mv Makefile Makefile_backup
		mv Makefile_opti Makefile

		mv arch/arm/Makefile arch/arm/Makefile_backup
		mv arch/arm/Makefile_opti arch/arm/Makefile

		mv drivers/misc/samsung_modemctl/built-in.o drivers/misc/samsung_modemctl_opti/built-in.443stock_samsung_modemctl
		mv drivers/misc/samsung_modemctl/modemctl/built-in.o drivers/misc/samsung_modemctl_opti/modemctl/built-in.443stock_modemctl

		mv drivers/misc/samsung_modemctl drivers/misc/samsung_modemctl_backup
		mv drivers/misc/samsung_modemctl_opti drivers/misc/samsung_modemctl
		}

	echo "Done! now preparing for next build..." && {

	./prepare.sh
	}

else

	echo "There's a backup missing, if needed, reverting changes to prepare rebuild.. "

fi

if test -s Makefile_opti -a -s arch/arm/Makefile_opti -a -d drivers/misc/samsung_modemctl_opti; then

		mv Makefile Makefile_backup
		mv Makefile_opti Makefile 

		mv arch/arm/Makefile arch/arm/Makefile_backup
		mv arch/arm/Makefile_opti arch/arm/Makefile

		mv drivers/misc/samsung_modemctl drivers/misc/samsung_modemctl_backup
		mv drivers/misc/samsung_modemctl_opti drivers/misc/samsung_modemctl

	echo "Let's restart the process"
	./oldgsm.sh

fi

