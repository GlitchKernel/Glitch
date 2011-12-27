#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include "kr3dh_reg.h"



#define TRACE_FUNC() pr_info(KR3DH_NAME ": <trace> %s()\n", __FUNCTION__)

/* The default settings when sensor is on is for all 3 axis to be enabled
 * and output data rate set to 400Hz.  Output is via a ioctl read call.
*/
#define DEFAULT_POWER_ON_SETTING (ODR400 | ENABLE_ALL_AXES)
#define READ_REPEAT_SHIFT       3
#define READ_REPEAT             (1 << READ_REPEAT_SHIFT)

#define DEVICE_ID		(0x32)
#define KR3DH_NAME		"kr3dh"
#define KR3DH_RESOLUTION	16384    /* [count/G] */
#define GRAVITY_EARTH                   9806550
#define ABSMIN_2G                       (-GRAVITY_EARTH * 2)
#define ABSMAX_2G                       (GRAVITY_EARTH * 2)



static const struct odr_delay {
        u8 odr; /* odr reg setting */
        s64 delay; /* odr in ns */
}kr3dh_odr_table [] = {
        {  ODR400,    2500000LL  }, /* 400Hz */
        {  ODR100,   10000000LL  }, /* 100Hz */
        {   ODR50,   20000000LL  }, /*  50Hz */
        {   ODR10,  100000000LL  }, /*  10Hz */
        {    ODR5,  200000000LL  }, /*   5Hz */
        {    ODR2,  500000000LL  }, /*   2Hz */
        {    ODR1, 1000000000LL  }, /*   1Hz */
        { ODRHALF, 2000000000LL  }, /* 0.5Hz */
};

#define event_delay(s)	(delay_to_jiffies(((s) / (1000 * 1000))))
#define delay_to_jiffies(d) ((d)?msecs_to_jiffies(d):1)
#define actual_delay(d)     (jiffies_to_msecs(delay_to_jiffies(d)))


/* KR3DM acceleration data */
struct acceleration {
        int axis[3];
};

/*
 * driver private data
 */
struct kr3dh_data {
        atomic_t enable;                /* attribute value */
        atomic_t delay;                 /* attribute value */
        atomic_t position;              /* attribute value */
        u8 ctrl_reg1_shadow;
        struct acceleration last;       /* last measured data */
        struct mutex enable_mutex;
        struct mutex data_mutex;
        struct i2c_client *client;
        struct input_dev *input;
        struct delayed_work work;
};


/*
 * Transformation matrix for chip mounting position
 */
static const int kr3dh_position_map[][3][3] = {
        {{ 0, -1,  0}, { 1,  0,  0}, { 0,  0,  1}}, /* top/upper-left */
        {{ 1,  0,  0}, { 0,  1,  0}, { 0,  0,  1}}, /* top/upper-right */
        {{ 0,  1,  0}, {-1,  0,  0}, { 0,  0,  1}}, /* top/lower-right */
        {{-1,  0,  0}, { 0, -1,  0}, { 0,  0,  1}}, /* top/lower-left */
        {{ 0,  1,  0}, { 1,  0,  0}, { 0,  0, -1}}, /* bottom/upper-right */
        {{-1,  0,  0}, { 0,  1,  0}, { 0,  0, -1}}, /* bottom/upper-left */
        {{ 0, -1,  0}, {-1,  0,  0}, { 0,  0, -1}}, /* bottom/lower-left */
        {{ 1,  0,  0}, { 0, -1,  0}, { 0,  0, -1}}, /* bottom/lower-right */
};



static int kr3dh_power_down(struct kr3dh_data *kr3dh)
{
        int res = 0;

        res = i2c_smbus_write_byte_data(kr3dh->client,
                                                CTRL_REG1, PM_OFF);
		
        res = i2c_smbus_write_byte_data(kr3dh->client,
                                                CTRL_REG5, 0x00);

        return res;
}

