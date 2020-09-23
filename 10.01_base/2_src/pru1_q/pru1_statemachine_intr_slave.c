/* pru1_statemachine_intr_slave.c:  CPU receives interrupt vector

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


 26-aug-2019	JH		start

 State machines for CPU emulation to receive INTR vector placed onto bus by device.
 

 All references "PDP11BUS handbook 1979"

 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

//#include "devices.h"
#include "mailbox.h"
#include "pru1_buslatches.h"
#include "pru1_utils.h"

#include "pru1_statemachine_intr_slave.h"

// states
statemachine_intr_slave_t sm_intr_slave;

// forwards
static statemachine_state_func sm_intr_slave_state_1(void);

// WAIT for INTR. 
// Master holds BBSY and SACK
statemachine_state_func sm_intr_slave_start() {
	if ((buslatches_getbyte(7) & BIT(0)) == 0)
		return NULL ;	// INTR still negated
	// device has put vector onto DATA lines, fetch after 150ns
	__delay_cycles(NANOSECS(150));
	uint8_t	latch5val = buslatches_getbyte(5) ;// DATA[0..7] = latch[5]
	uint8_t	latch6val = buslatches_getbyte(6) ; // DATA[8..15] = latch[6]

	// set SSYN = latch[4], bit 5
	buslatches_setbits(4, BIT(5), BIT(5));

	// mark priority level as invalid, block more BG GRANTS until PSW fetched
	mailbox.arbitrator.ifs_priority_level = CPU_PRIORITY_LEVEL_FETCHING ;

	// signal ARM, wait for event to be processed
	mailbox.events.intr_slave.vector = (uint16_t) latch6val << 8 | latch5val ;

	EVENT_SIGNAL(mailbox,intr_slave) ; 	// signal to ARM
	
	PRU2ARM_INTERRUPT ;
	// wait until ARM acked
	return (statemachine_state_func) &sm_intr_slave_state_1;
}

static statemachine_state_func sm_intr_slave_state_1() {

	// wait until ARM acked the INTR vector
	// event_intr_slave ACK is delayed until the CPU
	// CPU has read the new PSW and  new abritration level
	// event_intr_slave co solved by 
	if (! EVENT_IS_ACKED(mailbox,intr_slave))
		return (statemachine_state_func) &sm_intr_slave_state_1;
	
	// wait if INTR still active	
	if (buslatches_getbyte(7) & BIT(0)) // check INTR
		return (statemachine_state_func) &sm_intr_slave_state_1;

	// clear SSYN = latch[4], bit 5
	buslatches_setbits(4, BIT(5), 0);

	// now CPU may do DATI to fetch PC and PSW

	return NULL; // ready
}

