/* pru1_statemachine_dma.c: state machine for bus master DMA

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


 Statemachines to execute multiple masterr DATO or DATI cycles.
 All references "PDP11BUS handbook 1979"
 Precondition: BBSY already asserted (arbitration got)

 Master reponds to INIT by stopping transactions.
 new state

 Start: setup dma mailbox setup with
 startaddr, wordcount, cycle, words[]
 Then sm_dma_init() ;
 sm_dma_state = DMA_STATE_RUNNING ;
 while(sm_dma_state != DMA_STATE_READY)
 sm_dma_service() ;
 state is 0 for OK, or 2 for timeout error.
 mailbox.dma.cur_addr is error location

 Speed: (clpru 2.2, -O3:
 Example: DATI, time SSYN- active -> (processing) -> MSYN inactive
 a) 2 states, buslatch_set/get function calls, TIMEOUT_SET/REACHED(75) -> 700ns
 b) 2 states, buslatch_set/get macro, TIMEOUT_SET/REACHED(75) -> 605ns
 c) 2 states, no TIMEOUT (75 already met) -> 430ns
 d) 1 marged state, no TIMEOUT  ca. 350ns

 ! Uses single global timeout, don't run in parallel with other statemachines using timeout  !
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "iopageregister.h"
#include "mailbox.h"
#include "pru1_buslatches.h"
#include "pru1_utils.h"
#include "pru1_timeouts.h"

#include "pru1_statemachine_arbitration.h"
#include "pru1_statemachine_dma.h"

/* sometimes short timeout of 75 and 150ns are required
 * 75ns between state changes is not necessary, code runs longer
 * 150ns between state changes is necessary
 * Overhead for extra state and TIMEOUTSET/REACHED is 100ns
 */

statemachine_dma_t sm_dma;

/********** Master DATA cycles **************/
// forwards ;
static statemachine_state_func sm_dma_state_1(void);
static statemachine_state_func sm_dma_state_11(void);
static statemachine_state_func sm_dma_state_21(void);
static statemachine_state_func sm_dma_state_99(void);

// dma mailbox setup with
// startaddr, wordcount, cycle, words[]   ?
// "cycle" must be QUNIBUS_CYCLE_DATI or QUNIBUS_CYCLE_DATO
// Wait for BBSY, SACK already held asserted
// Sorting between device and CPU transfers: unibusadapter request scheduler
statemachine_state_func sm_dma_start() {
	// assert BBSY: latch[1], bit 6
	// buslatches_setbits(1, BIT(6), BIT(6));

	mailbox.dma.cur_addr = mailbox.dma.startaddr;
	sm_dma.dataptr = (uint16_t *) mailbox.dma.words; // point to start of data buffer
	sm_dma.cur_wordsleft = mailbox.dma.wordcount;
	mailbox.dma.cur_status = DMA_STATE_RUNNING;

	// do not wait for BBSY here. This is part of Arbitration.
	buslatches_setbits(1, BIT(6), BIT(6)); // assert BBSY
	// next call to sm_dma.state() starts state machine
	return (statemachine_state_func) &sm_dma_state_1;
}

/*
 // wait for BBSY deasserted, then assert
 static statemachine_state_func sm_dma_state_1() {
 if (buslatches_getbyte(1) & BIT(6))
 return (statemachine_state_func) &sm_dma_state_1; // wait
 buslatches_setbits(1, BIT(6), BIT(6)); // assert BBSY
 return (statemachine_state_func) &sm_dma_state_1;
 }
 */