static int kr3dh_power_up(struct kr3dh_data *kr3dh)
{
        int res = 0;

        res = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG1,
                                                kr3dh->ctrl_reg1_shadow);
	
        res = i2c_smbus_write_byte_data(kr3dh->client,
                                                CTRL_REG5, 0x03);

        return res;

}

static int kr3dh_measure(struct kr3dh_data *kr3dh, struct acceleration *accel);

static int kr3dh_hw_init(struct kr3dh_data *kr3dh)
{
	int err = 0;

	kr3dh->ctrl_reg1_shadow = DEFAULT_POWER_ON_SETTING;
        err = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG1,
                                                DEFAULT_POWER_ON_SETTING);
        if (err) {
               pr_err("kr3dh_hw_init() i2c write ctrl_reg1 failed\n");
        }

	/* Disables Filters */
        err = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG2,0x00);
        if (err) {
               pr_err("kr3dh_hw_init() i2c write ctrl_reg2 failed\n");
	}

	/* Disable Interrupts */
        err = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG3,0x00);
        if (err) {
               pr_err("kr3dh_hw_init() i2c write ctrl_reg3 failed\n");
	}

	/*  Non Continous mode, Little Endian, 2G Range */
        err = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG4,0x80);
        if (err) {
               pr_err("kr3dh_hw_init() i2c write ctrl_reg4 failed\n");
	}

	/* Sleep to Wake: TurnON */ 
        err = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG5,0x03);
        if (err) {
               pr_err("kr3dh_hw_init() i2c write ctrl_reg5 failed\n");
	}		

	return err;
}

static int kr3dh_get_enable(struct device *dev)
{
        struct i2c_client *client = to_i2c_client(dev);
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);

        return atomic_read(&kr3dh->enable);

}

static void kr3dh_set_enable(struct device *dev, int enable)
{
        struct i2c_client *client = to_i2c_client(dev);
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);
        int delay = atomic_read(&kr3dh->delay);

        mutex_lock(&kr3dh->enable_mutex);

        if (enable) {                   /* enable if state will be changed */
                if (!atomic_cmpxchg(&kr3dh->enable, 0, 1)) {
                        kr3dh_power_up(kr3dh);
                        schedule_delayed_work(&kr3dh->work,
                                              event_delay(delay) + 1);
                }
        } else {                        /* disable if state will be changed */
                if (atomic_cmpxchg(&kr3dh->enable, 1, 0)) {
                        cancel_delayed_work_sync(&kr3dh->work);
                        kr3dh_power_down(kr3dh);
                }
        }
        atomic_set(&kr3dh->enable, enable);

        mutex_unlock(&kr3dh->enable_mutex);

}

static int kr3dh_get_delay(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct kr3dh_data *kr3dh = i2c_get_clientdata(client);

	 return atomic_read(&kr3dh->delay);
}

static void kr3dh_set_delay(struct device *dev, s64 delay)
{
        struct i2c_client *client = to_i2c_client(dev);
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);
        u8 odr_value;
        int i, res = 0;
	unsigned long delay_ms = 0;

        /* determine optimum ODR */
        for (i = 1; (i < ARRAY_SIZE(kr3dh_odr_table)) &&
                     (delay >= kr3dh_odr_table[i].delay); i++)
                ;
        odr_value = kr3dh_odr_table[i-1].odr;
        atomic_set(&kr3dh->delay, delay);

        mutex_lock(&kr3dh->enable_mutex);

        if (kr3dh_get_enable(dev)) {
                cancel_delayed_work_sync(&kr3dh->work);
	        if (odr_value != (kr3dh->ctrl_reg1_shadow & ODR_MASK)) {
	                u8 ctrl = (kr3dh->ctrl_reg1_shadow & ~ODR_MASK);
	                ctrl |= odr_value;
	                kr3dh->ctrl_reg1_shadow = ctrl;
	                res = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG1, ctrl);
	                printk("writing odr value 0x%x\n", odr_value);
	        }
		delay_ms = delay ;
		delay_ms = delay_ms / 1000;
		delay_ms = delay_ms / 1000;
		
                schedule_delayed_work(&kr3dh->work, 
						delay_to_jiffies(delay_ms) + 1);
        } else {
                kr3dh_power_up(kr3dh);
	        if (odr_value != (kr3dh->ctrl_reg1_shadow & ODR_MASK)) {
	                u8 ctrl = (kr3dh->ctrl_reg1_shadow & ~ODR_MASK);
	                ctrl |= odr_value;
	                kr3dh->ctrl_reg1_shadow = ctrl;
	                res = i2c_smbus_write_byte_data(kr3dh->client, CTRL_REG1, ctrl);
	                printk("writing odr value 0x%x\n", odr_value);
	        }

                kr3dh_power_down(kr3dh);
        }

        mutex_unlock(&kr3dh->enable_mutex);
}

