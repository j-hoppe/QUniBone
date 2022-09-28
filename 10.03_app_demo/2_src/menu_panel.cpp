/* menu_panel.cpp: user sub menu

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

 24-Jul-2022  JH      worker running all the time
 16-Nov-2018  JH      created
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "mcout.h"
#include "application.hpp" // own

#include "panel.hpp"

/**********************************************
 * Test input and outputs on I2C connected MC23017s.
 * no active PRU needed
 * */
void application_c::menu_panel(const char *menu_code) {
	mcout_t mcout; // Multi Column OUTput
	bool show_help = true ; // show cmds on first screen, then only on error or request
	bool ready = false;
	char *s_choice;
	unsigned n_fields;
	char s_opcode[256], s_param[3][256];

	paneldriver->reset(); // reset I2C, restart worker()
  	printf("Start worker().\n");

	while (!ready) {
		// display all known controls
		unsigned name_len = 0;
		unsigned controlno;
		// get max name len of all panel controls
		for (vector<panelcontrol_c *>::iterator it = paneldriver->controls.begin();
				it != paneldriver->controls.end(); ++it)
			if ((*it)->full_name().length() > name_len)
				name_len = (*it)->full_name().length();
		// display all known controls
		mcout_init(&mcout, paneldriver->controls.size());

		// put all panel controls into array view
		for (controlno = 0; controlno < paneldriver->controls.size(); controlno++) {
			panelcontrol_c *pc = paneldriver->controls[controlno];
			mcout_printf(&mcout, "%2d) %-*s = %d", controlno, name_len, pc->full_name().c_str(),
					pc->value);
		}
		mcout_flush(&mcout, stdout, opt_linewidth, "  ||  ", /*first_col_then_row*/0);

		// no menu display when reading script
		if (show_help && !script_active()) {
			show_help = false; // only once
			// show all registers

			// 		 mcout_init(&mcout, MAX_GPIOCOUNT);
			// 		 mcout_flush(&mcout, stdout, linewidth, "  ||  ", /*first_col_then_row*/ 0);

			printf("\n");
			printf("*** Test I2C paneldriver.\n");
			printf("  All values hex\n");
			printf("ir <slave> <reg>        Read single I2C2 slave device byte register\n");
			printf("                          <slave> is I2C2 bus address of gpio chip\n");
			printf(
					"                          MC23017s are 0x20..0x27. Try \"i2cdump -y 2 0x20\".\n");
			printf("                          <reg> is register of GPIO chip.\n");
			printf(
					"                          MC23017 GPIOA is 0x12 (RL02 lamps), GPIOB is 0x13 (RL02 switches)\n");
			printf("iw <slave> <reg> <val>  Write single I2C slave device byte register\n");
			printf("<id> <val>              Write panel control <id>\n");
			/*
			 printf("<id>		  read single MC23017 register\n");
			 printf("<id> <byte> write single MC23017 register\n");
			 printf("t 		  short lamp test\n");
			 printf("l 		  test loop: lamp on if switch closed. Stop with ^C.\n");
			 printf("\n");
			 printf("<id> is <addr>a|b. Example:  \"1a\" is register a of MC23017 at 1\n");
			 */
			printf("tmo                     Test: moving ones through all lamps\n");
			printf("tlb                     Test: manual loop back of buttons to lamps\n");
			printf("rst                     Re-initialize paneldriver\n");
			printf("q                       Quit\n");
		}

		s_choice = getchoice(menu_code);

		printf("\n");

		n_fields = sscanf(s_choice, "%s %s %s %s", s_opcode, s_param[0], s_param[1],
				s_param[2]);
		if (!strcasecmp(s_opcode, "q")) {
			ready = true;
		} else if (!strcasecmp(s_opcode, "rst")) {
			paneldriver->reset();
		} else if (!strcasecmp(s_opcode, "tmo")) {
			paneldriver->test_moving_ones();
		} else if (!strcasecmp(s_opcode, "tlb")) {
			paneldriver->test_manual_loopback();
		} else if (!strcasecmp(s_opcode, "ir") && n_fields == 3) {
			uint8_t slave_addr = strtol(s_param[0], NULL, 16) & 0xff;
			uint8_t reg_addr = strtol(s_param[1], NULL, 16) & 0xff;
			uint8_t val;
			if (paneldriver->i2c_read_byte(slave_addr, reg_addr, &val))
				printf("I2C read slave 0x%x, reg[%x] => %02x\n", (unsigned) slave_addr,
						(unsigned) reg_addr, (unsigned) val);
			else
				printf("I2C read slave 0x%x, reg[%x] => ERROR\n", (unsigned) slave_addr,
						(unsigned) reg_addr);
		} else if (!strcasecmp(s_opcode, "iw") && n_fields == 4) {
			uint8_t slave_addr = strtol(s_param[0], NULL, 16) & 0xff;
			uint8_t reg_addr = strtol(s_param[1], NULL, 16) & 0xff;
			uint8_t val0 = strtol(s_param[2], NULL, 16) & 0xff;
			if (paneldriver->i2c_write_byte(slave_addr, reg_addr, val0)) {
				uint8_t val1;
				if (paneldriver->i2c_read_byte(slave_addr, reg_addr, &val1))
					printf("I2C write slave 0x%x, reg[0x%x]. wrote 0x%02x, is now 0x%02x.\n",
							(unsigned) slave_addr, (unsigned) reg_addr, (unsigned) val0,
							(unsigned) val1);
				else
					printf("I2C write read back slave 0x%x, reg[%x] => ERROR\n",
							(unsigned) slave_addr, (unsigned) reg_addr);
			} else
				printf("I2C write slave 0x%x, reg[%x] => ERROR\n", (unsigned) slave_addr,
						(unsigned) reg_addr);
		} else if (n_fields == 2 && strchr("0123456789", s_opcode[0])) {
			// parse <id> <opcode> |<val>
			controlno = strtol(s_opcode, NULL, 10);
			if (controlno >= paneldriver->controls.size()) {
				printf("Error: controlno %d not in [0..%d]\n", controlno,
						paneldriver->controls.size());
				show_help = true;
				continue;
			}
			if (strchr("0123456789abcdefABCDEF", s_param[0][0])) {
				panelcontrol_c *pc = paneldriver->controls[controlno];
				pc->value = strtol(s_param[0], NULL, 16);
			} else {
				printf("Error: %s not a hex value\n", s_param[0]);
				show_help = true;
			}
		} else {
			printf("Unknown command \"%s\"!\n", s_choice);
			show_help = true;
		}
	}
    paneldriver->enabled.set(false); // worker_stop();
	printf("Worker stopped.\n");

}

