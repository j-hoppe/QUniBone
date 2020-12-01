/* menu_device_exercisers.cpp: user sub menu

 Copyright (c) 2019, Joerg Hoppe
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

 19-mar-2019  JH      created
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "logger.hpp"
#include "inputline.hpp"
#include "mcout.h"
#include "stringgrid.hpp"

#include "application.hpp" // own

//#include "gpios.hpp"
#include "buslatches.hpp"
#include "mailbox.h"
#include "iopageregister.h"
#include "parameter.hpp"
#include "qunibus.h"
#include "memoryimage.hpp"
#include "qunibusadapter.hpp"

//#include "qunibusadapter.hpp"
//#include "qunibusdevice.hpp"

#include "devexer_rl.hpp"

void application_c::menu_device_exercisers(const char *menu_code) {
	bool ready = false;
	bool show_help = true;
	bool memory_installed = false;
	devexer::devexer_c *cur_exerciser = NULL;
	unsigned n_fields;
	char *s_choice;
	char s_opcode[256], s_param[2][256];

//	iopageregisters_init();
	// QBUS/UNIBUS activity
	hardware_startup(pru_c::PRUCODE_EMULATION);
	buslatches.output_enable(true);
	qunibus->set_arbitrator_active(false) ;

	// instantiate different device exercisers

	devexer::rl_c rl;
	USE(rl); // not accessed diretcly, but is registered to exerciser list
	// and used by name.

	cur_exerciser = NULL;

	while (!ready) {

		if (show_help) {
			show_help = false; // only once
			printf("\n");
			printf("*** Exercise (= work with) installed " QUNIBUS_NAME " decives.\n");
			print_arbitration_info("    ");
			if (cur_exerciser) {
				printf("    Current device is \"%s\" @ %s\n",
						cur_exerciser->name.value.c_str(), qunibus->addr2text(cur_exerciser->base_addr.value));
			} else
				printf("    No current device selected\n");
			if (memory_installed) {
				printf("    " QUNIBUS_NAME " memory emulated from %s to %s.\n",
						qunibus->addr2text(emulated_memory_start_addr), qunibus->addr2text(emulated_memory_end_addr));
			} else
				printf("    NO " QUNIBUS_NAME " memory installed ... device test limited!\n");
			printf("\n");
			printf("m i              Install (emulate) max " QUNIBUS_NAME " memory\n");
			if (memory_installed) {
				printf("m f [word]       Fill " QUNIBUS_NAME " memory (with 0 or other octal value)\n");
				printf("m d              Dump " QUNIBUS_NAME " memory to disk\n");
			}
			printf("le                   List all defined device exercisers\n");
			printf("se <exer>            Select \"current device exerciser\"\n");
			if (cur_exerciser) {
				printf("p <param> <val>      Set parameter value of current device\n");
				printf("p <param>            Get parameter value of current device\n");
				printf("p panel              Force parameter update from panel\n");
				printf("p                    Show all parameter of current device\n");
			}

			printf("d <regname> <val>    Deposit octal value into named device register\n");
			printf("e <regname>          Examine single device register (regno decimal)\n");
			printf("e                    Examine all device registers\n");
			printf("d <addr> <val>       Deposit octal val into " QUNIBUS_NAME " address.\n");
			printf("e <addr>             Deposit octal val into " QUNIBUS_NAME " address.\n");
			printf("dbg c|s|f            Debug log: Clear, Show on console, dump to File.\n");
			printf("                       (file = %s)\n", logger->default_filepath.c_str());
			printf("init                 Pulse " QUNIBUS_NAME " INIT\n");
#if defined(UNIBUS)			
			printf("pwr                  Simulate UNIBUS power cycle (ACLO/DCLO)\n");
#elif defined(QBUS)
			printf("pwr                  Simulate QBUS power cycle (POK/DCOK)\n");
#endif
			printf("q                    Quit\n");
		}
		s_choice = getchoice(menu_code);

		printf("\n");
		try {
			n_fields = sscanf(s_choice, "%s %s %s", s_opcode, s_param[0], s_param[1]);
			if (!strcasecmp(s_opcode, "q")) {
				ready = true;
			} else if (!strcasecmp(s_opcode, "init")) {
				qunibus->init();
			} else if (!strcasecmp(s_opcode, "pwr")) {
				qunibus->powercycle();
			} else if (!strcasecmp(s_opcode, "dbg") && n_fields == 2) {
				if (!strcasecmp(s_param[0], "c")) {
					logger->clear();
					printf("Debug log cleared.\n");
				} else if (!strcasecmp(s_param[0], "s"))
					logger->dump();
				else if (!strcasecmp(s_param[0], "f"))
					logger->dump(logger->default_filepath);
			} else if (!strcasecmp(s_opcode, "m") && n_fields == 2
					&& !strcasecmp(s_param[0], "i")) {
				// install (emulate) max QBUS/UNIBUS memory
				memory_installed = emulate_memory();
				show_help = true; // menu struct changed
			} else if (memory_installed && !strcasecmp(s_opcode, "m") && n_fields >= 2
					&& !strcasecmp(s_param[0], "f")) {
				//  clear QBUS/UNIBUS memory
				bool timeout;
				uint16_t fillword = 0;
				if (n_fields == 3)
					qunibus->parse_word(s_param[1], &fillword);
				membuffer->set_addr_range(emulated_memory_start_addr, emulated_memory_end_addr);
				membuffer->fill(fillword);
				// write buffer-> QBUS/UNIBUS
				printf("Fill memory with %06o, writing " QUNIBUS_NAME " memory[%s:%s]\n", fillword,
						qunibus->addr2text(emulated_memory_start_addr), qunibus->addr2text(emulated_memory_end_addr));
				qunibus->mem_write(membuffer->data.words,
						emulated_memory_start_addr, emulated_memory_end_addr, &timeout);
				if (timeout)
					printf("Error writing " QUNIBUS_NAME " memory!\n");
			} else if (memory_installed && !strcasecmp(s_opcode, "m") && n_fields == 2
					&& !strcasecmp(s_param[0], "d")) {
				// dump QBUS/UNIBUS memory to disk
				const char * filename = "memory.dump";
				bool timeout;
				// 1. read QBUS/UNIBUS memory
				uint32_t end_addr = qunibus->test_sizer() - 2;
				printf("Reading " QUNIBUS_NAME " memory[0:%s] with DMA\n", qunibus->addr2text(end_addr));
				//  clear memory buffer, to be sure content changed
				membuffer->set_addr_range(0, end_addr);
				membuffer->fill(0);

				qunibus->mem_read(membuffer->data.words, 0, end_addr,
						&timeout);
				if (timeout)
					printf("Error reading " QUNIBUS_NAME " memory!\n");
				else {
					// 1. read QBUS/UNIBUS memory
					printf("Saving to file \"%s\"\n", filename);
					membuffer->save_binary(filename, end_addr + 2);
				}
			} else if (!strcasecmp(s_opcode, "le") && n_fields == 1) {
				list<devexer::devexer_c *>::iterator it;
				cout << "Registered exercisers:\n";
				for (it = devexer::devexer_c::myexercisers.begin();
						it != devexer::devexer_c::myexercisers.end(); ++it)
					cout << "- " << (*it)->name.value << "\n";
			} else if (!strcasecmp(s_opcode, "se") && n_fields == 2) {
				list<devexer::devexer_c *>::iterator it;
				bool found = false;
				for (it = devexer::devexer_c::myexercisers.begin();
						it != devexer::devexer_c::myexercisers.end(); ++it)
					if (!strcasecmp((*it)->name.value.c_str(), s_param[0])) {
						found = true;
						cur_exerciser = *it;
					}
				if (!found)
					cout << "Exerciser \"" << s_param[0] << "\" not found.\n";
				else {
					printf("Current exerciser is \"%s\" @ %s\n",
							cur_exerciser->name.value.c_str(), qunibus->addr2text(cur_exerciser->base_addr.value));
					// TODO: find base address of assoiated QBUS/UNIBUS unibuscontroller
					show_help = true;
				}
			} else if (cur_exerciser && !strcasecmp(s_opcode, "p") && n_fields == 1) {
				cout << "Parameters of device " << cur_exerciser->name.value << ":\n";
				print_params(cur_exerciser, NULL);
			} else if (cur_exerciser && !strcasecmp(s_opcode, "p") && n_fields == 2) {
				// show selected
				string pn(s_param[0]);
				parameter_c *p = cur_exerciser->param_by_name(pn);
				if (p == NULL)
					cout << "Exerciser \"" << cur_exerciser->name.value
							<< "\" has no parameter \"" << pn << "\".\n";
				else
					print_params(cur_exerciser, p);
			} else if (cur_exerciser && !strcasecmp(s_opcode, "p") && n_fields == 3) {
				string pn(s_param[0]);
				parameter_c *p = cur_exerciser->param_by_name(pn);
				if (p == NULL)
					cout << "Exerciser \"" << cur_exerciser->name.value
							<< "\" has no parameter \"" << pn << "\".\n";
				else {
					string sval(s_param[1]);
					p->parse(sval);
					print_params(cur_exerciser, p);
				}
			} else if (!strcasecmp(s_opcode, "d") && n_fields == 3) {
				uint32_t addr;
				uint16_t wordbuffer;
				// interpret as 18 bit address
				qunibus->parse_addr(s_param[0], &addr);
				qunibus->parse_word(s_param[1], &wordbuffer);
				bool timeout = !qunibus->dma(true, QUNIBUS_CYCLE_DATO, addr,
						&wordbuffer, 1);
				printf("DEPOSIT %s <- %06o\n", qunibus->addr2text(addr), wordbuffer);
				if (timeout)
					printf("Bus timeout at %s.\n", qunibus->addr2text(qunibus->dma_request->unibus_end_addr));
			} else if (!strcasecmp(s_opcode, "e") && n_fields <= 2) {
				uint16_t wordbuffer;
				bool timeout=false;
				uint32_t addr;
				if (n_fields == 2) { // single reg number given
					qunibus->parse_addr(s_param[0], &addr); 	// interpret as 18 bit address
					timeout = !qunibus->dma(true, QUNIBUS_CYCLE_DATI, addr,
							&wordbuffer, 1);
					printf("EXAM %s -> %06o\n", qunibus->addr2text(addr), wordbuffer);
				}
				if (timeout)
					printf("Bus timeout at %s.\n", qunibus->addr2text(addr));
				// cur_addr now on last address in block

			} else {
				printf("Unknown command \"%s\"!\n", s_choice);
				show_help = true;
			}
		} catch (bad_parameter& e) {
			cout << "Error : " << e.what() << "\n";
		}
	} // ready

	buslatches.output_enable(false);
	hardware_shutdown();
}