static int kr3dh_set_position(struct device *dev, int position)
{
        struct i2c_client *client = to_i2c_client(dev);
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);
	
	return atomic_set(&kr3dh->position, position);

}

static int kr3dh_get_position(struct device *dev)
{
        struct i2c_client *client = to_i2c_client(dev);
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);
	
	return atomic_read(&kr3dh->position);

}
int kr3dh_i2c_byte_read(struct i2c_client *client, u8 command)
{
        union i2c_smbus_data data;
        int status;

        status = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
                                I2C_SMBUS_READ, command,
                                I2C_SMBUS_BYTE_DATA, &data);
	if (status < 0) {
		pr_err("I2C read error: acceleration not read\n");
	}
        return (status < 0) ? status : data.byte;
}

static int kr3dh_measure(struct kr3dh_data *kr3dh, struct acceleration *accel)
{
	int i =0;
	int j = 0;
	int value;
	int pos = (int)atomic_read(&kr3dh->position);
	unsigned char buf[6] = {0};
	short int raw[3] = {0};
	long long g;

	for (i = 0; i < 6; i++) {
        	buf[i] = kr3dh_i2c_byte_read(kr3dh->client, AXISDATA_REG + i);
	}
	for (i = 0; i < 3; i++) {
		raw[i] = (unsigned short)(buf[i*2+1]<<8) + (unsigned short)buf[i*2];
	}
	for (i = 0; i < 3; i++) {
		value = 0;
		for (j = 0; j < 3; j++) {
			value += kr3dh_position_map[pos][i][j] * (int)raw[j]; 
		}
		/* normalisation*/
		g = (long long)value * GRAVITY_EARTH / KR3DH_RESOLUTION;
		accel->axis[i] = g;	 
	}
	 
	return 0;
}

static void kr3dh_work_func(struct work_struct *work)
{
        struct kr3dh_data *kr3dh = container_of((struct delayed_work *)work,
                                                  struct kr3dh_data, work);
        struct acceleration accel;
        unsigned long delay = event_delay(atomic_read(&kr3dh->delay));

        kr3dh_measure(kr3dh, &accel);

        input_report_rel(kr3dh->input, REL_X, accel.axis[0]);
        input_report_rel(kr3dh->input, REL_Y, accel.axis[1]);
        input_report_rel(kr3dh->input, REL_Z, accel.axis[2]);
        input_sync(kr3dh->input);

        mutex_lock(&kr3dh->data_mutex);
        kr3dh->last = accel;
        mutex_unlock(&kr3dh->data_mutex);

        schedule_delayed_work(&kr3dh->work, delay);

}

/*
 * Input device interface
 */
