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
#define INTVOLT 1300000

// these are all the same, too!
#define DVSARM1 1500000 //1500
#define DVSARM2 1450000 //1400
#define DVSARM3 1400000 //1300
#define DVSARM4 1350000 //1200
#define DVSARM5 1275000 //1000
#define DVSARM6 1200000 //800
#define DVSARM7 1050000 //400
#define DVSARM8 950000  //200
#define DVSARM9 950000  //100

#define ARMBOOT 1250000
#define INTBOOT 1125000

#define DVSINT1 1200000 //1500
#define DVSINT2 1175000 //1400
#define DVSINT3 1150000 //1300
#define DVSINT4 1125000 //1200
#define DVSINT5 1100000 //1000
#define DVSINT6 1100000 //200, 400, 800
#define DVSINT7 1000000 //100


#endif /* __ASM_ARCH_VOLTAGES_H */


