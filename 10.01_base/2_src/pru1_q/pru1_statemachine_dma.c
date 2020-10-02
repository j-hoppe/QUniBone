/* pru1_statemachine_dma.c: state machine for QBUS master DMA

 Copyright (c) 2018-2020, Joerg Hoppe
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


 01-aug-2020	JH		start QBUS
 29-jun-2019	JH		rework: state returns ptr to next state func
 12-nov-2018	JH      entered beta phase


 Statemachines to execute multiple master DATO or DATI cycles.
 All references "LSI-11 BUS SPEC DEC STANDARD 160 REV A"

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

 Uses global timeout
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
static statemachine_state_func sm_dma_state_addr(void);
static statemachine_state_func sm_dma_state_dinstart(void);
static statemachine_state_func sm_dma_state_dincomplete(void);
static statemachine_state_func sm_dma_state_doutstart(void);
static statemachine_state_func sm_dma_state_doutcomplete(void);
static statemachine_state_func sm_dma_state_99(void);

// dma mailbox setup with
// startaddr, wordcount, cycle, words[]   ?
// "cycle" must be QUNIBUS_CYCLE_DATI or QUNIBUS_CYCLE_DATO
// DATIO not supported,
// DATBI and DATBO (block mode) automatically tried, max 8 words before new cycle
// SACK already held asserted
// Sorting between device and CPU transfers: qunibusadapter request scheduler

// start: entry, no func
// Precondition: bus mastership granted, SACK maybe set
statemachine_state_func sm_dma_start() {
    // assert BBSY: latch[1], bit 6
    // buslatches_setbits(1, BIT(6), BIT(6));

    mailbox.dma.cur_addr = mailbox.dma.startaddr;
    sm_dma.dataptr = (uint16_t *) mailbox.dma.words; // point to start of data buffer
    sm_dma.words_left = mailbox.dma.wordcount;
    mailbox.dma.cur_status = DMA_STATE_RUNNING;

    // next call to sm_dma.state() starts state machine
    return (statemachine_state_func) &sm_dma_state_addr;
}

// address portion of the cycle
// gate ADDR,BS7 and WTBT onto DAL lines
// If slave address is internal (= implemented by QBone),
// fast slave protocol is generated on the bus.
static statemachine_state_func sm_dma_state_addr() {
    uint32_t addr = mailbox.dma.cur_addr; // non-volatile snapshot
    uint8_t buscycle = mailbox.dma.buscycle;

    if (mailbox.dma.cur_status != DMA_STATE_RUNNING || mailbox.dma.wordcount == 0)
        return NULL; // still stopped

    sm_dma.state_timeout = 0;
//if (addr == 01046) // trigger address
// 	PRU_DEBUG_PIN0(1) ; // trigger to LA.

    // "As soon as a Bus Master gains control of the bus, it gates
    // TADDR, TBS7 and WTBT onto the bus"
    // DAL<7..0> = latch0
    buslatches_setbyte(0, addr & 0xff);
    // DAL<15..8> = latch[1]
    buslatches_setbyte(1, addr >> 8);

    // DAL<21..16> = latch2<5..0>. BS7 = latch2<6>, SYNC still negated
    // latch2<6> is bs7, as BS7 is (1 <<22)
    buslatches_setbyte(2, (addr >> 16)); // BIT(7)= SYNClatch is read only, not changed)

    // WTBT (Bit 4): early indicator of DATO
    // assertion: BS7 in reg 4 always negated after din/dout_complete(), so write to reg 4 will not assert it silently
    if (QUNIBUS_CYCLE_IS_DATO(buscycle))
        buslatches_setbits(4, BIT(4), BIT(4));


    // BS7 (Bit 5): prev DATA BS7 is cached in reg4.cur_reg_val from prev DATA portion, set to ADDR BS7.
    /*    {
        uint8_t tmp = (addr & QUNIBUS_IOPAGE_ADDR_BITMASK) ? BIT(5) : 0 ;
      	if (QUNIBUS_CYCLE_IS_DATO(buscycle))
            tmp |= BIT(4);
    	buslatches_setbits(4, BIT(4)+BIT(5), tmp);
        }
    */

    // "The Bus Master asserts TSYNC 150 ns minimum after it gates
    // TADDR, TBS7, and TWTBT onto the Bus; 300 ns minimum after the
    // negation of RRPLY, 250 ns minimum after the negation of RSYNC
    // (if another device had asserted BSYNC; and 200 ns after the
    // negation of TSYNC (if the current Bus Master had asserted BSYNC)."
    __delay_cycles(NANOSECS(0)); // timed with LA

    // "The Bus Master continues to gate TADDR, TBS7, and WTBT onto
    // the Bus for 100 ns minimum after the assertion of TSYNC."
