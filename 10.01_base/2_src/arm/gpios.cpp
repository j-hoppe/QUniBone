/* gpios_q.cpp: QBone functions for ARM and PRU GPIOs

 Copyright (c) 2018-2020, Joerg Hoppe
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


 21-may-2019  JH      added UNIBUS signals
 12-nov-2018  JH      entered beta phase
 */

#define _GPIOS_CPP_

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "mailbox.h"

#include "pru.hpp"
#include "utils.hpp"
#include "logsource.hpp"
#include "logger.hpp"

#include "gpios.hpp"

// see spruh73n.pdf
// address range of GPIO related registers. [spruh73n] page 181
#define GPIO_SIZE 	0x1000 // size of each GPIO register range
#define GPIO0_START_ADDR 0x44E07000
#define GPIO1_START_ADDR 0x4804C000
#define GPIO2_START_ADDR 0x481AC000
#define GPIO3_START_ADDR 0x481AE000 // offset in each GPIO register range
#define GPIO_OE_ADDROFFSET 0x134
#define GPIO_DATAIN_ADDROFFSET 0x138
#define GPIO_DATAOUT_ADDROFFSET 0x13c
#define GPIO_SETDATAOUT_ADDROFFSET 0x194
#define GPIO_CLEARDATAOUT_ADDROFFSET 0x190

gpios_c *gpios; // Singleton

gpios_c::gpios_c() {
	log_label = "GPIOS";
	
	cmdline_leds = 0 ; // is set before init()
}

/* fill the 4 gpio_banks with values and
 *	map addresses
 */
void gpios_c::bank_map_registers(unsigned bank_idx, unsigned unmapped_start_addr) {
	int fd;
	gpio_bank_t *bank;

	assert(bank_idx < 4);
	fd = open((char*) "/dev/mem", O_RDWR);
	if (!fd)
		FATAL("Can not open /dev/mem");

	bank = &(banks[bank_idx]);
	bank->gpios_in_use = 0;
	bank->registerrange_addr_unmapped = unmapped_start_addr; // info only
	INFO("GPIO%d registers at %X - %X (size = %X)", bank_idx, unmapped_start_addr,
			unmapped_start_addr + GPIO_SIZE - 1, GPIO_SIZE);
	bank->registerrange_start_addr = (uint8_t *) mmap(0, GPIO_SIZE, PROT_READ | PROT_WRITE,
	MAP_SHARED, fd, unmapped_start_addr);
	if (bank->registerrange_start_addr == MAP_FAILED)
		FATAL("Unable to map GPIO%d", bank_idx);

	bank->oe_addr = (uint32_t *) (bank->registerrange_start_addr + GPIO_OE_ADDROFFSET);
	bank->datain_addr = (uint32_t *) (bank->registerrange_start_addr + GPIO_DATAIN_ADDROFFSET);
	bank->dataout_addr =
			(uint32_t *) (bank->registerrange_start_addr + GPIO_DATAOUT_ADDROFFSET);
	bank->setdataout_addr = (uint32_t *) (bank->registerrange_start_addr
			+ GPIO_SETDATAOUT_ADDROFFSET);
	bank->clrdataout_addr = (uint32_t *) (bank->registerrange_start_addr
			+ GPIO_CLEARDATAOUT_ADDROFFSET);
}

gpio_config_t *gpios_c::config(const char *name, int direction, unsigned bank_idx,
		unsigned pin_in_bank) {
	gpio_config_t *result = (gpio_config_t *) malloc(sizeof(gpio_config_t));
	if (name)
		strcpy(result->name, name);
	else
		strcpy(result->name, "");
	if (strlen(result->name) > 0)
		result->internal = 0;
	else
		result->internal = 1;
	result->direction = direction;
	assert(bank_idx < 4);
	result->bank_idx = bank_idx;
	result->bank = &(banks[bank_idx]);
	result->bank->gpios_in_use++;
	result->pin_in_bank = pin_in_bank;
	result->linear_no = 32 * bank_idx + pin_in_bank;
	result->pin_in_bank_mask = 1 << pin_in_bank;

	return result;
}

// "export" a pin over the sys file system
// this is necessary for GPIO2&3, despite we operate memory mapped.
void gpios_c::export_pin(gpio_config_t *pin) {
	char fname[256];
	FILE *f;
	struct stat statbuff;

	sprintf(fname, "/sys/class/gpio/export");
	f = fopen(fname, "w");
	if (!f)
		FATAL("Can not open %s", fname);
	fprintf(f, "%d\n", pin->linear_no);
	fclose(f);

	// verify: now there should appear the control directory for the pin
	sprintf(fname, "/sys/class/gpio/gpio%d", pin->linear_no);
	if (stat(fname, &statbuff) != 0 || !S_ISDIR(statbuff.st_mode))
		FATAL("Gpio control dir %s not generated", fname);

}

