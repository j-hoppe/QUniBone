/* pru1_timeouts.c:  timeout conditions

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

 3-jul-2019	JH	begin edit

 Several timers are needed, but PRU hs only one
 global cycle counter CYCLEOUNT. It is 32 bit and runs at 200 MHz,
 so rollaround every 21 seconds.

 Usage:
 - if no timer is running, the first "timeout" reuqest clears CYCLEOUNT
 - for each timer, the "timeout" cycle count is set
 - tiemr msut be polled for timeout by user.
 - a timer is considered "timed-out", if its timeout is 0.
 - a global variable regsiteres the active running timeouts.
 - a running timeout MUST be canceled, or polled until "timeout_rechaed" !!


 The PRU CYCLECOUNT may not be reset if one timeout is active.
 So the total run time of all parallel running timeous must not exceed 21 seconds
 At least every 21 seconds all timers must be expired.

 */
#include <stdint.h>
#include <string.h>

#include "pru1_utils.h"
#include "pru1_timeouts.h"	// own

// count running timers.
static uint8_t timeouts_active = 0;

// cycle end count for each active timeoput.
uint32_t timeout_target_cycles[TIMEOUT_COUNT];

// all functions receive a pointer to one of these array members

#define TIMEOUT_INTERNAL_CYCLES	24

void timeout_set(uint32_t *target_cycles_var, uint32_t delta_cycles) {
	// stop timeout, if already running
	if (*target_cycles_var > 0) {
		*target_cycles_var = 0;
		timeouts_active--; // was inactive
	}

	if (timeouts_active == 0) {
		// first timeout: clear and restart counter
		PRU1_CTRL.CTRL_bit.CTR_EN = 0;
		PRU1_CTRL.CYCLE = 0;
	}

	/* 4 cycle used in TIMEOUT_REACHED */
	if (delta_cycles < TIMEOUT_INTERNAL_CYCLES)
		delta_cycles = 0;
	else
		delta_cycles -= TIMEOUT_INTERNAL_CYCLES;
	*target_cycles_var = PRU1_CTRL.CYCLE + delta_cycles;
	PRU1_CTRL.CTRL_bit.CTR_EN = 1;
	timeouts_active++; // now one more active
}

bool timeout_active(uint32_t *target_cycles_var) {
	return (*target_cycles_var > 0) ;
}


// must be called, if timeout not polled anymore for "timeout_reached()
void timeout_cleanup(uint32_t *target_cycles_var) {
	if (*target_cycles_var > 0) {
		*target_cycles_var = 0;
		timeouts_active--; // was inactive
	}
}

//

// test a timeout, wether it reached its arg count nor or earlier
bool timeout_reached(uint32_t *target_cycles_var) {
	bool result = false ;
	// fast path: assume timeout_reached() is called
	// because timeout is active
	if (PRU1_CTRL.CYCLE < *target_cycles_var)
		result = false;
	else if (*target_cycles_var == 0)
		result =true; // already "reached" if inactive
	else {
		// switched from "running" to "timeout reached"
		*target_cycles_var = 0;
		timeouts_active--;
		result = true;
	}
	return result ;
}

void timeout_init(void) {
	timeouts_active = 0;
	memset(timeout_target_cycles, 0, sizeof(uint32_t) * TIMEOUT_COUNT);
}
