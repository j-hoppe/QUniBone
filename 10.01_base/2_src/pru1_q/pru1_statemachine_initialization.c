/* pru1_statemachine_initialization.c: state machine for POK/DOK/INIT

 Copyright (c) 2020, Joerg Hoppe
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


 12-nov-2020  JH      begin


 Processes PRU->ARm signals for POK,DCOK and INIT
 POK,DCOK not critical: sigbal distnace longer than ARM INTR reaction time,
 INIT critical:
 	on QBUS, INIT is 10µs active (UNIBUS: > 10ms).
 	device states must go reset while INIT signal active.
 	Solution: stop PDP11 CPU until ARM acknoledges INIT events
 	Stopping done via DMR:


*/
#define _PRU1_STATEMACHINE_INITIALIZATION_C_

#include <stdint.h>
#include <stdbool.h>

#include "mailbox.h"
#include "pru1_buslatches.h"
#include "pru1_utils.h"
#include "pru1_timeouts.h"
#include "pru1_statemachine_arbitration.h"
#include "pru1_statemachine_initialization.h"


statemachine_initialization_t sm_initialization ;


void sm_initialization_reset(void) {
    sm_initialization.state = state_initialization_idle ;
	sm_initialization.bussignals_cur = buslatches_getbyte(5) & INITIALIZATIONSIGNAL_ANY; // now sampled
}


// detect signal change of INIT,POK and DCOK and sent event
// history initialized (among others) by powercycle
// DCOK,POK: slow transition, polled, bus signal lines directly evaluated by ARM
// INIT: fast transition (10us). Processed by state machine, 
//    IO register access DATI/DATO is must be delayed until ARM processed
//	  INIT for all emulated devices. Delay done by holding DMR until event acked.
//	  (Analoguous to dely for Regsiter access: there delay  by holding RPLY until EVENt acked.
void sm_initialization_func() {
// prev event processed.

// mailbox.events.init_signals_cur and _prev synchronouos with the vent,
// and delayed against bus lines!

	sm_initialization.bussignals_cur = buslatches_getbyte(5) & INITIALIZATIONSIGNAL_ANY; // now sampled

    if (sm_initialization.bussignals_cur & INITIALIZATIONSIGNAL_INIT) {
        sm_arb.device_request_mask = 0 ; // INIT clears all PRIORITY request signals
        // SACK cleared later on end of DMA transaction
    }

    // Power event
    uint8_t powersignals_prev = mailbox.events.power_signals_cur; // as ARM knows
    if ((powersignals_prev ^ sm_initialization.bussignals_cur) & (INITIALIZATIONSIGNAL_DCOK | INITIALIZATIONSIGNAL_POK)) {
        // DCOK or POK changed
        mailbox.events.power_signals_prev = powersignals_prev;
        mailbox.events.power_signals_cur = sm_initialization.bussignals_cur & (INITIALIZATIONSIGNAL_DCOK | INITIALIZATIONSIGNAL_POK);
        EVENT_SIGNAL(mailbox,power) ;
        PRU2ARM_INTERRUPT;
    }
	// INIT events
    switch(sm_initialization.state ) {
    case state_initialization_idle:
        if (sm_initialization.bussignals_cur & INITIALIZATIONSIGNAL_INIT) {
            // raising edge of INIT
			mailbox.events.init_signal_cur = 1 ;
            EVENT_SIGNAL(mailbox,init);
            PRU2ARM_INTERRUPT;
            // 50msec: longest time ARM unibusadapter needs accept PRU2ARM_INTERRUPT?
            TIMEOUT_SET(TIMEOUT_QBUS_INIT, MILLISECS(10));
            // wait with DMR asserted
            sm_arb.cpu_bus_inhibit_dmr_mask |= ARB_CPU_BUS_INHIBIT_DMR_INIT ;
            sm_initialization.state = state_initialization_init_asserted ;
        }
        break ;
    case state_initialization_init_asserted: {
        uint8_t ready;
        // wait for timeout or ACK of ARM event
        ready = EVENT_IS_ACKED(mailbox,init) ;
        if (!ready)
            TIMEOUT_REACHED(TIMEOUT_QBUS_INIT, ready) ;
        if (ready) {
			// wait for trailing edge of INIT, hold DMR
            if (!(sm_initialization.bussignals_cur & INITIALIZATIONSIGNAL_INIT)) {
                // trailing edge of INIT detected
// QBUS: signal only start of INIT                
//				mailbox.events.init_signal_cur = 0 ;
//                EVENT_SIGNAL(mailbox,init);
//                PRU2ARM_INTERRUPT;
                // 50msec: longest time ARM unibusadapter needs accept PRU2ARM_INTERRUPT?
                TIMEOUT_SET(TIMEOUT_QBUS_INIT, MILLISECS(10));
                sm_initialization.state = state_initialization_init_negated ;
            }
        }
    }
    break ;
    case state_initialization_init_negated: {
        uint8_t ready;
        // wait for timeout or ACK of ARM event
        ready = EVENT_IS_ACKED(mailbox,init) ;
        if (!ready)
            TIMEOUT_REACHED(TIMEOUT_QBUS_INIT, ready) ;
        if (ready) {
            // ARM processed trailing edge of INIT: negate DMR
            sm_arb.cpu_bus_inhibit_dmr_mask &= ~ARB_CPU_BUS_INHIBIT_DMR_INIT ;
            sm_initialization.state = state_initialization_idle ;
        }
    }
    break ;
    }
}




