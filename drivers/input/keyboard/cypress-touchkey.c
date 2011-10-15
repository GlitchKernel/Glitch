/*
 * Copyright 2006-2010, Cypress Semiconductor Corporation.
 * Copyright (C) 2010, Samsung Electronics Co. Ltd. All Rights Reserved.
 * Copyright (C) 2011 <kang@insecure.ws>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor
 * Boston, MA  02110-1301, USA.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/earlysuspend.h>
#include <linux/miscdevice.h>
#include <linux/input/cypress-touchkey.h>

#define SCANCODE_MASK		0x07
#define UPDOWN_EVENT_MASK	0x08
#define ESD_STATE_MASK		0x10

#define BACKLIGHT_ON		0x10
#define BACKLIGHT_OFF		0x20

#define OLD_BACKLIGHT_ON	0x1
#define OLD_BACKLIGHT_OFF	0x2

#define BACKLIGHT_TIMEOUT	1600

#define DEVICE_NAME "cypress-touchkey"

int bl_on = 0;
static DECLARE_MUTEX(enable_sem);
static DECLARE_MUTEX(i2c_sem);

struct cypress_touchkey_devdata *bl_devdata;
static struct timer_list bl_timer;
static void bl_off(struct work_struct *bl_off_work);
static DECLARE_WORK(bl_off_work, bl_off);

#ifdef CONFIG_KEYPAD_CYPRESS_TOUCH_BLN
#include <linux/miscdevice.h>
#define BACKLIGHTNOTIFICATION_VERSION 8

bool bln_enabled = false; // indicates if BLN function is enabled/allowed (default: false, app enables it on boot)
bool bln_notification_ongoing= false; // indicates ongoing LED Notification
bool bln_blink_enabled = false;	// indicates blink is set
struct cypress_touchkey_devdata *bln_devdata; // keep a reference to the devdata
#endif

struct cypress_touchkey_devdata {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchkey_platform_data *pdata;
	struct early_suspend early_suspend;
	u8 backlight_on;
	u8 backlight_off;
	bool is_dead;
	bool is_powering_on;
	bool has_legacy_keycode;
	bool is_sleeping;
};

static int i2c_touchkey_read_byte(struct cypress_touchkey_devdata *devdata,
					u8 *val)
{
	int ret;
	int retry = 2;

	down(&i2c_sem);

	while (true) {
		ret = i2c_smbus_read_byte(devdata->client);
		if (ret >= 0) {
			*val = ret;
			ret = 0;
			break;
		}

		if (!retry--) {
            dev_err(&devdata->client->dev, "i2c read error\n");
			break;
        }
		msleep(10);
	}

	up(&i2c_sem);

	return ret;
}

static int i2c_touchkey_write_byte(struct cypress_touchkey_devdata *devdata,
					u8 val)
{
	int ret;
	int retry = 2;
    unsigned long flags;

	down(&i2c_sem);

	while (true) {
		ret = i2c_smbus_write_byte(devdata->client, val);
		if (!ret) {
			ret = 0;
			break;
		}

		if (!retry--) {
            dev_err(&devdata->client->dev, "i2c write error\n");
			break;
        }
		msleep(10);
	}

	up(&i2c_sem);

	return ret;
}

static void all_keys_up(struct cypress_touchkey_devdata *devdata)
{
	int i;

	for (i = 0; i < devdata->pdata->keycode_cnt; i++)
		input_report_key(devdata->input_dev,
						devdata->pdata->keycode[i], 0);

	input_sync(devdata->input_dev);
}

static void bl_off(struct work_struct *bl_off_work)
{
	if (bl_devdata == NULL || unlikely(bl_devdata->is_dead) ||
		bl_devdata->is_powering_on || bl_on || bl_devdata->is_sleeping)
		return;

	i2c_touchkey_write_byte(bl_devdata, bl_devdata->backlight_off);
}

void bl_timer_callback(unsigned long data)
{
	schedule_work(&bl_off_work);
}

static int recovery_routine(struct cypress_touchkey_devdata *devdata)
{
	int ret = -1;
	int retry = 10;
	u8 data;
	int irq_eint;

	if (unlikely(devdata->is_dead)) {
		dev_err(&devdata->client->dev, "%s: Device is already dead, "
				"skipping recovery\n", __func__);
		return -ENODEV;
	}

	irq_eint = devdata->client->irq;

	all_keys_up(devdata);

	disable_irq_nosync(irq_eint);
	while (retry--) {
		devdata->pdata->touchkey_onoff(TOUCHKEY_OFF);
		devdata->pdata->touchkey_onoff(TOUCHKEY_ON);
		ret = i2c_touchkey_read_byte(devdata, &data);
		if (!ret) {
			if (!devdata->is_sleeping)
				enable_irq(irq_eint);
			goto out;
		}
		dev_err(&devdata->client->dev, "%s: i2c transfer error retry = "
				"%d\n", __func__, retry);
	}
	devdata->is_dead = true;
	devdata->pdata->touchkey_onoff(TOUCHKEY_OFF);
	dev_err(&devdata->client->dev, "%s: touchkey died\n", __func__);
out:
	dev_err(&devdata->client->dev, "%s: recovery_routine\n", __func__);
	return ret;
}

// Accidental touch key prevention (see mxt224.c)
extern unsigned int touch_state_val;

static irqreturn_t touchkey_interrupt_thread(int irq, void *touchkey_devdata)
{
	u8 data;
	int i;
	int ret;
	int scancode;
	struct cypress_touchkey_devdata *devdata = touchkey_devdata;

	ret = i2c_touchkey_read_byte(devdata, &data);
	if (ret || (data & ESD_STATE_MASK)) {
		ret = recovery_routine(devdata);
		if (ret) {
			dev_err(&devdata->client->dev, "%s: touchkey recovery "
					"failed!\n", __func__);
			goto err;
		}
	}

	if (devdata->has_legacy_keycode) {
		scancode = (data & SCANCODE_MASK) - 1;
		if (scancode < 0 || scancode >= devdata->pdata->keycode_cnt) {
			dev_err(&devdata->client->dev, "%s: scancode is out of "
				"range\n", __func__);
			goto err;
		}

		/* Don't send down event while the touch screen is being pressed
		 * to prevent accidental touch key hit.
		 */
		if ((data & UPDOWN_EVENT_MASK) || !touch_state_val) {
			input_report_key(devdata->input_dev,
				devdata->pdata->keycode[scancode],
				!(data & UPDOWN_EVENT_MASK));
		}
	} else {
		for (i = 0; i < devdata->pdata->keycode_cnt; i++)
			input_report_key(devdata->input_dev,
				devdata->pdata->keycode[i],
				!!(data & (1U << i)));
	}

	input_sync(devdata->input_dev);
	mod_timer(&bl_timer, jiffies + msecs_to_jiffies(BACKLIGHT_TIMEOUT));
