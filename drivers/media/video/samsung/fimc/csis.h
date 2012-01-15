/* linux/drivers/media/video/samsung/csis.h
 *
 * Copyright (c) 2010 Samsung Electronics Co,. Ltd.
 *		http://www.samsung.com/
 *
 * Header file for Samsung MIPI-CSI2 driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __CSIS_H
#define __CSIS_H __FILE__

#define S3C_CSIS_NAME		"s5p-mipi-csis"
#define S3C_CSIS_NR_LANES	1

#define info(args...)	\
	do { printk(KERN_INFO S3C_CSIS_NAME ": " args); } while (0)
#define err(args...)	\
	do { printk(KERN_ERR  S3C_CSIS_NAME ": " args); } while (0)

enum mipi_format {
	MIPI_CSI_YCBCR422_8BIT	= 0x1e,
	MIPI_CSI_RAW8		= 0x2a,
	MIPI_CSI_RAW10		= 0x2b,
	MIPI_CSI_RAW12		= 0x2c,
	MIPI_USER_DEF_PACKET_1	= 0x30,	/* User defined Byte-based packet 1 */
};

struct s3c_csis_info {
	char		name[16];
	struct device	*dev;
	struct clk	*clock;
	struct regulator *regulator;
	void __iomem	*regs;
	int		irq;
	int		nr_lanes;
};

#endif /* __CSIS_H */
