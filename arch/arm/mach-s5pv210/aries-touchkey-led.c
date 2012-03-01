/*
 * Copyright (C) 2010 Samsung Electronics Co. Ltd. All Rights Reserved.
 * Author: Rom Lemarchand <rlemarchand@sta.samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/earlysuspend.h>
#include <asm/mach-types.h>

#ifdef CONFIG_GENERIC_BLN
#include <linux/bln.h>
#endif

static int led_gpios[] = { 2, 3, 6, 7 };

static void aries_touchkey_led_onoff(int onoff)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(led_gpios); i++)
		gpio_direction_output(S5PV210_GPJ3(led_gpios[i]), !!onoff);
}

#ifdef CONFIG_GENERIC_BLN
static void aries_touchkey_bln_enable(void)
{
  aries_touchkey_led_onoff(1);
}

static void aries_touchkey_bln_disable(void)
{
  aries_touchkey_led_onoff(0);
}

static struct bln_implementation aries_touchkey_bln = {
  .enable = aries_touchkey_bln_enable,
  .disable = aries_touchkey_bln_disable,
};
#endif

static void aries_touchkey_led_early_suspend(struct early_suspend *h)
{
	aries_touchkey_led_onoff(0);
}

static void aries_touchkey_led_late_resume(struct early_suspend *h)
{
	aries_touchkey_led_onoff(1);
}

static struct early_suspend early_suspend = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = aries_touchkey_led_early_suspend,
	.resume = aries_touchkey_led_late_resume,
};

static int __init aries_init_touchkey_led(void)
{
	int i;
	int ret = 0;

#ifdef CONFIG_GENERIC_BLN
  u32 gpio;
#endif

	if (!machine_is_aries() || system_rev < 0x10)
		return 0;

	for (i = 0; i < ARRAY_SIZE(led_gpios); i++) {
#ifdef CONFIG_GENERIC_BLN
        gpio = S5PV210_GPJ3(led_gpios[i]);
        ret = gpio_request(gpio, "touchkey led");
        if (ret) {
            pr_err("Failed to request touchkey led gpio %d\n", i);
            goto err_req;
        }
        s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
        s3c_gpio_slp_cfgpin(gpio, S3C_GPIO_SLP_PREV);
        s3c_gpio_slp_setpull_updown(gpio, S3C_GPIO_PULL_NONE);
#else
		ret = gpio_request(S5PV210_GPJ3(led_gpios[i]), "touchkey led");
		if (ret) {
			pr_err("Failed to request touchkey led gpio %d\n", i);
			goto err_req;
		}
		s3c_gpio_setpull(S5PV210_GPJ3(led_gpios[i]),
							S3C_GPIO_PULL_NONE);
#endif
	}

	aries_touchkey_led_onoff(1);

	register_early_suspend(&early_suspend);

#ifdef CONFIG_GENERIC_BLN
    register_bln_implementation(&aries_touchkey_bln);
#endif

	return 0;

err_req:
	while (--i >= 0)
		gpio_free(S5PV210_GPJ3(led_gpios[i]));
	return ret;
}

device_initcall(aries_init_touchkey_led);