static int kr3dh_input_init(struct i2c_client *client, struct kr3dh_data *kr3dh)
{
        struct input_dev *dev;
        int err;

        dev = input_allocate_device();
        if (!dev) {
                return -ENOMEM;
        }
        dev->name = "accelerometer_sensor";
        dev->id.bustype = BUS_I2C;

        /* X */
        input_set_capability(dev, EV_REL, REL_X);
        input_set_abs_params(dev, REL_X, ABSMIN_2G, ABSMAX_2G, 0, 0);
        /* Y */
        input_set_capability(dev, EV_REL, REL_Y);
        input_set_abs_params(dev, REL_Y, ABSMIN_2G, ABSMAX_2G, 0, 0);
        /* Z */
        input_set_capability(dev, EV_REL, REL_Z);
        input_set_abs_params(dev, REL_Z, ABSMIN_2G, ABSMAX_2G, 0, 0);
	input_set_drvdata(dev, kr3dh);
	dev->dev.parent = &client->dev;

        err = input_register_device(dev);
        if (err < 0) {
                input_free_device(dev);
                return err;
        }
        kr3dh->input = dev;

        return 0;

}

static void kr3dh_input_fini(struct kr3dh_data *kr3dh)
{
        struct input_dev *dev = kr3dh->input;

        input_unregister_device(dev);
        input_free_device(dev);

}

/*
 * sysfs device attributes
 */

static ssize_t kr3dh_enable_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
       return sprintf(buf, "%d\n", kr3dh_get_enable(dev));
}

static ssize_t kr3dh_enable_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t count)
{
        unsigned long enable = simple_strtoul(buf, NULL, 10);

        if ((enable == 0) || (enable == 1)) {
                kr3dh_set_enable(dev, enable);
        }

        return count;

}

static ssize_t kr3dh_delay_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%d\n", kr3dh_get_delay(dev));
}

static ssize_t kr3dh_delay_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t count)
{
        unsigned long delay = simple_strtoul(buf, NULL, 10);

        kr3dh_set_delay(dev, delay);

        return count;
}

static ssize_t kr3dh_position_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%d\n", kr3dh_get_position(dev));
}

static ssize_t kr3dh_position_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf, size_t count)
{
        unsigned long position;

        position = simple_strtoul(buf, NULL,10);
        if ((position >= 0) && (position <= 7)) {
                kr3dh_set_position(dev, position);
        }

        return count;
}

static ssize_t kr3dh_wake_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count)
{
        struct input_dev *input = to_input_dev(dev);
        static atomic_t serial = ATOMIC_INIT(0);

        input_report_abs(input, ABS_MISC, atomic_inc_return(&serial));

        return count;

}

static ssize_t kr3dh_data_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
        struct input_dev *input = to_input_dev(dev);
        struct kr3dh_data *kr3dh = input_get_drvdata(input);
        struct acceleration accel;

        mutex_lock(&kr3dh->data_mutex);
        accel = kr3dh->last;
        mutex_unlock(&kr3dh->data_mutex);

        return sprintf(buf, "%d %d %d\n", accel.axis[0], accel.axis[1], accel.axis[2]);
}

static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP,
                   kr3dh_enable_show, kr3dh_enable_store);
static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP,
                   kr3dh_delay_show, kr3dh_delay_store);
static DEVICE_ATTR(position, S_IRUGO|S_IWUSR,
                   kr3dh_position_show, kr3dh_position_store);
static DEVICE_ATTR(wake, S_IWUSR|S_IWGRP,
                   NULL, kr3dh_wake_store);
static DEVICE_ATTR(data, S_IRUGO,
                   kr3dh_data_show, NULL);

static struct attribute *kr3dh_attributes[] = {
        &dev_attr_enable.attr,
        &dev_attr_delay.attr,
        &dev_attr_position.attr,
        &dev_attr_wake.attr,
        &dev_attr_data.attr,
        NULL
};

static struct attribute_group kr3dh_attribute_group = {
        .attrs = kr3dh_attributes
};


static int kr3dh_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	int ret = 0;
	int err = 0;
       /* read chip id */
        ret = i2c_smbus_read_byte_data(client, WHO_AM_I);

	printk("KR3DH: DEVICE_ID -> %x\n", ret);
        if (ret != DEVICE_ID) {
                if (ret < 0) {
                        pr_err("%s: i2c for reading chip id failed\n",
                                                                __func__);
                        err = ret;
                } else {
                        pr_err("%s : Device identification failed\n",
                                                                __func__);
                        err = -ENODEV;
                }
        }
	return err;
}