err:
	return IRQ_HANDLED;
}

static irqreturn_t touchkey_interrupt_handler(int irq, void *touchkey_devdata)
{
	struct cypress_touchkey_devdata *devdata = touchkey_devdata;

	if (devdata->is_powering_on) {
		dev_dbg(&devdata->client->dev, "%s: ignoring spurious boot "
					"interrupt\n", __func__);
		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static void notify_led_on(void) {
	if (unlikely(bl_devdata->is_dead))
		return;

	if (bl_devdata->is_sleeping) {
		bl_devdata->pdata->touchkey_sleep_onoff(TOUCHKEY_ON);
		bl_devdata->pdata->touchkey_onoff(TOUCHKEY_ON);
	}
	i2c_touchkey_write_byte(bl_devdata, bl_devdata->backlight_on);
	bl_on = 1;
	printk(KERN_DEBUG "%s: notification led enabled\n", __FUNCTION__);
}

static void notify_led_off(void) {
	if (unlikely(bl_devdata->is_dead))
		return;

	// Avoid race condition with touch key resume
	down(&enable_sem);

	if (bl_on)
		i2c_touchkey_write_byte(bl_devdata, bl_devdata->backlight_off);

	bl_devdata->pdata->touchkey_sleep_onoff(TOUCHKEY_OFF);
	if (bl_devdata->is_sleeping)
		bl_devdata->pdata->touchkey_onoff(TOUCHKEY_OFF);

	bl_on = 0;

	up(&enable_sem);

	printk(KERN_DEBUG "%s: notification led disabled\n", __FUNCTION__);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cypress_touchkey_early_suspend(struct early_suspend *h)
{
	struct cypress_touchkey_devdata *devdata =
		container_of(h, struct cypress_touchkey_devdata, early_suspend);

	devdata->is_powering_on = true;

	if (unlikely(devdata->is_dead))
		return;

	disable_irq(devdata->client->irq);
	
#ifdef CONFIG_KEYPAD_CYPRESS_TOUCH_BLN
	/*
	 * Disallow powering off the touchkey controller
	 * while a led notification is ongoing
	 */
	if(!bln_notification_ongoing)
#endif
	devdata->pdata->touchkey_onoff(TOUCHKEY_OFF);
	all_keys_up(devdata);
	devdata->is_sleeping = true;
	if (bl_on)
		notify_led_on();
}

static void cypress_touchkey_early_resume(struct early_suspend *h)
{
	struct cypress_touchkey_devdata *devdata =
		container_of(h, struct cypress_touchkey_devdata, early_suspend);

	// Avoid race condition with LED notification disable
	down(&enable_sem);

	devdata->pdata->touchkey_onoff(TOUCHKEY_ON);

	if (i2c_touchkey_write_byte(devdata, devdata->backlight_on)) {
		devdata->is_dead = true;
		devdata->pdata->touchkey_onoff(TOUCHKEY_OFF);
		dev_err(&devdata->client->dev, "%s: touch keypad not responding"
				" to commands, disabling\n", __func__);
		return;
	}
	devdata->is_dead = false;
	enable_irq(devdata->client->irq);
	devdata->is_powering_on = false;
	devdata->is_sleeping = false;

	up(&enable_sem);

#ifdef CONFIG_KEYPAD_CYPRESS_TOUCH_BLN
	/*
	 * Disallow powering off the touchkey controller
	 * while a led notification is ongoing
	 */
	if(!bln_notification_ongoing) {

	mod_timer(&bl_timer, jiffies + msecs_to_jiffies(BACKLIGHT_TIMEOUT));	
	}
#endif

//	mod_timer(&bl_timer, jiffies + msecs_to_jiffies(BACKLIGHT_TIMEOUT));
}
#endif

static ssize_t led_status_read(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf,"%u\n", bl_on);
}

static ssize_t led_status_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if (sscanf(buf, "%u\n", &data)) {
		if (data == 1)
			notify_led_on();
		else
			notify_led_off();
	}
	return size;
}

static DEVICE_ATTR(led, S_IRUGO | S_IWUGO , led_status_read, led_status_write);

static struct attribute *bl_led_attributes[] = {
		&dev_attr_led.attr,
		NULL
};

static struct attribute_group bl_led_group = {
		.attrs  = bl_led_attributes,
};

static struct miscdevice bl_led_device = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "notification",
};

