/* menu_buslatches.cpp: user sub menu "buslatches"

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
#include <assert.h>

#include "utils.hpp"
#include "inputline.hpp"
#include "mcout.h"
#include "application.hpp" // own
#include "pru.hpp"

#include "gpios.hpp"
#include "buslatches.hpp"

/**********************************************
 * test a single or multiple 8bit (or less) register latches
 * connected to UNIBUS or QBUS
 * */

#if defined(UNIBUS)
static void buslatches_m9302_sack_test() {
#define GRANT_LINE_COUNT	5
	unsigned count;
	unsigned i;
	bool error;
	buslatches_wire_info_t *grant_line[GRANT_LINE_COUNT];
	buslatches_wire_info_t *sack_line;
	printf("Test SACK turnaround of M9302 terminator.\n");

	printf("GRANT lines BG4,BG5,BG6,BG7,NPG are stimulated randomly,\n");
	printf("if at least one is active, SACK must be asserted by M9302 terminator.\n");
	printf("Starting now, stop with ^C ...\n");
	assert(grant_line[0] = buslatches_wire_info_get("BG4_OUT",/*is_input*/false));
	assert(grant_line[1] = buslatches_wire_info_get("BG5_OUT",/*is_input*/false));
	assert(grant_line[2] = buslatches_wire_info_get("BG6_OUT",/*is_input*/false));
	assert(grant_line[3] = buslatches_wire_info_get("BG7_OUT",/*is_input*/false));
	assert(grant_line[4] = buslatches_wire_info_get("NPG_OUT",/*is_input*/false));
	assert(sack_line = buslatches_wire_info_get("SACK",/*is_input*/true));
	// BG* have reversed polarity
	assert(grant_line[0]->properties == 1);
	assert(grant_line[1]->properties == 1);
	assert(grant_line[2]->properties == 1);
	assert(grant_line[3]->properties == 1);
	assert(grant_line[4]->properties == 1);

	SIGINTcatchnext();

	// INIT: all inactive
	for (i = 0; i < GRANT_LINE_COUNT; i++)
	buslatches.set_pin_val(grant_line[i], !0);
	// high speed loop
	count = 0;
	i = 0;
	error = false;
	while (!error && !SIGINTreceived) {

		// moving one, with lots of extra "all IDLE" phase
		// SACK LED must be on at 50%.
		i = count % (2 * GRANT_LINE_COUNT);
		// set one line
		if (i < GRANT_LINE_COUNT) {
			buslatches.set_pin_val(grant_line[i], !1);
			// SACK must be asserted
			if (!buslatches.get_pin_val(sack_line)) {
				printf("ERROR: GRANT line %s active, but SACK negated!\n",
						grant_line[i]->qunibus_name);
				printf("Check:\n");
				printf("- \"SACK turn around\" enabled on terminator?\n");
				error = true;
			}
			// clear signal
			buslatches.set_pin_val(grant_line[i], !0);
		} else {
			// all inactive: SACK must be negated
			if (buslatches.get_pin_val(sack_line)) {
				printf("ERROR: All 5 GRANT lines inactive, but SACK negated!\n");
				printf("Check:\n");
				printf("- GRANT chain between UniProbe and M9302 terminator closed?\n");
				error = true;
			}
		}
		count++;
	}
	if (error)
	printf("Test aborted after %d operations.\n", count);
	else
	printf("Test stopped by user after %d operations.\n", count);
	printf("\n");
}
#endif

