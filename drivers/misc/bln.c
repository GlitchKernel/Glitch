/* drivers/misc/bln.c
 *
 * Copyright 2011  Michael Richter (alias neldar)
 * Copyright 2011  Adam Kent <adam@semicircular.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/earlysuspend.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/bln.h>
#include <linux/mutex.h>

static bool bln_enabled = true;
static bool bln_ongoing = false; /* ongoing LED Notification */
static int bln_blink_state = 0;
static bool bln_suspended = false; /* is system suspended */
static struct bln_implementation *bln_imp = NULL;

static long unsigned int notification_led_mask = 0x0;

#ifdef CONFIG_GENERIC_BLN_EMULATE_BUTTONS_LED
static bool buttons_led_enabled = false;
#endif

#define BACKLIGHTNOTIFICATION_VERSION 9

static int gen_all_leds_mask(void)
{
	int i = 0;
	int mask = 0x0;

	for(; i < bln_imp->led_count; i++)
		mask |= 1 << i;

	return mask;
}

static int get_led_mask(void){
	return (notification_led_mask != 0) ? notification_led_mask: gen_all_leds_mask();
}

static void reset_bln_states(void)
{
	bln_blink_state = 0;
	bln_ongoing = false;
}

static void bln_enable_backlights(int mask)
{
	if (likely(bln_imp && bln_imp->enable))
		bln_imp->enable(mask);
}

static void bln_disable_backlights(int mask)
{
	if (likely(bln_imp && bln_imp->disable))
		bln_imp->disable(mask);
}

static void bln_power_on(void)
{
	if (likely(bln_imp && bln_imp->power_on))
		bln_imp->power_on();
}

static void bln_power_off(void)
{
	if (likely(bln_imp && bln_imp->power_off))
		bln_imp->power_off();
}

static void bln_early_suspend(struct early_suspend *h)
{
	bln_suspended = true;
}

static void bln_late_resume(struct early_suspend *h)
{
	bln_suspended = false;

	reset_bln_states();
}

static struct early_suspend bln_suspend_data = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = bln_early_suspend,
	.resume = bln_late_resume,
};

static void enable_led_notification(void)
{
	if (!bln_enabled)
		return;

	/* dont allow led notifications while the screen is on */
	if (!bln_suspended)
		return;

	bln_ongoing = true;

	bln_power_on();
	bln_enable_backlights(get_led_mask());
	pr_info("%s: notification led enabled\n", __FUNCTION__);
}

static void disable_led_notification(void)
{
	if (bln_suspended && bln_ongoing) {
		bln_disable_backlights(gen_all_leds_mask());
		bln_power_off();
	}

	reset_bln_states();

	pr_info("%s: notification led disabled\n", __FUNCTION__);
}

static ssize_t backlightnotification_status_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	if(likely(bln_imp)) {
		if(bln_enabled) {
			ret = 1;
		} else {
			ret = 0;
		}
	} else {
		ret = -1;
	}

	return sprintf(buf, "%u\n", ret);
}

static ssize_t backlightnotification_status_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if(unlikely(!bln_imp)) {
		pr_err("%s: no BLN implementation registered!\n", __FUNCTION__);
		return size;
	}

	if (sscanf(buf, "%u\n", &data) != 1) {
			pr_info("%s: input error\n", __FUNCTION__);
			return size;
	}

	pr_devel("%s: %u \n", __FUNCTION__, data);

	if (data == 1) {
		pr_info("%s: BLN function enabled\n", __FUNCTION__);
		bln_enabled = true;
	} else if (data == 0) {
		pr_info("%s: BLN function disabled\n", __FUNCTION__);
		bln_enabled = false;
		if (bln_ongoing)
			disable_led_notification();
	} else {
		pr_info("%s: invalid input range %u\n", __FUNCTION__,
				data);
	}

	return size;
}

static ssize_t notification_led_status_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%u\n", (bln_ongoing ? 1 : 0));
}

static ssize_t notification_led_status_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if (sscanf(buf, "%u\n", &data) != 1) {
			pr_info("%s: input error\n", __FUNCTION__);
			return size;
	}

	if (data == 1)
		enable_led_notification();
	else if (data == 0)
		disable_led_notification();
	else
		pr_info("%s: wrong input %u\n", __FUNCTION__, data);

	return size;
}

static ssize_t notification_led_mask_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%lu\n", notification_led_mask);
}

static ssize_t notification_led_mask_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if (sscanf(buf, "%u\n", &data) != 1) {
			pr_info("%s: input error\n", __FUNCTION__);
			return size;
	}

	if(data & gen_all_leds_mask()){
		notification_led_mask = data;
	} else {
		notification_led_mask = 0x0;
	}

	return size;
}