static int kr3dh_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
        struct kr3dh_data *kr3dh;
        int err;

        /* setup private data */
        kr3dh = kzalloc(sizeof(struct kr3dh_data), GFP_KERNEL);
        if (!kr3dh) {
                err = -ENOMEM;
                goto error_0;
        }
        mutex_init(&kr3dh->enable_mutex);
        mutex_init(&kr3dh->data_mutex);

        /* setup i2c client */
        if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
						      I2C_FUNC_SMBUS_READ_BYTE_DATA)) 
	{
                err = -ENODEV;
                goto error_1;
        }
        i2c_set_clientdata(client, kr3dh);
        kr3dh->client = client;
	
        /* detect and init hardware */
        if ((err = kr3dh_detect(client, NULL))) {
                goto error_1;
        }
        dev_info(&client->dev, "%s found\n", id->name);

	kr3dh_hw_init(kr3dh);
        kr3dh_set_delay(&client->dev, 20000000LL);
        kr3dh_set_position(&client->dev, 2);
	
	/* setup driver interfaces */
        INIT_DELAYED_WORK(&kr3dh->work, kr3dh_work_func);

        err = kr3dh_input_init(client, kr3dh);
        if (err < 0) {
                goto error_1;
        }

        err = sysfs_create_group(&kr3dh->input->dev.kobj, &kr3dh_attribute_group);
        if (err < 0) {
                goto error_2;
        }

        return 0;

error_2:
        kr3dh_input_fini(kr3dh);
error_1:
        kfree(kr3dh);
error_0:
        return err;



}

static int kr3dh_remove(struct i2c_client *client)
{
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);

        kr3dh_set_enable(&client->dev, 0);

        sysfs_remove_group(&kr3dh->input->dev.kobj, &kr3dh_attribute_group);
        kr3dh_input_fini(kr3dh);
        kfree(kr3dh);

	return 0;
}

static int kr3dh_suspend(struct i2c_client *client, pm_message_t mesg)
{
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);

        TRACE_FUNC();

        mutex_lock(&kr3dh->enable_mutex);

        if (kr3dh_get_enable(&client->dev)) {
                cancel_delayed_work_sync(&kr3dh->work);
                kr3dh_power_down(kr3dh);
        }
        mutex_unlock(&kr3dh->enable_mutex);

	return 0;
}

static int kr3dh_resume(struct i2c_client *client)
{
        struct kr3dh_data *kr3dh = i2c_get_clientdata(client);
        int delay = atomic_read(&kr3dh->delay);

        TRACE_FUNC();

        kr3dh_hw_init(kr3dh);
        kr3dh_set_delay(&client->dev, delay);

        mutex_lock(&kr3dh->enable_mutex);

        if (kr3dh_get_enable(&client->dev)) {
                kr3dh_power_up(kr3dh);
                schedule_delayed_work(&kr3dh->work,
                                      event_delay(delay) + 1);
	}
        mutex_unlock(&kr3dh->enable_mutex);
	return 0;
}

static const struct i2c_device_id kr3dh_id[] = {
        {KR3DH_NAME, 0},
        {},
};

MODULE_DEVICE_TABLE(i2c, kr3dh_id);

struct i2c_driver kr3dh_driver ={
        .driver = {
                .name = "kr3dh",
                .owner = THIS_MODULE,
        },
        .probe = kr3dh_probe,
        .remove = kr3dh_remove,
        .suspend = kr3dh_suspend,
        .resume = kr3dh_resume,
        .id_table = kr3dh_id,
};

/*
 * Module init and exit
 */
static int __init kr3dh_init(void)
{
        return i2c_add_driver(&kr3dh_driver);
}
module_init(kr3dh_init);

static void __exit kr3dh_exit(void)
{
        i2c_del_driver(&kr3dh_driver);
}
module_exit(kr3dh_exit);

MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("KR3DH 3 axis accelerometer driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

