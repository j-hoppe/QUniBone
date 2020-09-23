/* pru1_statemachine_arbitration.h: state machine for INTR/DMA arbitration

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

 29-jun-2019	JH		rework: state returns ptr to next state func
 12-nov-2018  JH      entered beta phase
 */
#ifndef  _PRU1_STATEMACHINE_ARBITRATION_H_
#define  _PRU1_STATEMACHINE_ARBITRATION_H_

#include <stdint.h>

// Arbitration master restes GRANT if device does not respond with
// SACK within this period.
#define ARB_MASTER_SACK_TIMOUT_MS	10

// a priority-arbitration-worker returns a bit mask with the GRANT signal he recognized

typedef uint8_t (*statemachine_arb_worker_func)(uint8_t grant_mask);

typedef struct {
	// There are 5 request/grant signals (BR4,5,6,7 and NPR).
	// These are encoded as bitmask fitting the buslatch[0] or[1]
	// BR/NPR lines = set of _PRIORITY_ARBITRATION_BIT_*
	uint8_t device_request_mask;

	// device_request_mask when actually on BR/NR lines
	uint8_t device_request_signalled_mask;

	// forwarded to GRANT OUT, not accepted as response to device_request_signalled_mask 
	uint8_t device_forwarded_grant_mask;

	// sm_arb has 2 states: State 1 "Wait for GRANT" and State 2 "wait for BBSY"
	// When arbitrator GRANts a request, we set SACK, GRAMT is cleared and we wait
	// for BBSY clear. 
	// 0: not waitong for BBSY.
	// != saves GRANTed request and indicates BBSY wait state
	uint8_t grant_bbsy_ssyn_wait_grant_mask;

	/*** master ****/
	// CPU is requesting memory access via PRU2ARM_DMA/mailbox.dma
	uint8_t cpu_request;

	uint8_t arbitrator_grant_mask; // single GRANT line set by master

	uint8_t dummy[2]; // make it dword-sized

} statemachine_arbitration_t;

/* receives a grant_mask with 1 bit set and returns the index of that bit
 when interpreted as intr odma request
 BR4->0, BR5->1,BR6->2, BR7->3,NPR->4
 grant_mask as value from buslatch 0: BR4 at bit 0
 Undefined result if grant_mask empty or > 0x10
 */
#define PRIORITY_ARBITRATION_INTR_BIT2IDX(grant_mask)	\
	__lmbd((grant_mask), 1)
// "LMBD" = "Left Most Bit Detect"

extern statemachine_arbitration_t sm_arb;

void sm_arb_reset(void);
uint8_t sm_arb_worker_none(uint8_t grant_mask);
uint8_t sm_arb_worker_device(uint8_t grant_mask);
uint8_t sm_arb_worker_cpu(void);

#endif