#ifdef CONFIG_KEYPAD_CYPRESS_TOUCH_BLN
/* bln start */

static void enable_touchkey_backlights(void){
	i2c_touchkey_write_byte(bln_devdata, bln_devdata->backlight_on);
}

static void disable_touchkey_backlights(void){
	i2c_touchkey_write_byte(bln_devdata, bln_devdata->backlight_off);
}

static void enable_led_notification(void){
	if (bln_enabled){
		/* is_powering_on signals whether touchkey lights are used for touchmode */
		pr_info("%s: bln interface enabled\n", __FUNCTION__); //remove me
		
		if (bln_devdata->is_powering_on){
		pr_info("%s: not in touchmode\n", __FUNCTION__); //remove me
			/* signal ongoing led notification */
			bln_notification_ongoing = true;

			/*
			 * power on the touchkey controller
			 * This is actually not needed, but it is intentionally
			 * left for the case that the early_resume() function
			 * did not power on the touchkey controller for some reasons
			 */
			pr_info("%s: enable vdd\n", __FUNCTION__); //remove me
			bln_devdata->pdata->touchkey_onoff(TOUCHKEY_ON);

			/* write to i2cbus, enable backlights */
			pr_info("%s: enable lights\n", __FUNCTION__); //remove me
			enable_touchkey_backlights();

			pr_info("%s: notification led enabled\n", __FUNCTION__);
		}
		else
			pr_info("%s: cannot set notification led, touchkeys are enabled\n",__FUNCTION__);
	}
}

