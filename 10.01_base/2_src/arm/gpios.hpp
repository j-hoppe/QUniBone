/* gpios.hpp: Q/UniBone functions for Non-PRU GPIOs

 Copyright (c) 2018, Joerg Hoppe
 j_hoppe@t-online.de, www.retrocmp.com

 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 JOERG HOPPE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 12-nov-2018  JH      entered beta phase
 */

#ifndef _GPIOS_H_
#define _GPIOS_H_

#include <stdint.h>
#include <vector>
#include <string>
#include <assert.h>

#include "logsource.hpp"
#include "bitcalc.h"

// device for a set of 32 gpio pins
typedef struct {
	unsigned gpios_in_use; // so much pins from this bank are in use
	uint32_t registerrange_addr_unmapped; //unmapped 	start addr of GPIo register range
	unsigned registerrange_size;

	// start addr of register bank, mapped into memory
	volatile uint8_t *registerrange_start_addr;

	// address of Output Enable Register, mapped into memory.
	// bit<i> = 1: pin i is output, 0 = input
	volatile uint32_t *oe_addr;

	// Address of DATAIN register. Reflects the GPIO pin voltage levels.
	volatile uint32_t *datain_addr;

	// Address of DATAOUT register. Reflects the GPIO output pin voltage levels.
	volatile uint32_t *dataout_addr;

	uint32_t cur_dataout_val; // cached value of dataout register
	// access to this is much faster than the GPIO dataut itself.

// mapped address of "set pin" register
	// bit<i> := 1: gpio pin i is set to H level
	volatile uint32_t *setdataout_addr;

	// mapped address of "clear pin" register
	// bit<i> := 1: gpio pin i is set to H level
	volatile uint32_t *clrdataout_addr;

} gpio_bank_t;

/* access addresses for on gpio pin */
typedef struct {
	char name[80];
	unsigned tag; // marker for processing
	int internal;  // 1 = used internally, for P9.41, P).42 collision
	int direction;
	unsigned bank_idx; // GPIO=0,1,2,3
	gpio_bank_t *bank; // GPIO=0,1,2,3
	unsigned pin_in_bank; // 0..31
	unsigned pin_in_bank_mask; // mask with bit 'gpio_pin_in_bank' set
	unsigned linear_no; // linear index in /sys/class/gpio= 32*bank + pin_in_bnak

} gpio_config_t;

// direction of a pin
#define DIR_INPUT	0
#define DIR_OUTPUT	1

#define MAX_GPIOCOUNT	100

// test pins
// SET(1) -> pin auf H, LED OFF
#define ARM_DEBUG_PIN0(val)	GPIO_SETVAL(gpios->led[0], !!(val))
#define ARM_DEBUG_PIN1(val)	GPIO_SETVAL(gpios->led[1], !!(val))
#define ARM_DEBUG_PIN2(val)	GPIO_SETVAL(gpios->led[2], !!(val))
#define ARM_DEBUG_PIN3(val)	GPIO_SETVAL(gpios->led[3], !!(val))
#define ARM_DEBUG_PIN(n,val) GPIO_SETVAL(gpios->led[n], !!(val))

class gpios_c: public logsource_c {
private:
	void bank_map_registers(unsigned bank_idx, unsigned unmapped_start_addr);
	gpio_config_t *config(const char *name, int direction, unsigned bank_idx,
			unsigned pin_in_bank);
	void export_pin(gpio_config_t *pin);

public:

	gpios_c();
	// list of gpio register banks
	gpio_bank_t banks[4];

	// list of pins pins
	gpio_config_t *pins[MAX_GPIOCOUNT + 1];

	// mapped start addresses of register space for GPIOs
	volatile void *registerrange_start_addr[4];

	gpio_config_t *led[4], *swtch[4], *button, *reg_enable, *bus_enable, *i2c_panel_reset, *activity_led=NULL,
			*reg_addr[3], *reg_write, *reg_datin[8], *reg_datout[8], *collision_p9_41,
			*collision_p9_42;

	unsigned cmdline_leds ; // as set on call to application via cmdline options

	void init(void);
	void set_leds(unsigned number) ;
	void test_toggle(gpio_config_t *gpio);
	void test_loopback(void);
};
extern gpios_c *gpios; // singleton

// merges the bits of unshifted_val into the gpio output register
// of bank[idx]. target position is given by bitpos, bitfield size by bitmask
// This macro is optimized assuming that writing a memorymapped GPIO register
// takes much longer than ARM internal operations.
#define GPIO_OUTPUTBITS(bank,bitpos,bitmask,unshifted_val) do {	\
		unsigned tmp = (bank)->cur_dataout_val ; \
		tmp &= ~((bitmask) << (bitpos)) ; /* erase bitfield*/	\
		tmp |= ((unshifted_val) & (bitmask)) << (bitpos) ; /* copy val to bitfield*/ \
		(bank)->cur_dataout_val = *((bank)->dataout_addr) = tmp ; \
} while(0)

// set a value by writing a bit mask into output register
#define GPIO_SETVAL(gpio,val) GPIO_OUTPUTBITS((gpio)->bank, (gpio)->pin_in_bank, 1, (val) )

// get a value by reading value register
// Getval(input) reads pin. Getval(output) reads values set.
#define GPIO_GETVAL(gpio) 		(	\
	(gpio)->direction == DIR_OUTPUT ? 	\
 	(!! ( *((gpio)->bank->dataout_addr) & (gpio)->pin_in_bank_mask ) )	\
 	: (!! ( *((gpio)->bank->datain_addr) & (gpio)->pin_in_bank_mask ) )	\
	)


#endif // _GPIOS_H_