// place address and control bits onto bus, also data for DATO
// If slave address is internal (= implemented by UniBone),
// fast UNIBUS slave protocol is generated on the bus.
static statemachine_state_func sm_dma_state_1() {
	uint32_t tmpval;
	uint32_t addr = mailbox.dma.cur_addr; // non-volatile snapshot
	uint16_t data;
	uint8_t buscycle = mailbox.dma.buscycle;
	// uint8_t page_table_entry;

	//  BBSY released
	if (mailbox.dma.cur_status != DMA_STATE_RUNNING || mailbox.dma.wordcount == 0)
		return NULL; // still stopped

	if (sm_dma.cur_wordsleft == 1) {
		// deassert SACK, enable next arbitration cycle
		// deassert SACK before deassert BBSY
		// parallel to last word data transfer
		buslatches_setbits(1, BIT(5), 0); // SACK = latch[1], bit 5
	}

	sm_dma.state_timeout = 0;
//if (addr == 01046) // trigger address
// 	PRU_DEBUG_PIN0(1) ; // trigger to LA.

	// if M9312 boot vector active: 
	// Don't put addr on bus, read modified addr back and use,
	// But: use modifed addr internally, clear on external bus, 
	// no  UNIBUS member will do another DATI for it.
	addr |= address_overlay ;

	// addr0..7 = latch[2]
	buslatches_setbyte(2, addr & 0xff);
	// addr8..15 = latch[3]
	buslatches_setbyte(3, addr >> 8);
	// addr 16,17 = latch[4].0,1
	// C0 = latch[4], bit 2
	// C1 = latch[4], bit 3
	// MSYN = latch[4], bit 4
	// SSYN = latch[4], bit 5
	if (QUNIBUS_CYCLE_IS_DATO(buscycle)) {
		bool internal;
		bool is_datob = (buscycle == QUNIBUS_CYCLE_DATOB);
		tmpval = (addr >> 16) & 3;
		if (is_datob)
			tmpval |= (BIT(3) | BIT(2)); // DATOB: c1=1, c0=1
		else
			tmpval |= BIT(3); // DATO: c1=1, c0=0
		// bit 4,5 == 0  -> MSYN,SSYN not asserted
		buslatches_setbits(4, 0x3f, tmpval);
		// write data. SSYN may still be active and cleared now? by sm_slave_10 etc?
//		data = mailbox.dma.words[sm_dma.cur_wordidx];
		data = *sm_dma.dataptr;
		buslatches_setbyte(5, data & 0xff); // DATA[0..7] = latch[5]
		buslatches_setbyte(6, data >> 8); // DATA[8..15] = latch[6]
		// wait 150ns, but guaranteed to wait 150ns after SSYN inactive
		// prev SSYN & DATA may be still on bus, disturbes DATA
		while (buslatches_getbyte(4) & BIT(5))
			;	// wait for SSYN inactive
		__delay_cycles(NANOSECS(UNIBUS_DMA_MASTER_PRE_MSYN_NS) - 10);
		// assume 10 cycles for buslatches_getbyte and address test
		// ADDR, CONTROL (and DATA) stable since 150ns, set MSYN

		// use 150ns delay to check for internal address
		// page_table_entry = PAGE_TABLE_ENTRY(deviceregisters,addr);
		// !!! optimizer may not move this around !!!
		// try "volatile internal_addr" (__asm(";---") may be rearanged)

		// MSYN = latch[4], bit 4
		buslatches_setbits(4, BIT(4), BIT(4)); // master assert MSYN

		// DATO to internal slave (fast test).
		// write data into slave (
		if (is_datob) {
			// A00=1: upper byte, A00=0: lower byte
			uint8_t b = (addr & 1) ? (data >> 8) : (data & 0xff);
			internal = iopageregisters_write_b(addr, b); // always sucessful, addr already tested
		} else
			// DATO
			internal = iopageregisters_write_w(addr, data);
		if (internal) {
			buslatches_setbits(4, BIT(5), BIT(5)); // slave assert SSYN
			buslatches_setbits(4, BIT(4), 0); // master deassert MSYN
			buslatches_setbyte(5, 0); // master removes data
			buslatches_setbyte(6, 0);
			// perhaps ARM issued ARM2PRU_INTR, request set in parallel state machine.
			// Arbitrator will GRANT it after DMA ready (SACK deasserted).
			// assert SSYN after ARM completes "active" register logic
			// while (mailbox.events.event_deviceregister) ;

			buslatches_setbits(4, BIT(5), 0); // slave deassert SSYN
			return (statemachine_state_func) &sm_dma_state_99; // next word
		} else {
			// DATO to external slave
			// wait for a slave SSYN
			timeout_set(TIMEOUT_DMA, MICROSECS(UNIBUS_TIMEOUT_PERIOD_US));
			return (statemachine_state_func) &sm_dma_state_21; // wait SSYN DATAO
		}
	} else {
		// DATI or DATIP
		tmpval = (addr >> 16) & 3;
		// bit 2,3,4,5 == 0  -> C0,C1,MSYN,SSYN not asserted
		buslatches_setbits(4, 0x3f, tmpval);

		// wait 150ns after MSYN, no distance to SSYN required
		__delay_cycles(NANOSECS(UNIBUS_DMA_MASTER_PRE_MSYN_NS) - 10);
		// assume 10 cycles for buslatches_getbyte and address test
		// ADDR, CONTROL (and DATA) stable since 150ns, set MSYN next

		// use 150ns delay to check for internal address
		// page_table_entry = PAGE_TABLE_ENTRY(deviceregisters,addr);
		// !!! optimizer may not move this around !!!

		// MSYN = latch[4], bit 4
		buslatches_setbits(4, BIT(4), BIT(4)); // master assert MSYN

		if (iopageregisters_read(addr, &data)) {
			// DATI to internal slave: put MSYN/SSYN/DATA protocol onto bus,
			// slave puts data onto bus
			// DATA[0..7] = latch[5]
			buslatches_setbyte(5, data & 0xff);
			// DATA[8..15] = latch[6]
			buslatches_setbyte(6, data >> 8);
			// theoretically another bus member could set bits in bus addr & data ...
			// if yes, we would have to read back the bus lines
			*sm_dma.dataptr = data;
//			mailbox.dma.words[sm_dma.cur_wordidx] = data;

			buslatches_setbits(4, BIT(5), BIT(5)); // slave assert SSYN
			buslatches_setbits(4, BIT(4), 0); // master deassert MSYN
			buslatches_setbyte(5, 0); // slave removes data
			buslatches_setbyte(6, 0);
			// perhaps ARM issued ARM2PRU_INTR, request set in parallel state machine.
			// Arbitrator will GRANT it after DMA ready (SACK deasserted).
			// assert SSYN after ARM completes "active" register logic
			// while (mailbox.events.event_deviceregister) ;

			buslatches_setbits(4, BIT(5), 0); // slave deassert SSYN
			return (statemachine_state_func) &sm_dma_state_99; // next word
		} else {
			// DATI to external slave
			// wait for a slave SSYN
			timeout_set(TIMEOUT_DMA, MICROSECS(UNIBUS_TIMEOUT_PERIOD_US));
			return (statemachine_state_func) &sm_dma_state_11; // wait SSYN DATI
		}
	}
}

