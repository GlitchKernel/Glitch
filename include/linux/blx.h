/* include/linux/blx.h */

#ifndef _LINUX_BLX_H
#define _LINUX_BLX_H

// for kernels including the 100% charging 'fix' change this to 100:
#define MAX_CHARGINGLIMIT 100

int get_charginglimit(void);

#endif