//	__delay_cycles(NANOSECS(100-50)) ; PRU runtime longer

    // how many word can be transferred in DATBI/BO block cycle?
    // "The Bus Master must limit itself to not more than eight transfers unless it
    // monitors RDMR. If it monitors RDMR, it may continue with blocks of eight so long
    // as RDMR is not asserted at the end of each seventh transfer."
    sm_dma.block_words_left = (sm_dma.words_left > 8) ? 8 : sm_dma.words_left;
    sm_dma.first_data_portion = true ; // remove ADDR before DIN/DOUT
    buslatches_setbits(4, BIT(0), BIT(0)); // assert SYNC, as late as possible. DAL self-latched

    // addr portion ready, SYNC asserted. ADDR remains on DAL
    if (QUNIBUS_CYCLE_IS_DATO(buscycle)) {
        sm_dma.block_data_state_func = (statemachine_state_func) &sm_dma_state_doutstart;
        return (statemachine_state_func) &sm_dma_state_doutstart;
    } else {
        sm_dma.block_data_state_func = (statemachine_state_func) &sm_dma_state_dinstart;
        return (statemachine_state_func) &sm_dma_state_dinstart;
    }
}

// initiate a DIN
statemachine_state_func sm_dma_state_dinstart() {
    uint32_t addr = mailbox.dma.cur_addr; // non-volatile snapshot, BS7 encoded
    uint16_t data;

    // "The Bus Master asserts TDIN 100 ns minimum after asserting TSYNC"
    // "The Bus Master asserts TBS7 50 ns maximum after asserting TDIN
    // for the first time. TBS7 remains asserted until 50 ns maximum
    // after the assertion of TDIN for the last time. In each case,
    // TBS7 can be asserted or negated as soon	as the conditions for
    // asserting TDIN are met."
    if (sm_dma.block_words_left > 1) // not on last TDIN of block
        buslatches_setbits(4, BIT(5)+BIT(1), 0xff); // assert DIN,BS7
    else
        buslatches_setbits(4, BIT(5)+BIT(1), BIT(1)); // assert DIN, negate BS7

    // remove ADDR from DAL while slave puts DATA onto it
    if (sm_dma.first_data_portion) {
        // no benefit, could as well setbyte() everytime
        sm_dma.first_data_portion=false ;
        buslatches_setbyte(3, 0x03) ; // cmd code "clr DAL and self latched DAL"
    }

    if (emulated_addr_read(addr, &data)) {
        // DATI to internal slave: put DIN/RPLY/DATA protocol onto bus,
        // Lazy: do not set REF, still do block mode

        // DATA[0..7] = DAL<7..0> = latch[0]
        buslatches_setbyte(0, data & 0xff);
        // DATA[8..15] = DAL<15..8> = latch[1]
        buslatches_setbyte(1, data >> 8);
        // theoretically another bus member could set bits in bus addr & data ...
        // if yes, we would have to read back the bus lines

        buslatches_setbits(4, BIT(3), BIT(3)); // "slave assert RPLY", +
        // "The Bus slave gates TDATA onto the Bus 0 ns minimum after assertion of RDIN
        // and 125 ns maximum after assertion of TRPLY."
        *sm_dma.dataptr = data;
        //			mailbox.dma.words[sm_dma.cur_wordidx] = data;

        // "The Bus Master negates TDIN 200ns minimum after the assertion of RPLY".
        __delay_cycles(NANOSECS(80)); // timed with LA
        // "The Bus Slave negates TRPLY 0 ns minimum after the negation of RDIN."
        buslatches_setbits(4, BIT(1)+BIT(3), 0); // clr RPLY and DIN
        // "The Bus Slave continues to gate TDATA onto the bus for 0 ns minimum and 100 ns maximum after negating TRPLY".
        buslatches_setbyte(3, 0x02) ; // cmd code "clr DAL"

        sm_dma.block_words_left--;
        if (sm_dma.block_words_left == 0) {
            sm_dma.block_data_state_func = NULL;	// signal to state 99: "end of block""
        }

        // perhaps ARM issued ARM2PRU_INTR, request set in parallel state machine.
        // Arbitrator will GRANT it after DMA ready (SACK negated).
        // assert RPLY after ARM completes "active" register logic
        // while (mailbox.events.event_deviceregister) ;
        return (statemachine_state_func) &sm_dma_state_99; // next word
    } else {
        // DATI to external slave
        // wait for a slave SSYN
        TIMEOUT_SET(TIMEOUT_DMA, MICROSECS(QUNIBUS_TIMEOUT_PERIOD_US)); // runs 90 ns
        return (statemachine_state_func) &sm_dma_state_dincomplete; // wait RPLY DATI
    }
}

