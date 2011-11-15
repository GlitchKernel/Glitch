#!/bin/bash

make mrproper

source ./verify_toolchain.sh
verify_toolchain

if test -s Makefile_backup -a -s arch/arm/Makefile_backup -a -d drivers/misc/samsung_modemctl_backup; then

		mv Makefile Makefile_opti
		mv Makefile_backup Makefile

		mv arch/arm/Makefile arch/arm/Makefile_opti
		mv arch/arm/Makefile_backup arch/arm/Makefile

		mv drivers/misc/samsung_modemctl drivers/misc/samsung_modemctl_opti
		mv drivers/misc/samsung_modemctl_backup drivers/misc/samsung_modemctl

	echo "Switching done. Building..."
	
	. release/auto/GSM.sh
	
	echo "Building done. Reswitching..." && {

		mv Makefile Makefile_backup
		mv Makefile_opti Makefile

		mv arch/arm/Makefile arch/arm/Makefile_backup
		mv arch/arm/Makefile_opti arch/arm/Makefile

		mv drivers/misc/samsung_modemctl/built-in.o drivers/misc/samsung_modemctl_opti/built-in.443gsm_samsung_modemctl
		mv drivers/misc/samsung_modemctl/modemctl/built-in.o drivers/misc/samsung_modemctl_opti/modemctl/built-in.443gsm_modemctl

		mv drivers/misc/samsung_modemctl drivers/misc/samsung_modemctl_backup
		mv drivers/misc/samsung_modemctl_opti drivers/misc/samsung_modemctl
		}

	echo "Done! now preparing for next build..." && {

	./prepgsm.sh
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
	./443gsm.sh

fi

