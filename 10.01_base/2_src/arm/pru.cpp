/* pru.cpp: Management interface to PRU0 & PRU1.

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

 Management interface to PRU0 & 1:
 - setup interrupt
 - download code from arrays in pru0/1_config.c

 Partly copyright (c) 2014 dhenke@mythopoeic.org

 PRU progam code:
 For different QUniBone operation modes special program code is used for PRU0 and PRU1 each.
 One single omnipotent program code can not be used due to 2K code space limit.
 ARM code asu to reload appropriate PRU program code according to current function
 (PRU selftest, QBUS/UNIBUS slave, QBUS/UNIBUS master, logic analyzer, etc.)


 ***/
#define _PRU_CPP_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>

#include <fcntl.h>

#include "utils.hpp"
#include "logger.hpp"
#include "prussdrv.h"
#include "pruss_intc_mapping.h"
#include "mailbox.h"
#include "ddrmem.h"
#include "iopageregister.h"

#include "pru.hpp"

/*** PRU code arrays generated by clpru / hexpru  ***
 Program code is generated by "lcpru" and "hexpru --array" as C-array source code.

 These arrays are included here and wrapped for use by ARM/C++ classes.

 Format:
 const uint32_t target_image_0[] = {
 0x240000c0,
 ..
 0x20c30000};

 const uint8_t target_image_1[] = {
 0x01,
 ...
 0x00};
 */
//  under c++ linker error with const attribute ?!
#define const
#include "pru0_code_all_array.c"
#include "pru1_code_test_array.c"
#if defined(UNIBUS)
#include "pru1_code_unibus_array.c"
#elif defined(QBUS)
#include "pru1_code_qbus_array.c"
#endif
#undef const

// Singleton
pru_c *pru;

pru_c::pru_c() 
{
	prucode_id = PRUCODE_NONE;
	log_label = "PRU";
}

/*** pru_setup() -- initialize PRU and interrupt handler

 Initializes both PRUs and sets up PRU_EVTOUT_0 handler.

 The argument is a pointer to a null-terminated string containing the path
 to the file containing the PRU program binary.

 Returns 0 on success, non-0 on error.
 ***/

// entry for one program code variant for both PRUs
struct prucode_entry {
	unsigned id;
	// ptr to PRU0 code array generated by clpru
	uint32_t *pru0_code_array;
	unsigned pru0_code_array_sizeof;
	uint32_t pru0_entry;
	// smame for PRU1
	uint32_t *pru1_code_array;
	unsigned pru1_code_array_sizeof;
	uint32_t pru1_entry;
	// properties required for certain functions
};

// local static dictionary of program code variants
struct prucode_entry prucode[] = {
// self test functions
		{ pru_c::PRUCODE_TEST, //
				pru0_code_all_image_0, sizeof(pru0_code_all_image_0), PRU0_ENTRY_ADDR, //
				pru1_code_test_image_0, sizeof(pru1_code_test_image_0), PRU1_ENTRY_ADDR //
		},//
		  // full bus protocols for QBUS/UNIBUS device emulation
		{ pru_c::PRUCODE_EMULATION, //
				pru0_code_all_image_0, sizeof(pru0_code_all_image_0), PRU0_ENTRY_ADDR, //
#if defined(UNIBUS)				
		pru1_code_unibus_image_0, sizeof(pru1_code_unibus_image_0), PRU1_ENTRY_ADDR //
#elif defined(QBUS)				
		pru1_code_qbus_image_0, sizeof(pru1_code_qbus_image_0), PRU1_ENTRY_ADDR //
#endif		
	}, //
	   // end marker
	{ pru_c::PRUCODE_EOD, NULL, 0, 0, NULL, 0, 0 } };