// initiate a DOUT
statemachine_state_func sm_dma_state_doutstart() {
    uint32_t addr = mailbox.dma.cur_addr; // non-volatile snapshot, BS7 encoded
    uint8_t buscycle = mailbox.dma.buscycle;
    uint16_t data;

    bool internal;
    bool is_datob = (buscycle == QUNIBUS_CYCLE_DATOB);
    // "The Bus Master gates DATA and WTBT onto the Bus 100ns minimum after TSYNC.
    // TWTBT is negated for DATO Cycles and asserted for DATOB cycles
    // write data.
    data = *sm_dma.dataptr;
//	if (addr == 0400010)
//		PRU_DEBUG_PIN0(1);

    buslatches_setbyte(0, data & 0xff); // DATA[0..7] = DAL<7..0> = latch[0]
    buslatches_setbyte(1, data >> 8); // DATA[8..15]] = DAL<15..8> = latch[1]
    if (sm_dma.first_data_portion) {
        // only once before DOUT
        sm_dma.first_data_portion=false ;
        if (is_datob)
            buslatches_setbits(4, BIT(4)+BIT(5), BIT(4)); // assert WTBT, negate BS7
        else
            buslatches_setbits(4, BIT(4)+BIT(5), 0); // negate BS7,WTBT
    }
    // "The Bus Master asserts DOUT 100 ns minimum after gating TDATA on to the Bus."
    __delay_cycles(NANOSECS(20)); // timed with LA
    // UNIBUS_DMA_MASTER_PRE_MSYN_NS ?

    buslatches_setbits(4, BIT(2), BIT(2)); // master asserts DOUT

    // DATO to internal slave (fast test).
    // write data into slave (
    if (is_datob) {
        // A00=1: upper byte, A00=0: lower byte
        uint8_t b = (addr & 1) ? (data >> 8) : (data & 0xff);
        internal = emulated_addr_write_b(addr, b); // always sucessful, addr already tested
    } else
        // DATO
        internal = emulated_addr_write_w(addr, data);
    if (internal) {
        // slave protocol onto bus. Lazy: do not set REF, still do block mode
        buslatches_setbits(4, BIT(3), BIT(3)); // slave assert RPLY
        __delay_cycles(NANOSECS(30));// timed with LA
        buslatches_setbits(4, BIT(2), 0); // master negates DOUT 150ns after RPLY
        __delay_cycles(NANOSECS(40)); // timed with LA
        // master removes data minimum 100ns after negating DOUT
        buslatches_setbyte(3, 0x02) ; // cmd code "clr DAL"
//		if (is_datob)
//			buslatches_setbits(4, BIT(0), 0); SYNC???
        buslatches_setbits(4, BIT(3), 0); // slave negates RPLY

        sm_dma.block_words_left--;
        if (sm_dma.block_words_left == 0) {
            sm_dma.block_data_state_func = NULL;	// signal to state 99: "end of block""
        }

        // perhaps ARM issued ARM2PRU_INTR, request set in parallel state machine.
        // Arbitrator will GRANT it after DMA ready (SACK negated).
        // assert RPLY after ARM completes "active" register logic
        // while (mailbox.events.event_deviceregister) ;

        // master still holds SYNC
        return (statemachine_state_func) &sm_dma_state_99; // next word
    } else {
        // DATO to external slave
        // wait for a slave RPLY
        // "The Bus Slave asserts TRPLY 0 ns minimum (8000 ns maximum to
        // avoid Bus Timeout) after the assertion of RDOUT."
        TIMEOUT_SET(TIMEOUT_DMA, MICROSECS(QUNIBUS_TIMEOUT_PERIOD_US)); // runs 280 ns !!!
        return (statemachine_state_func) &sm_dma_state_doutcomplete; // wait RPLY DATAO
    }
}

