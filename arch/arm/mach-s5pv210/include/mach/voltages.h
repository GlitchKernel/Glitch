/* arch/arm/mach-s5pv210/include/mach/voltages.h
*
* Copyright (c) 2010 Samsung Electronics Co., Ltd.
*
* S5PV210/S5PC110 CPU frequency scaling support
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_VOLTAGES_H
#define __ASM_ARCH_VOLTAGES_H

// these 2 are always the same
#define ARMVOLT 1500000
#define INTVOLT 1250000

// these are all the same, too!
#define DVSARM1 1500000 //1400
#define DVSARM2 1500000 //1300
#define DVSARM3 1500000 //1200
#define DVSARM4 1300000 //1000
#define DVSARM5 1225000 //800
#define DVSARM6 1100000 //400
#define DVSARM7 1000000 //200
#define DVSARM8 950000  //100

//High leakage
//#ifdef CONFIG_SOC_HIGH_LEAKAGE

#define ARMBOOT_HL 1275000
#define INTBOOT_HL 1150000

#define DVSINT1_HL  1250000 //1400
#define DVSINT2_HL  1250000 //1300
#define DVSINT3_HL  1225000 //1200
#define DVSINT4_HL  1150000 //1000
#define DVSINT5_HL  1100000 //200, 400, 800
#define DVSINT6_HL  1000000 //100
//#endif

//Med leakage
//#ifdef CONFIG_SOC_MEDIUM_LEAKAGE

#define ARMBOOT_ML 1250000
#define INTBOOT_ML 1125000

#define DVSINT1_ML  1250000 //1400
#define DVSINT2_ML  1225000 //1300
#define DVSINT3_ML  1200000 //1200
#define DVSINT4_ML  1125000 //1000
#define DVSINT5_ML  1100000 //200, 400, 800
#define DVSINT6_ML  1000000 //100
//#endif

//Low leakage
//#ifdef CONFIG_SOC_LOW_LEAKAGE

#define ARMBOOT_LL 1250000
#define INTBOOT_LL 1100000

#define DVSINT1_LL  1225000 //1400
#define DVSINT2_LL  1200000 //1300
#define DVSINT3_LL  1175000 //1200
#define DVSINT4_LL  1100000 //1000
#define DVSINT5_LL  1050000 //200, 400, 800
#define DVSINT6_LL  1000000 //100
//#endif

#endif /* __ASM_ARCH_VOLTAGES_H */


