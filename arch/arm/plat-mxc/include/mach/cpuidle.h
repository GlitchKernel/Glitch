/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __ARCH_ARM_PLAT_MXC_CPUIDLE_H__
#define __ARCH_ARM_PLAT_MXC_CPUIDLE_H__

#include <linux/cpuidle.h>
#include <mach/hardware.h>

/* for passing cpuidle state info to the cpuidle driver. */
struct imx_cpuidle_state_data {
	enum mxc_cpu_pwr_mode	mach_cpu_pwr_state;
	char			*name;
	char			*desc;
	/* time in uS to exit this idle state */
	unsigned int		exit_latency;
	/* OPTIONAL - power usage of this idle state in mW */
	unsigned int		power_usage;
	/* OPTIONAL - in uS. See drivers/cpuidle/governors/menu.c for usage */
	unsigned int		target_residency;
};

struct imx_cpuidle_data {
	unsigned int num_states;
	struct cpuidle_driver *imx_cpuidle_driver;
	struct imx_cpuidle_state_data *state_data;
	int (*mach_cpuidle)(struct cpuidle_device *dev,
		struct cpuidle_state *state);

	/* OPTIONAL - parameter of mach_cpuidle_init func below */
	void *mach_init_data;
	/* OPTIONAL - callback for mach level cpuidle initialization */
	int (*mach_cpuidle_init)(void *mach_init_data);
	/* OPTIONAL - Search drivers/cpuidle/cpuidle.c for usage */
	int (*prepare)(struct cpuidle_device *dev);
};

#ifdef CONFIG_CPU_IDLE
int imx_cpuidle_init(struct imx_cpuidle_data *cpuidle_data);
#else
static inline int imx_cpuidle_init(struct imx_cpuidle_data *cpuidle_data)
{
	return -EINVAL;
}
#endif /* CONFIG_CPU_IDLE */

#endif /* __ARCH_ARM_PLAT_MXC_CPUIDLE_H__ */
