/*
 *  Originally from linux/arch/arm/lib/delay.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/timex.h>

/*
 * Oh, if only we had a cycle counter...
 */
static void delay_loop(unsigned long loops)
{
	asm volatile(
	"1:	subs %0, %0, #1 \n"
	"	bhi 1b		\n"
	: /* No output */
	: "r" (loops)
	);
}

#ifdef ARCH_HAS_READ_CURRENT_TIMER
/*
 * Assumes read_current_timer() is monotonically increasing
 * across calls and wraps at most once within MAX_UDELAY_MS.
 */
void read_current_timer_delay_loop(unsigned long loops)
{
	unsigned long bclock, now;

	read_current_timer(&bclock);
	do {
		read_current_timer(&now);
	} while ((now - bclock) < loops);
}
#endif

void (*delay_fn)(unsigned long) = delay_loop;

/*
 * loops = usecs * HZ * loops_per_jiffy / 1000000
 */
void __delay(unsigned long loops)
{
	delay_fn(loops);
}
EXPORT_SYMBOL(__delay);

/*
 * 0 <= xloops <= 0x7fffff06
 * loops_per_jiffy <= 0x01ffffff (max. 3355 bogomips)
 */
void __const_udelay(unsigned long xloops)
{
	unsigned long mask = ULONG_MAX;
	unsigned long lpj = loops_per_jiffy;
	unsigned long loops;

	xloops += mask >> (32 - 14);
	xloops >>= 14;			/* max = 0x01ffffff */

	lpj += mask >> (32 - 10);
	lpj >>= 10;			/* max = 0x0001ffff */

	loops = lpj * xloops;		/* max = 0x00007fff */
	loops += mask >> (32 - 6);
	loops >>= 6;			/* max = 2^32-1 */

	if (likely(loops))
		__delay(loops);
}
EXPORT_SYMBOL(__const_udelay);

/*
 * usecs  <= 2000
 * HZ  <= 1000
 */
void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * ((2199023UL*HZ)>>11));
}
EXPORT_SYMBOL(__udelay);
