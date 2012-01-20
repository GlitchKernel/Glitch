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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <asm/proc-fns.h>
#include <mach/cpuidle.h>
#include <mach/system.h>

static int mx5_cpuidle_init(void *init_data);
static int mx5_cpuidle(struct cpuidle_device *dev, struct cpuidle_state *state);

static struct imx_cpuidle_state_data mx5_cpuidle_state_data[] __initdata = {
	{
		.name = "powered_noclock",
		.desc = "idle cpu powered, unclocked.",
		.exit_latency = 12,
		.mach_cpu_pwr_state = WAIT_UNCLOCKED,
	}, {
		.name = "nopower_noclock",
		.desc = "idle cpu unpowered, unclocked.",
		.exit_latency = 20,
		.mach_cpu_pwr_state = WAIT_UNCLOCKED_POWER_OFF,
	}
};

static struct cpuidle_driver mx5_cpuidle_driver = {
	.name	= "imx5_cpuidle",
	.owner	= THIS_MODULE,
};

struct imx_cpuidle_data mx5_cpuidle_data __initdata = {
	.imx_cpuidle_driver = &mx5_cpuidle_driver,
	.state_data = mx5_cpuidle_state_data,
	.mach_cpuidle = mx5_cpuidle,
	.mach_cpuidle_init = mx5_cpuidle_init,
	.num_states = ARRAY_SIZE(mx5_cpuidle_state_data),
};

int mx5_cpuidle(struct cpuidle_device *dev, struct cpuidle_state *state)
{
	mx5_cpu_lp_set((enum mxc_cpu_pwr_mode)state->driver_data);

	cpu_do_idle();

	return 0;
}

int __init mx5_cpuidle_init(void * init_data)
{
	int ret;
	struct clk *gpc_dvfs_clk;

	gpc_dvfs_clk = clk_get(NULL, "gpc_dvfs");

	if (IS_ERR(gpc_dvfs_clk)) {
		pr_err("%s: Failed to get gpc_dvfs clock\n", __func__);
		return (int)gpc_dvfs_clk;
	}

	ret = clk_enable(gpc_dvfs_clk);

	if (IS_ERR(&ret)) {
		pr_err("%s: Failed to enable gpc_dvfs clock\n", __func__);
		return ret;
	}

	return 0;
}