static ssize_t blink_control_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", bln_blink_state);
}

static ssize_t blink_control_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if (!bln_ongoing)
		return size;

	if (sscanf(buf, "%u\n", &data) != 1) {
		pr_info("%s: input error\n", __FUNCTION__);
		return size;
	}

	/* reversed logic:
	 * 1 = leds off
	 * 0 = leds on
	 */
	if (data == 1) {
		bln_blink_state = 1;
		bln_disable_backlights(get_led_mask());
	} else if (data == 0) {
		bln_blink_state = 0;
		bln_enable_backlights(get_led_mask());
	} else {
		pr_info("%s: wrong input %u\n", __FUNCTION__, data);
	}

	return size;
}

static ssize_t backlightnotification_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", BACKLIGHTNOTIFICATION_VERSION);
}

static ssize_t led_count_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int ret = 0x0;

	if (bln_imp)
		ret = bln_imp->led_count;

	return sprintf(buf,"%u\n", ret);
}

#ifdef CONFIG_GENERIC_BLN_EMULATE_BUTTONS_LED
static ssize_t buttons_led_status_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%u\n", (buttons_led_enabled ? 1 : 0));
}

static ssize_t buttons_led_status_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if (sscanf(buf, "%u\n", &data) != 1) {
			pr_info("%s: input error\n", __FUNCTION__);
			return size;
	}

	if (data == 1) {
		if(!bln_suspended){
			buttons_led_enabled = true;
			bln_power_on();
			bln_enable_backlights(gen_all_leds_mask());
		}
	} else if (data == 0) {
		if(!bln_suspended){
			buttons_led_enabled = false;
			bln_disable_backlights(gen_all_leds_mask());
		}
	} else {
		pr_info("%s: wrong input %u\n", __FUNCTION__, data);
	}

	return size;
}

static DEVICE_ATTR(buttons_led, S_IRUGO | S_IWUGO,
		buttons_led_status_read,
		buttons_led_status_write);
#endif

static DEVICE_ATTR(blink_control, S_IRUGO | S_IWUGO, blink_control_read,
		blink_control_write);
static DEVICE_ATTR(enabled, S_IRUGO | S_IWUGO,
		backlightnotification_status_read,
		backlightnotification_status_write);
static DEVICE_ATTR(led_count, S_IRUGO , led_count_read, NULL);
static DEVICE_ATTR(notification_led, S_IRUGO | S_IWUGO,
		notification_led_status_read,
		notification_led_status_write);
static DEVICE_ATTR(notification_led_mask, S_IRUGO | S_IWUGO,
		notification_led_mask_read,
		notification_led_mask_write);
static DEVICE_ATTR(version, S_IRUGO , backlightnotification_version, NULL);

static struct attribute *bln_notification_attributes[] = {
	&dev_attr_blink_control.attr,
	&dev_attr_enabled.attr,
	&dev_attr_led_count.attr,
	&dev_attr_notification_led.attr,
	&dev_attr_notification_led_mask.attr,
#ifdef CONFIG_GENERIC_BLN_EMULATE_BUTTONS_LED
	&dev_attr_buttons_led.attr,
#endif
	&dev_attr_version.attr,
	NULL
};

static struct attribute_group bln_notification_group = {
	.attrs  = bln_notification_attributes,
};

static struct miscdevice bln_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "backlightnotification",
};

/**
 *	register_bln_implementation	- register a bln implementation of a touchkey device device
 *	@imp: bln implementation structure
 *
 *	Register a bln implementation with the bln kernel module.
 */
void register_bln_implementation(struct bln_implementation *imp)
{
	if(likely(imp)){
		bln_imp = imp;
	}
}
EXPORT_SYMBOL(register_bln_implementation);

/**
 *	bln_is_ongoing - check if a bln (led) notification is ongoing
 */
bool bln_is_ongoing()
{
	return bln_ongoing;
}
EXPORT_SYMBOL(bln_is_ongoing);

static int __init bln_control_init(void)
{
	int ret;

	pr_info("%s misc_register(%s)\n", __FUNCTION__, bln_device.name);
	ret = misc_register(&bln_device);
	if (ret) {
		pr_err("%s misc_register(%s) fail\n", __FUNCTION__,
				bln_device.name);
		return 1;
	}

	/* add the bln attributes */
	if (sysfs_create_group(&bln_device.this_device->kobj,
				&bln_notification_group) < 0) {
		pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
		pr_err("Failed to create sysfs group for device (%s)!\n",
				bln_device.name);
	}

	register_early_suspend(&bln_suspend_data);

	return 0;
}

device_initcall(bln_control_init);
