/* arch/arm/mach-s5pv210/include/mach/cpu-freq-v210.h
 *
 *  Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *
 * S5PV210/S5PC110 CPU frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_CPU_FREQ_H
#define __ASM_ARCH_CPU_FREQ_H

#include <linux/cpufreq.h>

enum perf_level {
	L0, L1, L2, L3, L4, MAX_PERF_LEVEL = L4,
};

/* For cpu-freq driver */
struct s5pv210_cpufreq_voltage {
	unsigned int	freq;	/* kHz */
	unsigned long	varm;	/* uV */
	unsigned long	vint;	/* uV */
};

struct s5pv210_cpufreq_data {
	struct s5pv210_cpufreq_voltage	*volt;
	unsigned int			size;
};

#ifdef CONFIG_DVFS_LIMIT
enum {
	DVFS_LOCK_TOKEN_1 = 0,	// MFC
	DVFS_LOCK_TOKEN_2,	//	(FIMC)
	DVFS_LOCK_TOKEN_3,	// SND_RP
	DVFS_LOCK_TOKEN_4,	//	(TV)
	DVFS_LOCK_TOKEN_5,	//	(early suspend)
	DVFS_LOCK_TOKEN_6,	// APPS by sysfs
	DVFS_LOCK_TOKEN_7,	// 	(TOUCH)
	DVFS_LOCK_TOKEN_8,	// USB
	DVFS_LOCK_TOKEN_9,	// BT
	DVFS_LOCK_TOKEN_NUM
};

extern void s5pv210_lock_dvfs_high_level(uint nToken, uint perf_level);
extern void s5pv210_unlock_dvfs_high_level(unsigned int nToken);
#endif

extern void s5pv210_cpufreq_set_platdata(struct s5pv210_cpufreq_data *pdata);

#endif /* __ASM_ARCH_CPU_FREQ_H */
