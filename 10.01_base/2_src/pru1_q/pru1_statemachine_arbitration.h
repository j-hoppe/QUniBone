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


// states (switch() test order)
enum sm_arbitration_states_enum {
    // device DMA/IRQ
    state_arbitration_grant_check,
    state_arbitration_dma_grant_rply_sync_wait,
    state_arbitration_intr_vector,
    state_arbitration_intr_complete,
    // no arbitration, steady state
    state_arbitration_noop
} ;


// a priority-arbitration-worker returns a bit mask with the GRANT signal he recognized

	// reasons to inhibit QBUS access of LSI11 CPU via DMR
#define ARB_CPU_BUS_INHIBIT_DMR_ARM 0x01 // via ARM2PRU_CPU_BUS_ACCESS
#define ARB_CPU_BUS_INHIBIT_DMR_INIT 0x02 // elongate INIT 


typedef struct {
    enum sm_arbitration_states_enum state;

    // There are 5 request/grant signals (INTR,5,6,7 and NPR).
    // INTR/DMR lines = set of _PRIORITY_ARBITRATION_BIT_*
    // corresponding GRANt signals are constructed by CPLD2
    // INTR<4,5,6,7> are NOT directly output to BIRQ<4:7>, but generate combinations of these
    uint8_t device_request_mask;

    // device_request_mask when actually on BR/NR lines
    uint8_t device_request_signalled_mask;

    // forwarded to GRANT OUT, not accepted as response to device_request_signalled_mask
    uint8_t device_forwarded_grant_mask;

    uint8_t device_grant_mask ; // single bit grant processed in state machine

    // sm_arb has several states: for DMA and INTR ACK.
    // On INTR completion the ARM event for one of INTR4,5,6,7 must be signaled, save its idx.
    uint8_t intr_level_index ;

    /*** master ****/
    // CPU is requesting memory access via PRU2ARM_DMA/mailbox.dma
    uint8_t cpu_request;

    uint8_t arbitrator_grant_mask; // single GRANT line set by master

	/*** CPU blocking via DMR ***/
	// several reasons to set DMR, which inhibits LSI11 CPU from bus accesses

	uint8_t cpu_bus_inhibit_dmr_mask ; // OR of ARB_CPU_BUS_INHIBIT_DMR_*

//    uint8_t dummy[1]; // make it dword-sized

} statemachine_arbitration_t;

/* receives a grant_mask with 1 bit set and returns the index of that bit
 when interpreted as intr or dma request
 INTR4->0, INTR5->1, INTR6->2, INTR7->3, DMR->4
 grant_mask as value from buslatch 6/7: INTR4 at bit 0
 Undefined result if grant_mask empty or > 0x10
 */
#define PRIORITY_ARBITRATION_INTR_BIT2IDX(grant_mask)	\
	__lmbd((grant_mask), 1)
// "LMBD" = "Left Most Bit Detect"

extern statemachine_arbitration_t sm_arb;

void sm_arb_reset(void);
uint8_t sm_arb_worker_device(uint8_t granted_requests_mask);
uint8_t sm_arb_worker_cpu(void);

#endif