static void disable_led_notification(void){
	pr_info("%s: notification led disabled\n", __FUNCTION__);

	/* disable the blink state */
	bln_blink_enabled = false;

	/* if touchkeys lights are not used for touchmode */
	if (bln_devdata->is_powering_on){
		disable_touchkey_backlights();
	}

	/* signal led notification is disabled */
	bln_notification_ongoing = false;
}

static ssize_t backlightnotification_status_read(struct device *dev, struct device_attribute *attr, char *buf) {
    return sprintf(buf,"%u\n",(bln_enabled ? 1 : 0));
}
static ssize_t backlightnotification_status_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;
	if(sscanf(buf, "%u\n", &data) == 1) {
		pr_devel("%s: %u \n", __FUNCTION__, data);
		if(data == 0 || data == 1){

			if(data == 1){
				pr_info("%s: backlightnotification function enabled\n", __FUNCTION__);
				bln_enabled = true;
			}

			if(data == 0){
				pr_info("%s: backlightnotification function disabled\n", __FUNCTION__);
				bln_enabled = false;
				if (bln_notification_ongoing)
					disable_led_notification();
			}
		}
		else
			pr_info("%s: invalid input range %u\n", __FUNCTION__, data);
	}
	else
		pr_info("%s: invalid input\n", __FUNCTION__);

	return size;
}

static ssize_t notification_led_status_read(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf,"%u\n", (bln_notification_ongoing ? 1 : 0));
}

static ssize_t notification_led_status_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if(sscanf(buf, "%u\n", &data) == 1) {
		if(data == 0 || data == 1){
			pr_devel("%s: %u \n", __FUNCTION__, data);
			if (data == 1)
				enable_led_notification();

			if(data == 0)
				disable_led_notification();

		} else
			pr_info("%s: wrong input %u\n", __FUNCTION__, data);
	} else
		pr_info("%s: input error\n", __FUNCTION__);

	return size;
}

static ssize_t blink_control_read(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf,"%u\n", (bln_blink_enabled ? 1 : 0));
}

static ssize_t blink_control_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;

	if(sscanf(buf, "%u\n", &data) == 1) {
		if(data == 0 || data == 1){
			if (bln_notification_ongoing){
				pr_devel("%s: %u \n", __FUNCTION__, data);
				if (data == 1){
					bln_blink_enabled = true;
					disable_touchkey_backlights();
				}

				if(data == 0){
					bln_blink_enabled = false;
					enable_touchkey_backlights();
				}
			}

		} else
			pr_info("%s: wrong input %u\n", __FUNCTION__, data);
	} else
		pr_info("%s: input error\n", __FUNCTION__);

	return size;
}

static ssize_t backlightnotification_version(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "%u\n", BACKLIGHTNOTIFICATION_VERSION);
}

static DEVICE_ATTR(blink_control, S_IRUGO | S_IWUGO , blink_control_read, blink_control_write);
static DEVICE_ATTR(enabled, S_IRUGO | S_IWUGO , backlightnotification_status_read, backlightnotification_status_write);
static DEVICE_ATTR(notification_led, S_IRUGO | S_IWUGO , notification_led_status_read, notification_led_status_write);
static DEVICE_ATTR(version, S_IRUGO , backlightnotification_version, NULL);

