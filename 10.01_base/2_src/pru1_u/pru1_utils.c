/* pru1_utils.c:  misc. utilities

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

#define _PRU1_UTILS_C_

#include <stdint.h>
#include <stdbool.h>

#include "mailbox.h"
#include "iopageregister.h"
#include "pru1_buslatches.h"
#include "pru1_statemachine_arbitration.h"
#include "pru1_utils.h"


// detect signal change of INIT,DCLO,ACLO and sent event
// history initialized (among others) by powercycle
// Assume this events come so slow, no one gets raised until
// prev event processed.
void do_event_initializationsignals() {
	uint8_t bussignals_cur = buslatches_getbyte(7) & 0x38; // now sampled
	
	if (bussignals_cur & INITIALIZATIONSIGNAL_INIT) {
	sm_arb.device_request_mask = 0 ; // INIT clears all PRIORITY request signals
		// SACK cleared later on end of INTR/DMA transaction
	}

	    // Power event
    uint8_t powersignals_prev = mailbox.events.power_signals_cur; // as ARM knows
    if ((powersignals_prev ^ bussignals_cur) & (INITIALIZATIONSIGNAL_DCLO | INITIALIZATIONSIGNAL_ACLO)) {
        // ACLO or DCLO changed
        mailbox.events.power_signals_prev = powersignals_prev;
        mailbox.events.power_signals_cur = bussignals_cur & (INITIALIZATIONSIGNAL_DCLO | INITIALIZATIONSIGNAL_ACLO);
        EVENT_SIGNAL(mailbox,power) ;
        PRU2ARM_INTERRUPT;
    }

    uint8_t initsignal_prev = mailbox.events.init_signal_cur; // as ARM knows
    if ((initsignal_prev ^ bussignals_cur) & INITIALIZATIONSIGNAL_INIT) {
        // INIT changed
        if (!initsignal_prev) {
			// INIT raised: put reset values into every register
			// runs 7,6 usec, shorter than UNIBUS INIT, so no bus cycles are missed
			iopageregisters_reset_values() ; // all reset signals
		}
        mailbox.events.init_signal_cur = bussignals_cur & INITIALIZATIONSIGNAL_INIT;
        EVENT_SIGNAL(mailbox,init) ;
        PRU2ARM_INTERRUPT;
    }

	
/*		
	if (bussignals_cur != mb_cur) {
		// save old state, so ARM can detect what changed
		mailbox.events.init_signals_prev = mb_cur;
		mailbox.events.init_signals_cur = bussignals_cur;
		// trigger the correct event: power and/or INIT
		if ((mb_cur ^ bussignals_cur) & (INITIALIZATIONSIGNAL_DCLO | INITIALIZATIONSIGNAL_ACLO)) {
			// AC_LO or DC_LO changed
			EVENT_SIGNAL(mailbox,power) ;
		}
		if ((mb_cur ^ bussignals_cur) & INITIALIZATIONSIGNAL_INIT) {
			// INIT changed
			EVENT_SIGNAL(mailbox,init);
		}
		PRU2ARM_INTERRUPT
		;
	}
	*/
}