// DATI to external slave: MSYN set, wait for SSYN or timeout
static statemachine_state_func sm_dma_state_11() {
	uint16_t tmpval;
	sm_dma.state_timeout = timeout_reached(TIMEOUT_DMA);
	// SSYN = latch[4], bit 5
	if (!sm_dma.state_timeout && !(buslatches_getbyte(4) & BIT(5)))
		return (statemachine_state_func) &sm_dma_state_11; // no SSYN yet: wait
	// SSYN set by slave (or timeout). read data
	__delay_cycles(NANOSECS(75) - 6); // assume 2*3 cycles for buslatches_getbyte

	// DATA[0..7] = latch[5]
	tmpval = buslatches_getbyte(5);
	// DATA[8..15] = latch[6]
	tmpval |= (buslatches_getbyte(6) << 8);
	// save in buffer
	*sm_dma.dataptr = tmpval;
	// mailbox.dma.words[sm_dma.cur_wordidx] = tmpval;
	// negate MSYN
	buslatches_setbits(4, BIT(4), 0);
	// DATI: remove address,control, MSYN,SSYN from bus, 75ns after MSYN inactive
	__delay_cycles(NANOSECS(75) - 8); // assume 8 cycles for state change
	return (statemachine_state_func) &sm_dma_state_99;
}

