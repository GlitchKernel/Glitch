/* arch/arm/mach-s5pv210/include/mach/cpuidle.h
 *
 * Copyright 2010 Samsung Electronics
 *	Jaecheol Lee <jc.lee@samsung>
 *
 * S5PV210 - CPUIDLE support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define NORMAL_MODE	0
#define LOWPOWER_MODE	1

extern int previous_idle_mode;
extern int didle_lock_count;
extern int s5p_setup_lowpower(unsigned int mode);
extern void s5p_set_lowpower_lock(int flag);
extern int s5p_get_lowpower_lock(void);

extern int  s5pv210_didle_save(unsigned long *saveblk);
extern void s5pv210_didle_resume(void);
extern void i2sdma_getpos(dma_addr_t *src);
extern unsigned int get_rtc_cnt(void);