static struct attribute *bln_interface_attributes[] = {
		&dev_attr_blink_control.attr,
		&dev_attr_enabled.attr,
		&dev_attr_notification_led.attr,
		&dev_attr_version.attr,
		NULL
};

static struct attribute_group bln_interface_attributes_group = {
		.attrs  = bln_interface_attributes,
};

static struct miscdevice backlightnotification_device = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "backlightnotification",
};
/* bln end */
#endif


static int cypress_touchkey_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct input_dev *input_dev;
	struct cypress_touchkey_devdata *devdata;
	u8 data[3];
	int err;
	int cnt;

	if (!dev->platform_data) {
		dev_err(dev, "%s: Platform data is NULL\n", __func__);
		return -EINVAL;
	}

	devdata = kzalloc(sizeof(*devdata), GFP_KERNEL);
	if (devdata == NULL) {
		dev_err(dev, "%s: failed to create our state\n", __func__);
		return -ENODEV;
	}

	devdata->client = client;
	i2c_set_clientdata(client, devdata);

	devdata->pdata = client->dev.platform_data;
	if (!devdata->pdata->keycode) {
		dev_err(dev, "%s: Invalid platform data\n", __func__);
		err = -EINVAL;
		goto err_null_keycodes;
	}

	strlcpy(devdata->client->name, DEVICE_NAME, I2C_NAME_SIZE);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		goto err_input_alloc_dev;
	}

	devdata->input_dev = input_dev;
	dev_set_drvdata(&input_dev->dev, devdata);
	input_dev->name = DEVICE_NAME;
	input_dev->id.bustype = BUS_HOST;

	for (cnt = 0; cnt < devdata->pdata->keycode_cnt; cnt++)
		input_set_capability(input_dev, EV_KEY,
					devdata->pdata->keycode[cnt]);

	err = input_register_device(input_dev);
	if (err)
		goto err_input_reg_dev;

	devdata->is_powering_on = true;
	devdata->is_sleeping = false;

	devdata->pdata->touchkey_onoff(TOUCHKEY_ON);

	err = i2c_master_recv(client, data, sizeof(data));

	if (err < sizeof(data)) {
		if (err >= 0)
			err = -EIO;
		dev_err(dev, "%s: error reading hardware version\n", __func__);
		goto err_read;
	}

	dev_info(dev, "%s: hardware rev1 = %#02x, rev2 = %#02x\n", __func__,
				data[1], data[2]);

#ifdef CONFIG_KEYPAD_CYPRESS_TOUCH_HAS_LEGACY_KEYCODE
	devdata->has_legacy_keycode = true;
#else
	devdata->has_legacy_keycode = data[1] >= 0xc4 || data[1] < 0x9 ||
					(data[1] == 0x9 && data[2] < 0x9);