int pru_c::start(enum prucode_enum _prucode_id) 
{
	timeout_c timeout;
	int rtn;
	tpruss_intc_initdata intc = PRUSS_INTC_INITDATA;

	// use stop() before restart()
	assert(this->prucode_id == PRUCODE_NONE);

	/* initialize PRU */
	if ((rtn = prussdrv_init()) != 0) {
		ERROR("prussdrv_init() failed");
		goto error;
	}

	/* open the interrupt */
	if ((rtn = prussdrv_open(PRU_EVTOUT_0)) != 0) {
		ERROR("prussdrv_open() failed");
		goto error;
	}

	/* initialize interrupt */
	if ((rtn = prussdrv_pruintc_init(&intc)) != 0) {
		ERROR("prussdrv_pruintc_init() failed");
		goto error;
	}

	/*
	 http://credentiality2.blogspot.com/2015/09/beaglebone-pru-ddr-memory-access.html
	 * get pointer to shared DDR
	 */
	// Pointer into the DDR RAM mapped by the uio_pruss kernel module.
	ddrmem->base_virtual = NULL;
	prussdrv_map_extmem((void **) &(ddrmem->base_virtual));
	ddrmem->len = prussdrv_extmem_size();
//	INFO("Shared DDR memory: %u bytes available.", ddrmem.len);

	ddrmem->base_physical = prussdrv_get_phys_addr((void *) (ddrmem->base_virtual));
	ddrmem->info(); // may abort program

	// get address of mail box struct in PRU
	mailbox_connect();
	// now all mailbox command fields initialized/cleared, PRUs can be started

	// get address of device register descriptor struct in PRU
	iopageregisters_connect();

	// search code in dictionary
	struct prucode_entry *pce;
	for (pce = prucode; pce->id != _prucode_id && pce->id != PRUCODE_EOD; pce++)
		;
	if (pce->id == PRUCODE_EOD)
		FATAL("PRU program code for config %u not found", pce->id);

	/* load code from arrays PRU*_code[] into PRU and start at 0
	 */
	if (pce->pru0_code_array_sizeof > PRUSS_MAX_IRAM_SIZE) {
		FATAL("PRU0 code too large. Closing program");
	}
	if ((rtn = prussdrv_exec_code_at(0, pce->pru0_code_array, pce->pru0_code_array_sizeof,
			pce->pru0_entry)) != 0) {
		FATAL("prussdrv_exec_program(PRU0) failed");
		goto error;
	}

	if (pce->pru1_code_array_sizeof > PRUSS_MAX_IRAM_SIZE) {
		FATAL("PRU1 code too large.");
	}
	if ((rtn = prussdrv_exec_code_at(1, pce->pru1_code_array, pce->pru1_code_array_sizeof,
			pce->pru1_entry)) != 0) {
		FATAL("prussdrv_exec_program(PRU1) failed");
	}
	INFO("Loaded and started PRU code with id = %d", _prucode_id);

	timeout.wait_ms(100); // wait for PRU to come up, much too long

	prucode_id = _prucode_id;

	// verify PRU1 is executing its command loop
	mailbox->arm2pru_req = ARM2PRU_NOP;
	timeout.wait_ms(1);
	if (mailbox->arm2pru_req != ARM2PRU_NONE) {
		FATAL("PRU1 is not executing its command loop");
		goto error;
	}

	return rtn;

	error: //
	pru->stop();
	FATAL("Could not connect to PRU.\n"
			"- Correct Device Tree Overlay loaded?\n"
			"- Check also /sys/class/uio/uio*.");
	return rtn; // not reached
}

/***  pru_c::stop() -- halt PRU and release driver

 Performs all necessary de-initialization tasks for the prussdrv library.

 Returns 0 on success, non-0 on error.
 ***/
int pru_c::stop(void) 
{
	int rtn = 0;
	prucode_id = PRUCODE_NONE;

	/* clear the event (if asserted) */
	if (prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT)) {
		ERROR("prussdrv_pru_clear_event() failed");
		rtn = -1;
	}

	/* halt and disable the PRU (if running) */
	if ((rtn = prussdrv_pru_disable(0)) != 0) {
		ERROR("prussdrv_pru_disable(0) failed");
		rtn = -1;
	}

	if ((rtn = prussdrv_pru_disable(1)) != 0) {
		ERROR("prussdrv_pru_disable(1) failed");
		rtn = -1;
	}

	/* release the PRU clocks and disable prussdrv module */
	if ((rtn = prussdrv_exit()) != 0) {
		ERROR("prussdrv_exit() failed");
		rtn = -1;
	}

	return rtn;
}