// DATO to external slave: wait for SSYN or timeout
static statemachine_state_func sm_dma_state_21() {
	sm_dma.state_timeout = timeout_reached(TIMEOUT_DMA); // SSYN timeout?
	// SSYN = latch[4], bit 5
	if (!sm_dma.state_timeout && !(buslatches_getbyte(4) & BIT(5)))
		return (statemachine_state_func) &sm_dma_state_21; // no SSYN yet: wait

	// SSYN set by slave (or timeout): negate MSYN, remove DATA from bus
	buslatches_setbits(4, BIT(4), 0); // deassert MSYN
	buslatches_setbyte(5, 0);
	buslatches_setbyte(6, 0);
	// DATO: remove address,control, MSYN,SSYN from bus, 75ns after MSYN inactive
	__delay_cycles(NANOSECS(75) - 8); // assume 8 cycles for state change
	return (statemachine_state_func) &sm_dma_state_99;
}

// word is transfered, or timeout.
static statemachine_state_func sm_dma_state_99() {
	uint8_t final_dma_state;
	// from state_12, state_21

	// 2 reasons to terminate transfer
	// - BUS timeout at curent address
	// - last word transferred
	if (sm_dma.state_timeout) {
		final_dma_state = DMA_STATE_TIMEOUTSTOP;
		// deassert SACK after timeout, independent of remaining word count
		buslatches_setbits(1, BIT(5), 0); // deassert SACK = latch[1], bit 5
	} else {
		sm_dma.dataptr++;  // point to next word in buffer
		sm_dma.cur_wordsleft--;
		if (sm_dma.cur_wordsleft == 0)
			final_dma_state = DMA_STATE_READY; // last word: stop
		else if (buslatches_getbyte(7) & BIT(3)) { // INIT stops transaction: latch[7], bit 3
			// only bus master (=CPU?) can issue INIT
			final_dma_state = DMA_STATE_INITSTOP;
			// deassert SACK after INIT, independent of remaining word count
			buslatches_setbits(1, BIT(5), 0); // deassert SACK = latch[1], bit 5
		} else
			final_dma_state = DMA_STATE_RUNNING; // more words:  continue
	}

	if (final_dma_state == DMA_STATE_RUNNING) {
		// dataptr and words_left already incremented
		mailbox.dma.cur_addr += 2; // signal progress to ARM
		return (statemachine_state_func) &sm_dma_state_1; // reloop
	} else {
		// remove addr and control from bus. 
		// clears also address_overlay from bus
		buslatches_setbyte(2, 0);
		buslatches_setbyte(3, 0) ;
		buslatches_setbits(4, 0x3f, 0);
		// remove BBSY: latch[1], bit 6
		buslatches_setbits(1, BIT(6), 0);

		timeout_cleanup(TIMEOUT_DMA);

		// SACK already de-asserted at wordcount==1
		mailbox.dma.cur_status = final_dma_state; // signal to ARM

		// device or cpu cycle ended
		// no concurrent ARM+PRU access

		// for cpu access: ARM CPU thread ends looping now
		// test for DMA_STATE_IS_COMPLETE(cur_status)
		EVENT_SIGNAL(mailbox, dma);

		// for device DMA: unibusadapter worker() waits for signal
		if (!mailbox.dma.cpu_access) {
			// signal to ARM
			// ARM is clearing this, before requesting new DMA.
			// no concurrent ARM+PRU access
			PRU2ARM_INTERRUPT
			;
		}
//		PRU_DEBUG_PIN0_PULSE(50) ;  // CPU20 performace
		
		return NULL; // now stopped
	}
}

