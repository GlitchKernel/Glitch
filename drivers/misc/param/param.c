/*
 * param.c
 *
 * Parameter read & save driver on param partition.
 *
 * COPYRIGHT(C) Samsung Electronics Co.Ltd. 2006-2010 All Right Reserved.  
 *
 * Author: Jeonghwan Min <jeonghwan.min@samsung.com>
 *
 * 2008.02.26. Supprot for BML layer.
 * 2009.12.07. Modified to support for FSR_BML
 * 2010.04.22. Remove FSR_BML
 * 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <linux/file.h>
#include <mach/hardware.h>
#include <mach/param.h>

#define PAGE_LEN	(4 * 1024)  /* 4KB */
#define UNIT_LEN	(256 * 1024)  /* 256KB OneNand unit size */
#define IMAGE_LEN	(192 * 1024)	/* 192KB, size of image area in PARAM block */
#define PARAM_LEN	(32 * 2048)  /* 64KB */
#define PARAM_PART_ID	0x5	// param ID is 2

#define kloge(fmt, arg...)  printk(KERN_ERR "%s(%d): " fmt "\n" , __func__, __LINE__, ## arg)
#define klogi(fmt, arg...)  printk(KERN_INFO fmt "\n" , ## arg)

#define PARAM_PROCFS_DEBUG
extern int factorytest;

#ifdef PARAM_PROCFS_DEBUG
struct proc_dir_entry *param_dir;
#endif

#define PARAM_USE_INIT_BUFFER

#ifdef PARAM_USE_INIT_BUFFER
static unsigned char *param_buf = NULL;
static unsigned char *image_buf = NULL;
#endif

/* added by geunyoung for LFS. */
static int load_lfs_param_value(void);
static int save_lfs_param_value(void);

static int param_check(unsigned char *addr)
{
	status_t 		*status;
	status = (status_t *)addr;
	
	if ((status->param_magic == PARAM_MAGIC) &&
			(status->param_version == PARAM_VERSION)) {
		klogi("Checking PARAM... OK");
		return 0;
	}

	klogi("Checking PARAM... Invalid");
	return -1;
}

static status_t param_status;

void set_param_value(int idx, void *value)
{
	int i, str_i;

	klogi("inside set_param_value1 idx = %d, value = %d", idx, value);

	for (i = 0; i < MAX_PARAM; i++) {
		if (i < (MAX_PARAM - MAX_STRING_PARAM)) {	
			if(param_status.param_list[i].ident == idx) {
				param_status.param_list[i].value = *(int *)value;
			}
		}
		else {
			str_i = (i - (MAX_PARAM - MAX_STRING_PARAM));
			if(param_status.param_str_list[str_i].ident == idx) {
				strlcpy(param_status.param_str_list[str_i].value, 
					(char *)value, PARAM_STRING_SIZE);
			}
		}
	}

	save_lfs_param_value();
}
EXPORT_SYMBOL(set_param_value);

void get_param_value(int idx, void *value)
{
	int i, str_i;

	for (i = 0 ; i < MAX_PARAM; i++) {
		if (i < (MAX_PARAM - MAX_STRING_PARAM)) {	
			if(param_status.param_list[i].ident == idx) {
				*(int *)value = param_status.param_list[i].value;
			}
		}
		else {
			str_i = (i - (MAX_PARAM - MAX_STRING_PARAM));
			if(param_status.param_str_list[str_i].ident == idx) {
				strlcpy((char *)value, 
					param_status.param_str_list[str_i].value, PARAM_STRING_SIZE);
			}
		}
	}
}
EXPORT_SYMBOL(get_param_value);

