/* menu_menu_ddrmem_slave_only.cpp: user sub menu

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

//#include "gpios.hpp"
#include "buslatches.hpp"
#include "pru.hpp"
#include "ddrmem.h"

/**********************************************
 * DDRMEM
 * function to read, write, test shared DDR memory
 * */
void application_c::menu_ddrmem_slave_only(const char *menu_code) 
{
	bool show_help = true ; // show cmds on first screen, then only on error or request
	char *s_choice;
	char s_opcode[256], s_param[2][256];
	bool ready;
	int n_fields;

	hardware_startup(pru_c::PRUCODE_EMULATION);
	qunibus->set_arbitrator_active(false) ;

	ready = false;
	
	while (!ready) {
		// sync pagetable
		ddrmem->set_range(emulated_memory_start_addr, emulated_memory_end_addr);
		// no menu display when reading script
		if (show_help && ! script_active()) {
			show_help = false; // only once
			printf("\n");
			printf("*** Access Shared DDR memory = "  QUNIBUS_NAME " memory as BUS SLAVE.\n");
			printf("\n");

			printf("l <filename>     Load memory content from disk file\n");
			printf("s <filename>     Save memory content to disk file\n");
			printf("c                Clear memory to 0\n");
			printf("f a              Fill memory with test pattern, with local ARM code\n");
			printf(
					"f p              Fill memory with test pattern, by mailbox command to PRU\n");
			printf("u <start> <end>  Start acting as " QUNIBUS_NAME " slave memory\n");
			printf(
					"                 Responds to master cycles in octal address range <start..end>\n");
			printf("i                Info\n");
			printf("q                Quit\n");
		}
		s_choice = getchoice(menu_code);

		printf("\n");
		n_fields = sscanf(s_choice, "%s  %s %s", s_opcode, s_param[0], s_param[1]);
		if (!strcasecmp(s_opcode, "q")) {
			ready = true;
		} else if (!strcasecmp(s_opcode, "l") && (n_fields == 2)) {
			ddrmem->load(s_param[0]);
		} else if (!strcasecmp(s_opcode, "s") && (n_fields == 2)) {
			ddrmem->save(s_param[0]);

		} else if (!strcasecmp(s_opcode, "c")) {
			ddrmem->clear();

		} else if (!strcasecmp(s_opcode, "f") && (n_fields == 2)) {
			if (!strcasecmp(s_param[0], "a"))
				ddrmem->fill_pattern();
			else if (!strcasecmp(s_param[0], "p"))
				ddrmem->fill_pattern_pru();
			else
				printf("Use \"f a\" or \"f p\"\n");
		} else if (!strcasecmp(s_opcode, "u") && (n_fields == 3)) {
			uint32_t start_addr, end_addr;
			qunibus->parse_addr(s_param[0], &start_addr);
			qunibus->parse_addr(s_param[1], &end_addr);
			if (ddrmem->set_range(start_addr, end_addr)) {
				emulated_memory_start_addr = start_addr;
				emulated_memory_end_addr = end_addr;
				printf("Implement an " QUNIBUS_NAME " memory card with DDR memory:\n");
				printf("  Monitoring " QUNIBUS_NAME " master for accesses into memory,\n");
				printf("  responding to addresses in range %s..%s.\n",
					qunibus->addr2text(emulated_memory_start_addr), qunibus->addr2text(emulated_memory_end_addr));
				printf("  To test, start XXDP2.5 and run ZKMA?? with SW12 set.\n");
				// These tests need active bus drivers
				buslatches.output_enable(true);

				ddrmem->unibus_slave(emulated_memory_start_addr, emulated_memory_end_addr);
				// terminates on user action
				buslatches.output_enable(false);
			}
		} else if (!strcasecmp(s_opcode, "i")) {
			ddrmem->info();
		} else {
			printf("Unknown command \"%s\"!\n", s_choice);
			show_help = true;
		}
	}
	hardware_shutdown();

}