void application_c::menu_buslatches(const char *menu_code) {
	bool show_help = true ; // show cmds on first screen, then only on error or request
	bool show_inputs = true; // query and show state of all latches
	bool stop_on_error = true ;
	bool ready;
	char *s_choice;
	char s_opcode[256], s_param[256];
	int n_fields;

	// Bypass central adddress-width-test
	if (!qunibus->addr_width)
		qunibus->set_addr_width(22) ; 

	// These test need active bus drivers
	hardware_startup(pru_c::PRUCODE_TEST);
	buslatches.output_enable(true);

#define PRINTBUSLATCH(bl)	do { 											\
		if ((bl)->rw_bitmask != 0xff)								\
		printf("buslatch[%d] = 0x%02x (bits = 0x%02x, R/W bits = 0x%02x)\n", (bl)->addr,	\
				(bl)->getval() & (bl)->bitmask, 				\
				(bl)->bitmask, (bl)->rw_bitmask) ;			\
		else 																\
				printf("buslatch[%d] = 0x%02x (bits = 0x%02x)\n", (bl)->addr,				\
						(bl)->getval() & (bl)->bitmask, 		\
						(bl)->bitmask) ;							\
				} while(0)
	ready = false;
		
	while (!ready) {
		if (show_inputs) {
			unsigned i;
			for (i = 0; i < 8; i++) {
				printf("%d) ", i);
				PRINTBUSLATCH(buslatches[i]);
			}
		}
		
		// no menu display when reading script
		if (show_help && ! script_active()) {
			show_help = false; // only once
			printf("\n");
			printf("*** Test 8-bit register bus-latches and corresponding " QUNIBUS_NAME " lines.\n");
			printf("*** Run only on empty " QUNIBUS_NAME "!\n");
			printf("<id>        Read inputs connected to latch\n");
			printf("<id> <val>  Set latch outputs to hex value.\n");
#if defined(QBUS)
			printf("            ADDR in register 3,4,5 is latched from DAL reg 0,1,2. Write has side effects on 0,1,2 incl. SYNC.\n");
#endif

			printf(
					"              Value appears on PRU inputs after signal round trip delay.\n");
			printf("<id> u      Count latch value upward\n");
			printf("<id> o      Rotate a \"moving one\"\n");
			printf("<id> z      Rotate a \"moving zero\"\n");
			printf("<id> t      Toggle 0x00,0xff\n");
			printf("<id> r      Random values\n");
			printf("* o|z|t|r   As above, test on all R/W registers, without ADDR mux test.\n");
			printf("* 0|1       All OFF, all ON\n");
			printf("soe <0|1>   disable/enable \"stop on error\" for continous self tests (is %s).\n",
						stop_on_error ? "ENABLED":"NOT ENABLED");
#if defined(UNIBUS)
			printf("gst         M9302 GRANT/SACK turnaround test\n");
#endif
			printf("o <0|1>     disable/enable DS8641 " QUNIBUS_NAME " output drivers.\n");
			printf("              Drivers are currently %s.\n",
					buslatches.cur_output_enable ? "ENABLED" : "NOT ENABLED");
			printf("a           Show all\n");
			printf("r           Reset outputs to \"neutral\" values\n");
			printf(
					"t           High speed timing test by PRU. PRU1.12 is error signal. Stop with ^C\n");
			printf("q           Quit\n");
		}
		s_choice = getchoice(menu_code);
		printf("\n");
		n_fields = sscanf(s_choice, "%s %s", s_opcode, s_param);
		if (strlen(s_choice) == 0) {
			// should not happen, but occurs under Eclipse?
		} else if (!strcasecmp(s_choice, "q")) {
			ready = true;
		} else if (!strcasecmp(s_choice, "r")) {
			buslatches.pru_reset();
		} else if (!strcasecmp(s_choice, "a")) {
			show_inputs = true;
		} else if (n_fields == 2 && !strcasecmp(s_opcode, "o")) {
			// parse o 0|1
			unsigned param = strtol(s_param, NULL, 10);
			buslatches.output_enable(param);
		printf(QUNIBUS_NAME " drivers now %s.\n",
				buslatches.cur_output_enable ? "enabled" : "disabled");
	} else if (n_fields == 1 && strchr("01234567", s_opcode[0])) {
		unsigned reg_sel = strtol(s_opcode, NULL, 16);
		buslatch_c *bl = buslatches[reg_sel];
		PRINTBUSLATCH(bl);
	} else if (n_fields == 2 && strchr("01234567", s_opcode[0])) {
		// parse <id> <opcode> |<val>
		unsigned reg_sel = strtol(s_opcode, NULL, 16);
		if (reg_sel > 7) {
			printf("Error: reg_sel %d not in [0..7]\n", reg_sel);
			show_help = true;
			continue;
		}
		buslatch_c *bl = buslatches[reg_sel];
		if (strchr("0123456789abcdefABCDEF", s_param[0])) {
			unsigned val = strtol(s_param, NULL, 16);
			bl->setval(0xff, val);
			PRINTBUSLATCH(bl);
		} else if (!strcasecmp(s_param, "u")) {
			buslatches.test_simple_pattern(1, bl);
		} else if (!strcasecmp(s_param, "o")) {
			buslatches.test_simple_pattern(2, bl);
		} else if (!strcasecmp(s_param, "z")) {
			buslatches.test_simple_pattern(3, bl);
		} else if (!strcasecmp(s_param, "t")) {
			buslatches.test_simple_pattern(4, bl);
		} else if (!strcasecmp(s_param, "r")) {
			buslatches.test_simple_pattern(5, bl);
		} else {
			printf("Syntax error: <id> <pattern> |<val>.\n");
			show_help = true;
		}
	} else if (n_fields == 2 && strchr("*", s_opcode[0])) {
		if (!strcasecmp(s_param, "o")) {
			buslatches.test_simple_pattern_multi(2, stop_on_error);
		} else if (!strcasecmp(s_param, "z")) {
			buslatches.test_simple_pattern_multi(3, stop_on_error);
		} else if (!strcasecmp(s_param, "t")) {
			buslatches.test_simple_pattern_multi(4, stop_on_error);
		} else if (!strcasecmp(s_param, "r")) {
			buslatches.test_simple_pattern_multi(5, stop_on_error);
		} else if (!strcasecmp(s_param, "0")) {
			for (unsigned reg_sel = 0; reg_sel < 8; reg_sel++)
				buslatches[reg_sel]->setval(0xff, 0);
			show_inputs = true;
		} else if (!strcasecmp(s_param, "1")) {
			for (unsigned reg_sel = 0; reg_sel < 8; reg_sel++)
				buslatches[reg_sel]->setval(0xff, 0xff);
			show_inputs = true;
		} else {
			printf("Syntax error: *  <pattern>.\n");
			show_help = true;
		}
	} else if (n_fields == 2 && !strcasecmp(s_opcode, "soe")) {
		stop_on_error = strtol(s_param, NULL, 10);
#if defined(UNIBUS)			
	} else if (!strcasecmp(s_opcode, "gst")) {
		buslatches_m9302_sack_test();
#endif			
	} else if (n_fields == 1 && !strcasecmp(s_opcode, "t")) {
		buslatches.test_timing(0x55, 0xaa, 0x00, 0xff);
	} else {
		printf("Unknown command \"%s\"!\n", s_choice);
		show_help = true;
	} // if (s_choice...)
} // while (!ready)

buslatches.output_enable(false);
hardware_shutdown();
}