static void param_set_default(void)
{
	memset(&param_status, 0, sizeof(status_t));

	param_status.param_magic = PARAM_MAGIC;
	param_status.param_version = PARAM_VERSION;
	param_status.param_list[0].ident = __SERIAL_SPEED;
	param_status.param_list[0].value = SERIAL_SPEED;
	param_status.param_list[1].ident = __LOAD_RAMDISK;
	param_status.param_list[1].value = LOAD_RAMDISK;
	param_status.param_list[2].ident = __BOOT_DELAY;
	param_status.param_list[2].value = BOOT_DELAY;
	param_status.param_list[3].ident = __LCD_LEVEL;
	param_status.param_list[3].value = LCD_LEVEL;
	param_status.param_list[4].ident = __SWITCH_SEL;
	param_status.param_list[4].value = SWITCH_SEL;
	param_status.param_list[5].ident = __PHONE_DEBUG_ON;
	param_status.param_list[5].value = PHONE_DEBUG_ON;
	param_status.param_list[6].ident = __LCD_DIM_LEVEL;
	param_status.param_list[6].value = LCD_DIM_LEVEL;
	param_status.param_list[7].ident = __LCD_DIM_TIME;
	param_status.param_list[7].value = LCD_DIM_TIME;
	param_status.param_list[8].ident = __MELODY_MODE;
	param_status.param_list[8].value = MELODY_MODE;
	param_status.param_list[9].ident = __REBOOT_MODE;
	param_status.param_list[9].value = REBOOT_MODE;
	param_status.param_list[10].ident = __NATION_SEL;
	param_status.param_list[10].value = NATION_SEL;
	param_status.param_list[11].ident = __LANGUAGE_SEL;
	param_status.param_list[11].value = LANGUAGE_SEL;
	param_status.param_list[12].ident = __SET_DEFAULT_PARAM;
	param_status.param_list[12].value = SET_DEFAULT_PARAM;
	param_status.param_str_list[0].ident = __VERSION;
	strlcpy(param_status.param_str_list[0].value,
			VERSION_LINE, PARAM_STRING_SIZE);
	param_status.param_str_list[1].ident = __CMDLINE;
	strlcpy(param_status.param_str_list[1].value,
			COMMAND_LINE, PARAM_STRING_SIZE);
}

#ifdef PARAM_PROCFS_DEBUG
static void param_show_info(void)
{
	klogi("-----------------------------------------------------");
	klogi("	Information of Parameters");
	klogi("-----------------------------------------------------");
	klogi("  -     param_magic	  : 0x%x", param_status.param_magic);
	klogi("  -     param_version	  : 0x%x", param_status.param_version);
	klogi("  - %2d. SERIAL_SPEED	  : %d", param_status.param_list[0].ident, param_status.param_list[0].value);
	klogi("  - %2d. LOAD_RAMDISK	  : %d", param_status.param_list[1].ident, param_status.param_list[1].value);
	klogi("  - %2d. BOOT_DELAY	  : %d", param_status.param_list[2].ident, param_status.param_list[2].value);
	klogi("  - %2d. LCD_LEVEL	  : %d", param_status.param_list[3].ident, param_status.param_list[3].value);
	klogi("  - %2d. SWITCH_SEL	  : %d", param_status.param_list[4].ident, param_status.param_list[4].value);
	klogi("  - %2d. PHONE_DEBUG_ON	  : %d", param_status.param_list[5].ident, param_status.param_list[5].value);
	klogi("  - %2d. LCD_DIM_LEVEL	  : %d", param_status.param_list[6].ident, param_status.param_list[6].value);
	klogi("  - %2d. LCD_DIM_TIME	  : %d", param_status.param_list[7].ident, param_status.param_list[7].value);
	klogi("  - %2d. MELODY_LEVEL	  : %d", param_status.param_list[8].ident, param_status.param_list[8].value);
	klogi("  - %2d. REBOOT_MODE	  : %d", param_status.param_list[9].ident, param_status.param_list[9].value);
	klogi("  - %2d. NATION_SEL	  : %d", param_status.param_list[10].ident, param_status.param_list[10].value);
	klogi("  - %2d. LANGUAGE_SEL	  : %d", param_status.param_list[11].ident, param_status.param_list[11].value);
	klogi("  - %2d. SET_DEFAULT_PARAM  : %d", param_status.param_list[12].ident, param_status.param_list[12].value);
	klogi("  - %2d. VERSION(STR)	  : %s", param_status.param_str_list[0].ident, param_status.param_str_list[0].value);
	klogi("  - %2d. CMDLINE(STR)	  : %s", param_status.param_str_list[1].ident, param_status.param_str_list[1].value);
	klogi("-----------------------------------------------------");
}

/* test codes for debugging */
static int param_run_test(void)
{
#if 1  /* For the purpose of testing... */
	int val=3;
	if (!sec_set_param_value)
		return -1;

	sec_set_param_value(__SWITCH_SEL, &val);
#endif
	return 0;
}

static int param_lfs_run_test(void)
{
	return load_lfs_param_value();
}

static int param_read_proc_debug(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
	*eof = 1;
	return sprintf(page, "0. show parameters\n\
1. initialize parameters\n\
2. run test function\n\
example: echo [number] > /proc/param/debug\n");
}

