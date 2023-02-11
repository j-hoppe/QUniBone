/* menu_panel.cpp: user sub menu "panel"

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

#include "application.hpp" // own
#include "pru.hpp"

#include "mailbox.h"

/**********************************************
 * Function and performance test of ARM-PRU1 mailbox
 * */
void application_c::menu_mailbox(const char *menu_code) 
{
	bool show_help = true; // show cmds on first screen, then only on error or request
	// mcout_t mcout; // Multi Column OUTput
	bool ready;
	char *s_choice;
	char s_id[256], s_opcode[256];
	ready = false;
	// test PRUs
	hardware_startup(pru_c::PRUCODE_TEST);
	while (!ready) {
		// no menu display when reading script
		if (show_help && !script_active()) {
			show_help = false; // only once
			printf("\n");
			printf("*** Test ARM-PRU1 mailbox.\n");

			//mcout_init(&mcout, MAX_GPIOCOUNT);
			//mcout_flush(&mcout, stdout, linewidth, "  ||  ", /*first_col_then_row*/0);

			printf("a      Send opcode + single value, verify result\n");
			printf("q      Quit\n");
		}
		s_choice = getchoice(menu_code);
		printf("\n");
		sscanf(s_choice, "%s %s", s_id, s_opcode);
		if (!strcasecmp(s_choice, "q")) {
			ready = true;
		} else if (!strcasecmp(s_choice, "a")) {
			mailbox_test1();
		} else {
			printf("Unknown command \"%s\"!\n", s_choice);
			show_help = true;
		}
	}
	hardware_shutdown();
}