/* export the NON-PRU pins: */
void gpios_c::init() {
	unsigned n;
	gpio_config_t *gpio;

	bank_map_registers(0, GPIO0_START_ADDR);
	bank_map_registers(1, GPIO1_START_ADDR);
	bank_map_registers(2, GPIO2_START_ADDR);
	bank_map_registers(3, GPIO3_START_ADDR);

	// fill pin database
	n = 0;
	pins[n++] = led[0] = config("LED0", DIR_OUTPUT, 1, 0);
	pins[n++] = led[1] = config("LED1", DIR_OUTPUT, 1, 1);
	pins[n++] = led[2] = config("LED2", DIR_OUTPUT, 1, 2);
	pins[n++] = led[3] = config("LED3", DIR_OUTPUT, 1, 3);
	pins[n++] = swtch[0] = config("SW0", DIR_INPUT, 1, 4);
	pins[n++] = swtch[1] = config("SW1", DIR_INPUT, 1, 5);
	pins[n++] = swtch[2] = config("SW2", DIR_INPUT, 1, 6);
	pins[n++] = swtch[3] = config("SW3", DIR_INPUT, 1, 7);
	pins[n++] = button = config("BUTTON", DIR_INPUT, 1, 12);
	pins[n++] = reg_enable = config("REG_ENABLE", DIR_OUTPUT, 1, 14);
	pins[n++] = bus_enable = config("BUS_ENABLE", DIR_OUTPUT, 1, 13);
	pins[n++] = i2c_panel_reset = config("PANEL_RESET", DIR_OUTPUT, 1, 28);

	// double functions on header: P9.41 set other pin function to tristate
	pins[n++] = collision_p9_41 = config(NULL, DIR_INPUT, 3, 20);
	pins[n++] = collision_p9_42 = config(NULL, DIR_INPUT, 3, 18);

	pins[n] = NULL; // end mark

	// program pins registers
	// echo no  > /sys/class/gpio/export_pin

	for (n = 0; (gpio = pins[n]); n++)
		export_pin(gpio);

	// set pin directions
	// a) echo in|out > /sys/class/gpio/gpio<no>/direction
	// b) bitmask in pin->gpio_bank_oe_addr
	for (n = 0; (gpio = pins[n]); n++) {
		unsigned reg = *(gpio->bank->oe_addr);
		// bit set in OE register: pin is input.
		reg &= ~gpio->pin_in_bank_mask; // clear bit
		if (gpio->direction == DIR_INPUT)
			reg |= gpio->pin_in_bank_mask;
		*(gpio->bank->oe_addr) = reg;
	}

	// init output register cache with values (after export_pin!)
	for (n = 0; n < 4; n++)
		// bus error, if no gpio registered from this bank
		if (banks[n].gpios_in_use > 0)
			banks[n].cur_dataout_val = *(banks[n].dataout_addr);

	// set the 4 LEDs to OFF
	// shared with LEDs
	ARM_DEBUG_PIN0(0)
	;
	ARM_DEBUG_PIN1(0)
	;
	ARM_DEBUG_PIN2(0)
	;
	ARM_DEBUG_PIN3(0)
	;

	set_leds(cmdline_leds) ;
}

// display a number on the 4 LEDs
void gpios_c::set_leds(unsigned number) {
	// inverted drivers
	GPIO_SETVAL(led[0], !(number & 1)) ;
	GPIO_SETVAL(led[1], !(number & 2)) ;
	GPIO_SETVAL(led[2], !(number & 4)) ;
	GPIO_SETVAL(led[3], !(number & 8)) ;
}


/*
 * Toggle in high speed, break with ^C
 */
void gpios_c::test_toggle(gpio_config_t *gpio) {
	INFO("Highspeed toggle pin %s, stop with ^C.", gpio->name);

	// Setup ^C catcher
	SIGINTcatchnext();
	// high speed loop
	while (!SIGINTreceived) {
		// run, baby, run!
		// the access macros:
		GPIO_SETVAL(gpio, 1)
		;
		GPIO_SETVAL(gpio, 0)
		;
		// fastest possible:
		// *(gpio->bank->setdataout_addr) = gpio->pin_in_bank_mask;
		//*(gpio->bank->clrdataout_addr) = gpio->pin_in_bank_mask;
	}
}

/* visible loop back
 * Switches control LEDs
 * Button controls QBUS/UNIBUS reg_enable
 */
void gpios_c::test_loopback(void) {
	timeout_c timeout;

	INFO("Manual loopback test, stop with ^C");
	INFO("Switch control LEDs, button controls \"" QUNIBONE_NAME " enable\".");

	// Setup ^C catcher
	SIGINTcatchnext();
	// high speed loop
	while (!SIGINTreceived) {
		GPIO_SETVAL(led[0], GPIO_GETVAL(swtch[0]));
		GPIO_SETVAL(led[1], GPIO_GETVAL(swtch[1]));
		GPIO_SETVAL(led[2], GPIO_GETVAL(swtch[2]));
		GPIO_SETVAL(led[3], GPIO_GETVAL(swtch[3]));
		GPIO_SETVAL(bus_enable, GPIO_GETVAL(button));
		timeout.wait_ms(10);
	}
}