// DATI to external slave: DIN set, wait for RPLY or timeout
static statemachine_state_func sm_dma_state_dincomplete() {
    uint16_t tmpval;

    // "The slave asserts TRPLY 0ns minimum (8000 ns maximum to avoid bus timeouts)
    // after assertion of RDIN."
    if (!(buslatches_getbyte(4) & BIT(3))) { // RPLY?
        // No RPLY yet, check timeout
        TIMEOUT_REACHED(TIMEOUT_DMA, sm_dma.state_timeout); // 110 ns
        if (!sm_dma.state_timeout)
            return (statemachine_state_func) &sm_dma_state_dincomplete; // no RPLY yet: wait
        // on timeout: proceed to end cycle
    }

    // RPLY set by slave (or timeout).

    // "The Bus Master receives stable RDATA from 200 ns maximum after the assertion
    // RRPLY until 20 ns minimum after the negation of TDIN.
    // The 20 ns represents total minimum receiver delays for RDIN
    // at the slave and RDATA at the Master)."
    __delay_cycles(NANOSECS(150)); // timed with LA

    // DATA[0..7] = DAL<7..0> = latch[0]
    tmpval = buslatches_getbyte(0);
    // DATA[8..15] = DAL<15..8> = latch[1]
    tmpval |= (buslatches_getbyte(1) << 8);
    // save in buffer
    *sm_dma.dataptr = tmpval;

    // "The Bus Slave asserts TREF concurrent with TRPLY if, and only if, it is a
    // block mode device which can support another RDIN after the current RDIN""
    sm_dma.block_words_left--;
    if (sm_dma.block_words_left == 0 || (buslatches_getbyte(4) & BIT(6)) == 0) {
        // stop block mode data portions
        sm_dma.block_data_state_func = NULL;	// signal to state 99: "end of block""
    }

    // mailbox.dma.words[sm_dma.cur_wordidx] = tmpval;
    // negate DIN
    // "The Bus master negates TDIN 200 ns minimum after the assertion of RRPLY"
    buslatches_setbits(4, BIT(1), 0);
    // "The Bus Slave negates TRPLY 0 ns minimum after the negation of RDIN"
    // "The Bus Slave continues to gate TDATA onto the Bus for 0ns minimum and
    //	100 ns maximum after negating TRPLY"

    // SYNC remains set by master
    // "The Bus Master  negates  TSYNC  250  nsec  minimum  after the
    // assertion of RRPLY and 0 ns minimum after the negation  of RRPLY".
    return (statemachine_state_func) &sm_dma_state_99;
}

