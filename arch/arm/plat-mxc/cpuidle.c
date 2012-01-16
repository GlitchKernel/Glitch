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

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/cpuidle.h>
#include <linux/err.h>
#include <asm/proc-fns.h>
#include <mach/cpuidle.h>

static int (*mach_cpuidle)(struct cpuidle_device *dev,
			       struct cpuidle_state *state);
static struct cpuidle_driver *imx_cpuidle_driver;
static struct cpuidle_device *device;

static int imx_enter_idle(struct cpuidle_device *dev,
			       struct cpuidle_state *state)
{
	struct timeval before, after;
	int idle_time;

	local_irq_disable();
	local_fiq_disable();

	do_gettimeofday(&before);

	mach_cpuidle(dev, state);

	do_gettimeofday(&after);

	local_fiq_enable();
	local_irq_enable();

	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		(after.tv_usec - before.tv_usec);

	return idle_time;
}

static DEFINE_PER_CPU(struct cpuidle_device, imx_cpuidle_device);

int __init imx_cpuidle_init(struct imx_cpuidle_data *cpuidle_data)
{
	int i, cpu_id;

	if (cpuidle_data == NULL) {
		pr_err("%s: cpuidle_data pointer NULL\n", __func__);
		return -EINVAL;
	}

	if (cpuidle_data->mach_cpuidle == NULL) {
		pr_err("%s: idle callback function NULL\n", __func__);
		return -EINVAL;
	}

	imx_cpuidle_driver = cpuidle_data->imx_cpuidle_driver;

	mach_cpuidle = cpuidle_data->mach_cpuidle;

	/* register imx_cpuidle driver */
	if (cpuidle_register_driver(imx_cpuidle_driver)) {
		pr_err("%s: Failed to register cpuidle driver\n", __func__);
		return -ENODEV;
	}

	/* if provided, initialize the mach level cpuidle functionality */
	if (cpuidle_data->mach_cpuidle_init) {
		if (cpuidle_data->mach_cpuidle_init(cpuidle_data)) {
			pr_err("%s: Failed to register cpuidle driver\n",
				 __func__);
			cpuidle_unregister_driver(imx_cpuidle_driver);
			return -ENODEV;
		}
	}

	/* initialize state data for each cpuidle_device(one per present cpu)*/
	for_each_cpu(cpu_id, cpu_present_mask) {

		device = &per_cpu(imx_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

		device->state_count = min((unsigned int) CPUIDLE_STATE_MAX,
			cpuidle_data->num_states);

		device->prepare = cpuidle_data->prepare;

		for (i = 0; i < device->state_count; i++) {
			strlcpy(device->states[i].name,
				cpuidle_data->state_data[i].name,
				CPUIDLE_NAME_LEN);

			strlcpy(device->states[i].desc,
				cpuidle_data->state_data[i].desc,
				CPUIDLE_DESC_LEN);

			device->states[i].driver_data =
				(void *)cpuidle_data->
				state_data[i].mach_cpu_pwr_state;

			/*
			 * Because the imx_enter_idle function measures
			 * and returns a valid time for all imx SoCs,
			 * we always set this flag.
			 */
			device->states[i].flags = CPUIDLE_FLAG_TIME_VALID;

			device->states[i].exit_latency =
				cpuidle_data->state_data[i].exit_latency;

			device->states[i].power_usage =
				cpuidle_data->state_data[i].power_usage;

			device->states[i].target_residency =
				cpuidle_data->state_data[i].target_residency;

			device->states[i].enter = imx_enter_idle;
		}
	}

	return 0;
}

int __init imx_cpuidle_dev_init(void)
{
	int cpu_id;

	/*
	 * Register online cpus.  If maxcpus is specified as a boot
	 * argument and secondary cpus are brought online after boot,
	 * this function can be called again to register a cpuidle
	 * device for those secondary cpus.
	 */
	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(imx_cpuidle_device, cpu_id);
		if (device == NULL) {
			pr_err("%s: Failed to register (No device)\n",
				__func__);
			return -ENODEV;
		}

		if (!device->registered)
			if (cpuidle_register_device(device)) {
				pr_err("%s: Failed to register\n", __func__);
				return -ENODEV;
			}
	}

	return 0;
}
late_initcall(imx_cpuidle_dev_init);
