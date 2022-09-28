/* menu_gpio.cpp: user "GPIO" sub menu

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

 16-Nov-2018  JH      created
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "inputline.hpp"
#include "mcout.h"
#include "application.hpp" // own

#include "gpios.hpp"

/**********************************************
 * select a single GPIO pin for set/clear
 * and high speed toggle
 * no PRU activity
 * */
void application_c::menu_gpio(const char *menu_code) {
	bool show_help ; // show cmds on first screen, then only on error or request
	mcout_t mcout; // Multi Column OUTput
	unsigned name_len;
	bool ready;
	int i;
	gpio_config_t *gpio;
	char *s_choice;
	char s_id[256], s_opcode[256];
	int n_fields;
	
	ready = false;
	while (!ready) {
		// get max len of pin names
		name_len = 0;
		for (i = 0; (gpio = gpios->pins[i]); i++)
			if (!gpio->internal && name_len < strlen(gpio->name))
				name_len = strlen(gpio->name);

		mcout_init(&mcout, MAX_GPIOCOUNT);

		for (i = 0; (gpio = gpios->pins[i]); i++) {
			gpio->tag = 0xffffff; // invalid
			if (!gpio->internal) {
				mcout_printf(&mcout, "%2d) %-*s = %d", i + 1, name_len, gpio->name,
						GPIO_GETVAL(gpio));
				gpio->tag = i + 1; // remember label in menu
			}
		}
		mcout_flush(&mcout, stdout, opt_linewidth, "  ||  ", /*first_col_then_row*/0);
		// no menu display when reading script		
		if (show_help && !script_active()) {
			printf("\n");
			printf("*** Test single GPIO pins.\n");
			show_help = false; // only once
			printf("<id> 0  Set GPIO to Low\n");
			printf("<id> 1  Set GPIO to High\n");
			printf("<id> f  Toggle GPIO pin in high frequency (> 2 MHz)\n");
			printf("lb      Manual loopback test\n");
			printf("a       Show all\n");
			printf("q       Quit\n");
		}
		s_choice = getchoice(menu_code);
		printf("\n");
		n_fields = sscanf(s_choice, "%s %s", s_id, s_opcode);
		if (strlen(s_choice) == 0) {
			// should not happen, but occurs under Eclipse?
		} else if (!strcasecmp(s_choice, "q")) {
			ready = true;
		} else if (!strcasecmp(s_choice, "lb")) {
			gpios->test_loopback();
		} else if (n_fields != 2) {
			printf("Error: not \"id opcode\"\n");
		} else {
			// parse <id> <opcode>
			// search pin with selected tag number
			unsigned pin_tag = strtol(s_id, NULL, 10);
			gpio = NULL;
			for (i = 0; !gpio && gpios->pins[i]; i++)
				if (gpios->pins[i]->tag == pin_tag)
					gpio = gpios->pins[i]; // found!
			if (!gpio) {
				printf("Error: GPIO #%d not found", pin_tag);
				continue;
			}
			if (!strcasecmp(s_opcode, "0"))
				GPIO_SETVAL(gpio, 0);
			else if (!strcasecmp(s_opcode, "1"))
				GPIO_SETVAL(gpio, 1);
//			else if (!strcasecmp(s_opcode, "f"))
//				test_gpio_toggle(gpio);
			else {
				printf("Unknown command \"%s\"!\n", s_choice);
				show_help = true;
			}
		}
	}
}