// DATO to external slave: DOUT set, wait for RPLY or timeout
static statemachine_state_func sm_dma_state_doutcomplete() {
    // "The Bus Slave receives stable RDATA and RWTBT from 25 ns
    // minimum before the assertion of RCOUT until 25 ns minimum
    // after the negation of RDOUT."
    // "The Bus Slave asserts TRPLY 0 ns minimum (8000 ns maximum to
    // avoid Bus Timeout) after the assertion of RDOUT."
    if (!(buslatches_getbyte(4) & BIT(3))) { // RPLY?
        // No RPLY yet, check timeout
        TIMEOUT_REACHED(TIMEOUT_DMA, sm_dma.state_timeout);
        if (!sm_dma.state_timeout)
            return (statemachine_state_func) &sm_dma_state_doutcomplete; // no RPLY yet: wait
        // on timeout: proceed to end cycle
    }
    // RPLY set by slave (or timeout): negate DOUT, remove DATA from bus
    // "The Bus Master negates TDOUT 150 ns minimum after the
    // assertion of RRPLY.""
    __delay_cycles(NANOSECS(50)); // timed with LA

    buslatches_setbits(4, BIT(2), 0); // negate DOUT
    // "The Bus Master continues to gate TDATA and TWTBT onto the bus
    // for 100 ns minimum after negating TDOUT.""

    __delay_cycles(NANOSECS(0)); //timed with LA

    // "The Bus Slave asserts TREF concurrent with TRPLY if, and only if, it is a
    // block mode device which can support another RDOUT after the current RDOUT"
    sm_dma.block_words_left--;
    if (sm_dma.block_words_left == 0 || (buslatches_getbyte(4) & BIT(6)) == 0) {
        // master or slave stop block mode data portions
        sm_dma.block_data_state_func = NULL;	// signal to state 99: "end of block""
        buslatches_setbyte(3, 0x2); // "clr DAL, remove data
        buslatches_setbits(4, BIT(4), 0);  // negate WTBT
    }

    // "The Bus Slave negates TRPLY 0 ns minimum after the negation of RDOUT."
    // "The Bus Master negates TSYNC 175 ns minimum after negating
    // TDOUT, 0 ns minimum after removing TDATA and TWTBT from the
    // Bus, and 0 ns minimum after the negation of RRPLY.""
    return (statemachine_state_func) &sm_dma_state_99;
}

// word is transfered, or timeout.
static statemachine_state_func sm_dma_state_99() {
    uint8_t final_dma_state;
    // from state_12, state_21

    //  "The Bus Master negates TSYNC 250 nsec minimum after the
    // assertion of RRPLY and 0 ns minimum after the negation of RRPLY."
    // 250 ns since RPLY already passed, see sate _11 and _21

    // 2 reasons to terminate transfer
    // - BUS timeout at curent address
    // - last word transferred

    if (sm_dma.state_timeout) {
        final_dma_state = DMA_STATE_TIMEOUTSTOP;
        // negate SACK after timeout, independent of remaining word count
    } else {
        sm_dma.dataptr++;  // point to next word in buffer
        sm_dma.words_left--;
        if (sm_dma.words_left == 0) {
            final_dma_state = DMA_STATE_READY; // last word: stop
        } else if (buslatches_getbyte(5) & BIT(0)) { // INIT stops transaction
            // only bus master (=CPU?) can issue INIT
            final_dma_state = DMA_STATE_INITSTOP;
            // negate SACK after INIT, independent of remaining word count
        } else {
            final_dma_state = DMA_STATE_RUNNING; // more words:  continue
            // dataptr and words_left already updated
            mailbox.dma.cur_addr += 2; // signal progress to ARM, next addr to output
            if (sm_dma.block_data_state_func)
                return sm_dma.block_data_state_func; // Next data portion in DATBI or DATBO
            buslatches_setbits(4, BIT(0)+BIT(5), 0); // negate SYNC, BS7(block indicator)
            // SACK still asserted
            return (statemachine_state_func) &sm_dma_state_addr; // reloop: output next address
        }
    }
    // here if state != running: end of multi word transfer

    // "The bus master negates TSYNC 300ns maximum after it negates SACK"
    buslatches_setbits(6, BIT(7), 0); // negate SACK
    buslatches_setbits(4, BIT(0)+BIT(5), 0); // negate SYNC, BS7(block indicator)

    mailbox.dma.cur_status = final_dma_state; // signal to ARM

    // device or cpu cycle ended
    // no concurrent ARM+PRU access

    // for cpu access: ARM CPU thread ends looping now
    // test for DMA_STATE_IS_COMPLETE(cur_status)
    EVENT_SIGNAL(mailbox, dma);

    // for device DMA: qunibusadapter worker() waits for signal
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

