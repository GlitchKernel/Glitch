/* include/linux/bln.h */

#ifndef _LINUX_BLN_H
#define _LINUX_BLN_H

/**
 * struct bln_implementation - a structure containing BLN controls
 * @enable: enables the leds given by ledmask
 * @disable: disables the leds given by ledmask
 * @power_on: powers on the components to enable the leds
 * @power_off: powers off the components
 * @led_count: number of leds in this bln implementation (see ledmask)
 *
 * The BLN implementation structure contains all LED control functions of an
 * touchkey device.
 *
 * Enable/Disable functions should only affect the leds given by the ledmask.
 * It should not configure components necessary for powering the leds (e.g.
 * regulators, GPIOs).
 *
 * Ledmask: the least significant bit is the first led, e.g. 0x1 is the left
 * led, on the Nexus S is that the back button.
 *
 * Power On/Off functions take care of the necessary components that needs to
 * be (re-)configured so that enable/disable of leds could operate.
 */
struct bln_implementation {
	int (*enable)(int led_mask);
	int (*disable)(int led_mask);
	int (*power_on)(void);
	int (*power_off)(void);
	unsigned int led_count;
};

void register_bln_implementation(struct bln_implementation *imp);
bool bln_is_ongoing(void);
#endif