static int param_write_proc_debug(struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	char *buf;

	if (count < 1)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		kfree(buf);
		return -EFAULT;
	}

	switch(buf[0]) {
		case '0':
			param_show_info();
			break;
		case '1':
			param_set_default();
			save_lfs_param_value();
			klogi("Parameters have been set as DEFAULT values...");
			param_show_info();
			break;
		case '2':
			param_run_test();
			break;
		case '3':
			param_lfs_run_test();
			break;
		default:
			kfree(buf);
			return -EINVAL;			
	}

	kfree(buf);
	return count;
}
#endif  /* PARAM_PROCFS_DEBUG */

static int param_init(void)
{
	int ret;

#ifdef PARAM_USE_INIT_BUFFER
	param_buf = kmalloc(PARAM_LEN, GFP_KERNEL);
	if (!param_buf) {
		kloge("Unable to alloc param_buf!");
		return -ENOMEM;
	}

	image_buf = kmalloc(IMAGE_LEN, GFP_KERNEL);
	if (!image_buf) {
		kloge("Unable to alloc image_buf!");
		kfree(param_buf);
		return -ENOMEM;
	}
#endif

#ifdef PARAM_PROCFS_DEBUG
	struct proc_dir_entry *ent;

	/* Creats '/proc/param' directory */
	param_dir = proc_mkdir("param", NULL);
	if (param_dir == NULL) {
		kloge("Unable to create /proc/param directory\n");
		return -ENOMEM;
	}

	/* Creats RW '/proc/param/sleep/debug' entry */
	ent = create_proc_entry("debug", 0, param_dir);
	if (ent == NULL) {
		kloge("Unable to create /proc/param/debug entry");
		ret = -ENOMEM;
		goto fail;
	}
	ent->read_proc = param_read_proc_debug;
	ent->write_proc = param_write_proc_debug;
#endif

	klogi("param_init");

#if 0
	ret = load_param_value();
#else
	ret = load_lfs_param_value();
#endif
	if (ret < 0) {
		kloge("Loading parameters failed. Parameters have been initialized as default.");
		param_set_default();
	}

	sec_set_param_value = set_param_value;
	sec_get_param_value = get_param_value;

	return 0;

#ifdef PARAM_PROCFS_DEBUG
fail:
	remove_proc_entry("param", 0);

#ifdef PARAM_USE_INIT_BUFFER
	kfree(param_buf);
	kfree(image_buf);
#endif
	
	return ret;
#endif
}

static void param_exit(void)
{
	klogi("param_exit");

#ifdef PARAM_USE_INIT_BUFFER
	kfree(param_buf);
	kfree(image_buf);
#endif

#ifdef PARAM_PROCFS_DEBUG
	remove_proc_entry("debug", param_dir);
	remove_proc_entry("param", 0);
#endif
}

module_init(param_init);
module_exit(param_exit);

/* added by geunyoung for LFS. */
#define PARAM_FILE_NAME	"/mnt/.lfs/param.blk"
#define PARAM_RD	0
#define PARAM_WR	1

static int lfs_param_op(int dir, int flags)
{
	struct file *filp;
	mm_segment_t fs;

	int ret;

	filp = filp_open(PARAM_FILE_NAME, flags, 0);

	if (IS_ERR(filp)) {
		pr_err("%s: filp_open failed. (%ld)\n", __FUNCTION__,
				PTR_ERR(filp));

		return -1;
	}

	fs = get_fs();
	set_fs(get_ds());

	if (dir == PARAM_RD)
		ret = filp->f_op->read(filp, (char __user *)&param_status,
				sizeof(param_status), &filp->f_pos);
	else
		ret = filp->f_op->write(filp, (char __user *)&param_status,
				sizeof(param_status), &filp->f_pos);

	set_fs(fs);
	filp_close(filp, NULL);

	return ret;
}

static int load_lfs_param_value(void)
{
	int ret;

	ret = lfs_param_op(PARAM_RD, O_RDONLY);

	if (ret == sizeof(param_status)) {
		pr_info("%s: param.blk read successfully.\n", __FUNCTION__);
	}

	return ret;
}

static int save_lfs_param_value(void)
{
	int ret;

	ret = lfs_param_op(PARAM_WR, O_RDWR|O_SYNC);

	if (ret == sizeof(param_status)) {
		pr_info("%s: param.blk write successfully.\n", __FUNCTION__);
	}

	return 0;
}