#endif

	if (data[1] < 0xc4 && (data[1] >= 0x8 ||
				(data[1] == 0x8 && data[2] >= 0x9)) && 
				devdata->has_legacy_keycode == false) {
		devdata->backlight_on = BACKLIGHT_ON;
		devdata->backlight_off = BACKLIGHT_OFF;
	} else {
		devdata->backlight_on = OLD_BACKLIGHT_ON;
		devdata->backlight_off = OLD_BACKLIGHT_OFF;
	}

	err = i2c_touchkey_write_byte(devdata, devdata->backlight_off);
	if (err) {
		dev_err(dev, "%s: touch keypad backlight on failed\n",
				__func__);
		/* The device may not be responding because of bad firmware
		 */
		goto err_backlight_off;
	}

	err = request_threaded_irq(client->irq, touchkey_interrupt_handler,
				touchkey_interrupt_thread, IRQF_TRIGGER_FALLING,
				DEVICE_NAME, devdata);
	if (err) {
		dev_err(dev, "%s: Can't allocate irq.\n", __func__);
		goto err_req_irq;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	devdata->early_suspend.suspend = cypress_touchkey_early_suspend;
	devdata->early_suspend.resume = cypress_touchkey_early_resume;
#endif
	register_early_suspend(&devdata->early_suspend);

	devdata->is_powering_on = false;

	if (misc_register(&bl_led_device))
		printk("%s misc_register(%s) failed\n", __FUNCTION__, bl_led_device.name);
	else {
		bl_devdata = devdata;
		if (sysfs_create_group(&bl_led_device.this_device->kobj, &bl_led_group) < 0)
			pr_err("failed to create sysfs group for device %s\n", bl_led_device.name);
	}

//	setup_timer(&bl_timer, bl_timer_callback, 0);

#ifdef CONFIG_KEYPAD_CYPRESS_TOUCH_BLN
	pr_info("%s misc_register(%s)\n", __FUNCTION__, backlightnotification_device.name);
	err = misc_register(&backlightnotification_device);
	if (err) {
		pr_err("%s misc_register(%s) fail\n", __FUNCTION__, backlightnotification_device.name);
	}else {
		/*
		 *  keep a reference to the devdata,
		 *  misc driver does not give access to it (or i missed that somewhere)
		 */
		bln_devdata = devdata;

		/* add the backlightnotification attributes */
		if (sysfs_create_group(&backlightnotification_device.this_device->kobj, &bln_interface_attributes_group) < 0)
		{
			pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
			pr_err("Failed to create sysfs group for device (%s)!\n", backlightnotification_device.name);
		}
	}
#endif

	setup_timer(&bl_timer, bl_timer_callback, 0);

	return 0;

err_req_irq:
err_backlight_off:
	input_unregister_device(input_dev);
	goto touchkey_off;
err_input_reg_dev:
err_read:
	input_free_device(input_dev);
touchkey_off:
	devdata->is_powering_on = false;
	devdata->pdata->touchkey_onoff(TOUCHKEY_OFF);
err_input_alloc_dev:
err_null_keycodes:
	kfree(devdata);
	return err;
}

static int __devexit i2c_touchkey_remove(struct i2c_client *client)
{
	struct cypress_touchkey_devdata *devdata = i2c_get_clientdata(client);
	
#ifdef CONFIG_KEYPAD_CYPRESS_TOUCH_BLN
	misc_deregister(&backlightnotification_device);
#endif

	dev_err(&client->dev, "%s: i2c_touchkey_remove\n", __func__);

	misc_deregister(&bl_led_device);

	unregister_early_suspend(&devdata->early_suspend);
	/* If the device is dead IRQs are disabled, we need to rebalance them */
	if (unlikely(devdata->is_dead))
		enable_irq(client->irq);
	else {
		devdata->pdata->touchkey_onoff(TOUCHKEY_OFF);
		devdata->is_powering_on = false;
	}
	free_irq(client->irq, devdata);
	all_keys_up(devdata);
	input_unregister_device(devdata->input_dev);
	del_timer(&bl_timer);
	kfree(devdata);
	return 0;
}

static const struct i2c_device_id cypress_touchkey_id[] = {
	{ CYPRESS_TOUCHKEY_DEV_NAME, 0 },
};

MODULE_DEVICE_TABLE(i2c, cypress_touchkey_id);

struct i2c_driver touchkey_i2c_driver = {
	.driver = {
		.name = "cypress_touchkey_driver",
	},
	.id_table = cypress_touchkey_id,
	.probe = cypress_touchkey_probe,
	.remove = __devexit_p(i2c_touchkey_remove),
};

static int __init touchkey_init(void)
{
	int ret = 0;

	ret = i2c_add_driver(&touchkey_i2c_driver);
	if (ret)
		pr_err("%s: cypress touch keypad registration failed. (%d)\n",
				__func__, ret);

	return ret;
}

static void __exit touchkey_exit(void)
{
	i2c_del_driver(&touchkey_i2c_driver);
}

late_initcall(touchkey_init);
module_exit(touchkey_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("@@@");
MODULE_DESCRIPTION("cypress touch keypad");
