#!/bin/bash

make mrproper

if test -s Makefile_backup -a -s arch/arm/Makefile_backup -a -d drivers/misc/samsung_modemctl_backup; then

	mv Makefile Makefile_opti
	mv Makefile_backup Makefile

	cd arch/arm && {
		mv Makefile Makefile_opti
		mv Makefile_backup Makefile
	}

	cd ~/android/system/kernel/samsung/glitch/drivers/misc && {
		mv samsung_modemctl samsung_modemctl_opti
		mv samsung_modemctl_backup samsung_modemctl
	}

	echo "Switching done. Building..."
	
	cd ~/android/system/kernel/samsung/glitch && {
	./galaxys.sh
	}
	#Delete the created old-toolchain kernel for less confusion
	
	cd ~/android/system/kernel/samsung/glitch/release/SGS && {
	declare -i RELVER=0

	until test ! -s ${REL}; do
		REL_=CM7_SGS_Glitch-kernel_$(date +%Y%m%d_r)${RELVER}_update.zip
		RELVER+=1
		REL=CM7_SGS_Glitch-kernel_$(date +%Y%m%d_r)${RELVER}_update.zip
	done
	
	rm ${REL_}
	rm ${REL_}.sha256sum
	}
	#Deleted	
	
	echo "Building done. Reswitching..."

	cd ~/android/system/kernel/samsung/glitch && {
		mv Makefile Makefile_backup
		mv Makefile_opti Makefile
	}

	cd arch/arm && {
		mv Makefile Makefile_backup
		mv Makefile_opti Makefile
	}

	cd ~/android/system/kernel/samsung/glitch/drivers/misc && {

		mv samsung_modemctl/built-in.o samsung_modemctl_opti/built-in.443stock_samsung_modemctl
		mv samsung_modemctl/modemctl/built-in.o samsung_modemctl_opti/modemctl/build-in.443stock_modemctl

		mv samsung_modemctl samsung_modemctl_backup
		mv samsung_modemctl_opti samsung_modemctl
	}

	echo "Done! now preparing for next build..."

	cd ~/android/system/kernel/samsung/glitch
	./prepare.sh

else

	echo "There's a backup missing"

fi

