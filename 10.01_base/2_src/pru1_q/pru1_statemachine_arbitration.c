/* pru1_statemachine_arbitration.c: state machine for INTR/DMA arbitration

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

 Statemachine for execution of the Priority Arbitration protocol
 NPR arbitration and BR interrupt arbitration

 PRU handles all 5 requests in parallel:
 4x INTR BR4-BR7
 1x DMA NPR.
 Several ARM device may raise the same BR|NPR level, ARM must serialize this to PRU.

 Flow:
 1. ARM sets a REQUEST by
 filling the RQUEST struct and perhaps DMA data
 doing AMR2PRO_PRIORITY_ARBITRATION_REQUEST,
 2. PRU sets BR4567|NPR lines according to open requests
 3. PRU monitors IN GRANT lines BG4567,NPG.
 IN state of idle requests is forwarded to BG|NPG OUT liens,
 to be processed by other QBUS cards.
 BG*|NPG IN state line of active request clears BR*|NPR line,
 sets SACK, and starts INTR or DMA state machine.
 4. INTR or DMA sent a signal on compelte to PRU.
 PRU may then start next request on same (completed) BR*|NPR level.

 All references "PDP11BUS handbook 1979"
 - At any time, CPU receives NPR it asserts NPG
 - between CPU instructions:
 if PRI < n and BRn is received, assert BGn
 else if PRI < 7 and BR7 is reived, assert BG7
 else if PRI < 6 and BR6 is reived, assert BG6
 else if PRI < 5 and BR5 is reived, assert BG5
 else if PRI < 4 and BR4 is reived, assert BG4


 If PRU detects a BGINn which it not requested, it passes it to BGOUTn
 "passing the grant"
 if PRU detects BGIN which was requests, it "blocks the GRANT" )sets SACK and
 transmit the INT (BG*) or becomes
 "no interrupt request while NPR transfer active!"
 Meaning: bus mastership acquired by NPG may not be used to transmit an
 INTR vector.

 Device may take bus if SYNC==0 && RPLY==0

 Device timing: assert DMR, wait for DMGI, assert SACK, wait for NPG==0,
 data cycles, set SACK=0 after last RPLY, set negate SYNC 200ns max after negate SACK

 BBSY is set before SACK is released. SACK is released immediately after BBSY,
 enabling next arbitration in parallel to current data transfer
 "Only the device which holds SACk asserted can assert BBSY


 Several arbitration "workers" which set request, monitor or generate GRANT signals
 and allocate SACK.
 Which worker to use depends on whether a physical PDP-11 CPU is Arbitrator,
 the Arbitrator is implemented here (CPU emulation),
 or DMA should be possible always
 (even if some other CPU monitor is holding SACK (11/34).
 */

#define _PRU1_STATEMACHINE_ARBITRATION_C_

#include <stdint.h>

#include "pru1_utils.h"

#include "mailbox.h"
#include "pru1_timeouts.h"

#include "pru1_buslatches.h"
#include "pru1_timeouts.h"
#include "pru1_statemachine_arbitration.h"

statemachine_arbitration_t sm_arb;


/********** NPR/NPG/SACK arbitrations **************/

// to be called on INIT signal: abort the arbitration process
void sm_arb_reset() {
    // cleanup: clear all IRQ/DMR Requests and SACK
    buslatches_setbits(6, PRIORITY_ARBITRATION_BIT_MASK | BIT(7), 0);
    sm_arb.device_request_mask = 0;
    sm_arb.device_forwarded_grant_mask = 0;
    sm_arb.device_request_signalled_mask = 0;

    sm_arb.intr_level_index = 0 ;

    sm_arb.cpu_request = 0;
    sm_arb.arbitrator_grant_mask = 0;

	sm_arb.cpu_bus_inhibit_dmr_mask = 0;
}

/* sm_arb_workers_*()
 If return !=0: we have SACK on the GRANT lines return in a bit mask
 see PRIORITY_ARBITRATION_BIT_*
 */


/* worker_device():
 Issue request to extern or emulated Arbitrator (PDP-11 CPU).
 CPLD2 decodes IAKI + IRQ to IAKI4..7, IOAKO4..7 are all IAKO
 Watch for IAKI4..7/DMG on the bus signal lines, then raise SACK for DMG.
 Wait for current bus master to release bus => Wait for SYNC and RPLY clear.
 Then return GRANTed request.
 "Wait for SYNC and RPLY clear" may not be part of the arbitration protocol.
 But it guarantees caller may now issue an DMA or INTR.

 granted_requests_mask: state of all IAGI4..7/DMGI lines,
 as forwarded by other devices from physical CPU
 or generated directly by emulated CPU
 result: grants, which the device has accepted via protocol
 */
uint8_t sm_arb_worker_device(uint8_t granted_requests_mask) {
#ifdef TODO
    if (sm_arb.cpu_request) {
        // Emulated CPU memory access: no NPR/NPG/SACK protocol.
        // No arbitration, start transaction when bus idle.
        // device_request_mask is ignored for CPU
        uint8_t latch1val = buslatches_getbyte(1);

        /* Do not GRANT cpu memory ACCESS if -
         -	SACK
         - NPR pending
         - BR4-7 request and ifs_arbitration_pending
         (Deadlock ahead: CPu needs to execute program to reach point before fecth",
         where INTRs are granted.)
         */
        bool granted = true;
        if ((latch1val & 0x70) != 0)
            // NPR, BBSY or SACK set
            granted = false;
        else if ((latch1val & 0xf) && mailbox.arbitrator.ifs_intr_arbitration_pending)
            // BR* set, and next is opcode fetch: INTR first
            granted = false;
        if (granted) {
            // neither REQUESTs nor SACK nor BBSY asserted
            sm_arb.cpu_request = 0;
            return PRIORITY_ARBITRATION_BIT_NP;
            // DMA will be started, BBSY will be set
        } else {
            // CPU memory access delayed until device requests processed/completed
        }
    }
#endif

    // read GRANT IN lines from CPU (Arbitrator).
    // Only one bit on cpu_grant_mask at a time may be active, else arbitrator or CPLD2 malfunction.
    // Arbitrator asserts SACK is inactive
    switch (sm_arb.state) {

    case  state_arbitration_grant_check: {
        uint8_t bus_lines ;
		// Put device requests onto QBUS while waiting for GRANTs

        // DMA: "A DMA Bus Master Device requests control of the bus by asserting TDMR."
        // IRQ: "A device asserts one or more of the IRQ4,IRQ5,IRQ6,IRQ7 lines".
        // Always update QBUS IRQ/DMR lines, are ORed with requests from other devices.

        if (sm_arb.cpu_bus_inhibit_dmr_mask)
			// Inhibit QBUS access of LSI11 CPU via un-ACKed dummy DMR
			bus_lines = sm_arb.device_request_mask | PRIORITY_ARBITRATION_BIT_NP;
		else 
	        bus_lines = sm_arb.device_request_mask ;
		
        // set BIRQ<4:7> depending on INTR level, reproduce DMR
        // INTR4 -> BIRQ4
        // INTR5 -> BIRQ5,4
        // INTR6 -> BIRQ6,5
        // INTR7 -> BIRQ7,6,4
        // use of const uint_t[] table in ROM difficult and not much faster.
        if (bus_lines & (PRIORITY_ARBITRATION_BIT_B5 | PRIORITY_ARBITRATION_BIT_B6 | PRIORITY_ARBITRATION_BIT_B7)) {
            bus_lines |= PRIORITY_ARBITRATION_BIT_B4 ;
            if (bus_lines & PRIORITY_ARBITRATION_BIT_B7)
                bus_lines |= PRIORITY_ARBITRATION_BIT_B6 ;
        }
        buslatches_setbits(6, PRIORITY_ARBITRATION_BIT_MASK, bus_lines);
        // buslatches_setbits(6, PRIORITY_ARBITRATION_BIT_MASK, sm_arb.device_request_mask);
        // now relevant for GRANT forwarding
        sm_arb.device_request_signalled_mask = sm_arb.device_request_mask;


        // IRQ: "The processor begins the interrupt service cycle by asserting TDIN.
        // The processor asserts TIAKO 325ns minimum after the assertion of TDIN"

        sm_arb.device_grant_mask = granted_requests_mask & sm_arb.device_request_mask
                                   & ~sm_arb.device_forwarded_grant_mask;
		// GRANT mask: only 1 bit set (single IOAKI, or DMG)
										   
        // IRQ: no SACK, but DIN set
        if (sm_arb.device_grant_mask & PRIORITY_ARBITRATION_INTR_MASK) {
            // "Each Bus Option which receives the assertion of RIAKI either
            // accepts it and becomes Bus Slave or passes it on to the next
            // Bus option as TIAKO. Traditionally, the propagation time from
            // BIAKI to BIAKO has been spec'd at 500 ns maximum, but 55 nsec
            // typical. ""
            sm_arb.state = state_arbitration_intr_vector ;
        } else if (sm_arb.device_grant_mask & PRIORITY_ARBITRATION_BIT_NP) {
            // DMA: "The Bus Arbitration logic the processor asserts TDMGO 0 nsec
            // minimum	after RDMR	asserts and  0	ns	minimum  after RSACK  negates"
            sm_arb.state = state_arbitration_dma_grant_rply_sync_wait ;
        }
        return 0 ; // wait, nothing yet granted
    }
    case state_arbitration_dma_grant_rply_sync_wait:
        // "3. The DMA Bus Master device asserts TSACK 0 ns minimum after the
        // assertion  of  RDMGI; 0 ns minimum after the negation of RSYNC;
        // and  0ns minimum after the  negation of RRPLY."
        if ( buslatches_getbyte(4) & (BIT(0) + BIT(3)))
            return 0 ; // wait for RPLY and SYNC to negate
        // 4. The DMA Bus Master device negates TDMR 0 ns minimum after the
        // assertion of TSACK.
        // set SACK AND simultaneously clear granted DMR. 6.7 = SACK
        buslatches_setbits(6, PRIORITY_ARBITRATION_BIT_NP | BIT(7), BIT(7));

        // clear granted requests internally
        sm_arb.device_request_mask &= ~PRIORITY_ARBITRATION_BIT_NP;
        // QBUS DATA section is indepedent: MSYN, SSYN, BBSY may still be active.
        // -> DMA and INTR statemachine must wait for BBSY.

        // Arbitrator should remove GRANT now. Data section on Bus still BBSY
        // "5. The Bus Arbitration logic clears TDMGO 0 ns minimum after the
        // assertion of TSACK. The bus arbitration logic must also
        // negate TDMGO if RDMR negates or if RSACK fails to assert
        // within 10 us (*No SACK* timeout).
        // 6. The DMA Bus Master device has control of the Bus, and may gate
        // TADDR onto the bus, when the conditions for asserting TSACK are met.
        // 7. The DMA Bus Master negates TSACK 0 ns minimum after negation
        // of the last RRPLY.
        // 8. The DMA Bus Master negates TSYNC 300 ns maximum after it
        // negates TSACK.
        // 9. The DMA Bus Master must remove TDATA, TBS7, TWTBT, and TREF
        // from the bus 100 ns maximum after clearing TSYNC.
        sm_arb.state = state_arbitration_grant_check ; // restart
        return PRIORITY_ARBITRATION_BIT_NP ; // that was granted and accepted

    case state_arbitration_intr_vector: {
        uint8_t intr_idx ; // 0..3 for INTR4..7
        uint16_t intr_vector ; // max 777
        // detected IAK<4:7> for own INTR request. TDIN already set!
        if (! (buslatches_getbyte(4) & BIT(1)))
            return 0 ; // DIN not yet set despite RPLY missing?
        // "5. The Bus Slave negates IRQ and asserts TRPLY 0 ns minimum after
        // the assertion of RIAKI. Note that the Bus Slave must assert
        // TRPLY 8000 ns maximum after RDIN to avoid a Bus Timeout."

        // one of our INTR requests was granted and not forwarded:
        // (device_grant_mask has only 1 bit set, CPLD2!)
        // clr granted INTR and set RPLY simultaneously
        buslatches_setbits(6, sm_arb.device_grant_mask, 0) ; // clr granted INTR
        // clear granted requests internally
        sm_arb.device_request_mask &= ~sm_arb.device_grant_mask;
        buslatches_setbits(4, BIT(3),BIT(3)) ; // assert RPLY

        // "6. The Bus Slave gates the Interrupt Vector (TVECT) address onto
        // the Bus 125 ns maximum after asserting TRPLY.
        // Because the vector is the first of a pair of addresses and
        // because vectors are constrained to addresses between 0 and
        // 777, only bits TDAL<08:02> are involved and no others should
        // be asserted."
        // now transfer INTR vector for interrupt of GRANTed level.
        // vector and ARM context have been setup by ARM before ARM2PRU_INTR already
        intr_idx = PRIORITY_ARBITRATION_INTR_BIT2IDX(sm_arb.device_grant_mask);
        intr_vector = mailbox.intr.vector[intr_idx];
        buslatches_setbyte(0, intr_vector & 0xff);					// DAL7..0
        buslatches_setbyte(1, (intr_vector >> 8) & 0xff);			// DAL15..8
		// DAL21 must be negated to indicate "no parity".
		// Implicitly true, as master removes address from DAL after SYNC, and we don't set DAL21 with other vlaues
		//		buslatches_setbits(2, 0x3f, 0) ;  // clr DAL21..16, keep BS7: "no parity support"
        
        sm_arb.intr_level_index = intr_idx; // to be returned to ARM on complete

        sm_arb.state = state_arbitration_intr_complete ; // wait for CPU to ack
        return 0 ; // wait, not yet complete
    }
    case state_arbitration_intr_complete: // wait for CPU to ack
        // "8. The Processor negates TDIN and TIAKO 200 ns minimum after the
        // assertion of RRPLY"
        // "9. The Bus Slave negates TRPLY 0 ns minimum after the negation of RIAKI."
        if (buslatches_getbyte(6) & BIT(5))
            return 0 ; // wait for RIAKI to negate
        buslatches_setbits(4, BIT(3), 0) ; // negate RPLY
        // "10. The Bus Slave continues to gate TVECT onto the Bus for 0 ns
        // minimum and 100 ns maximum after negating TRPLY."

        buslatches_setbyte(0, 0);					// DAL7..0
        buslatches_setbyte(1, 0);			// DAL15..8

        // no need to wait for negation of DIN?
        // if (buslatches_getbyte(4) & BIT(1))
        //	return 0 ; // wait for DIN to negate, "RPLY negate" repeats then

        // signal to ARM which INTR was completed
        // change mailbox only after ARM has ack'ed mailbox.events.event_intr
        // mailbox.events.intr_master.level_index = sm_intr_master.level_index;
        EVENT_SIGNAL(mailbox,intr_master[sm_arb.intr_level_index]);
        // ARM is clearing this, before requesting new interrupt of same level
        // so no concurrent ARP+PRU access
        PRU2ARM_INTERRUPT
        ;

        sm_arb.state = state_arbitration_grant_check ; // restart

		// signal which request where completed
        return sm_arb.device_request_signalled_mask ;




        /* worker_noop():
         * Static state to disable arbitration protocols. Make DMA possible in every bus configuration:
         * For diagnostics on hung CPU, active device or console processor holding SACK.
         * Ignores active SACK and/or SYNC/RPLY from other bus masters.
         */
    case state_arbitration_noop:
        // Unconditionally forward IAKI4..7 and DMGI to IAKO,DMGO
        buslatches_setbits(7, PRIORITY_ARBITRATION_BIT_MASK, granted_requests_mask);

        if (sm_arb.cpu_bus_inhibit_dmr_mask)
			// Inhibit QBUS access of LSI11 CPU (in ODT!) via un-ACKed dummy DMR
			buslatches_setbits(6, BIT(4), BIT(4)); // set DMR
		else 
			buslatches_setbits(6, BIT(4), 0); // clr DMR

	
        // ignore INTR requests, only ack DMA.
        if (sm_arb.device_request_mask & PRIORITY_ARBITRATION_BIT_NP) {
            sm_arb.device_request_mask &= ~PRIORITY_ARBITRATION_BIT_NP;
            return PRIORITY_ARBITRATION_BIT_NP;
        } else
            return 0;

    default:
        return 0; // must bever happen
    }
}



/*  "worker_master"
 Act as Arbitrator, Interrupt Fielding Processor and Client
 Is assumed to be on first slot, so BG*IN/NPGIN lines are ignored
 BR/NPR are set in device request, as in worker_client()

 Grant highest of requests, if SACK negated.
 Execute QBUS priority algorithm:
 - Grant DMA request, if present
 - GRANT IRQ* in descending priority, when CPU execution level allows .
 - Cancel GRANT, if no device responds with SACK within timeout period
 */
uint8_t sm_arb_worker_cpu() {
    /******* arbitrator logic *********/
    uint8_t intr_request_mask;
    uint8_t latch1val = buslatches_getbyte(1);
    bool do_intr_arbitration = mailbox.arbitrator.ifs_intr_arbitration_pending; // ARM allowed INTR arbitration

    // monitor BBSY
    if (latch1val & BIT(5)) {
        // SACK set by a device
        // priority arbitration disabled, remove GRANT.
        sm_arb.arbitrator_grant_mask = 0;

        // CPU looses now access to QBUS after current cycle
        // DATA section to be used by device now, for DMA or INTR

    } else if (latch1val & PRIORITY_ARBITRATION_BIT_NP) {
        // device NPR
        if (sm_arb.arbitrator_grant_mask == 0) {
            // no 2nd device's request may modify GRANT before 1st device acks with SACK
            sm_arb.arbitrator_grant_mask = PRIORITY_ARBITRATION_BIT_NP;
            TIMEOUT_SET(TIMEOUT_SACK, MILLISECS(ARB_MASTER_SACK_TIMOUT_MS));
        }
    } else if (do_intr_arbitration
               && (intr_request_mask = (latch1val & PRIORITY_ARBITRATION_INTR_MASK))) {
        // device BR4,BR5,BR6 or BR7
        if (sm_arb.arbitrator_grant_mask == 0) {
            // no 2nd device's request may modify GRANT before 1st device acks with SACK
            // GRANT request depending on CPU priority level
            // find level # of highest request in bitmask
            // lmbd() = LeftMostBitDetect(0x01)-> 0 (0x03) -> 1, (0x07) -> 2, 0x0f -> 3
            // BR4 = 0x01 -> 4, BR5 = 0x02 ->  5, etc.
            uint8_t requested_intr_level = __lmbd(intr_request_mask, 1) + 4;
            // compare against cpu run level 4..7
            // but do not GRANT anything if emulated CPU did not fetch new PSW yet,
            // then cpu_priority_level is invalid
            if (requested_intr_level > mailbox.arbitrator.ifs_priority_level //
                    && requested_intr_level != CPU_PRIORITY_LEVEL_FETCHING) {
                // GRANT request,  set GRANT line:
                // BG4 is signal bit mask 0x01, 0x02, etc ...
                sm_arb.arbitrator_grant_mask = BIT(requested_intr_level - 4);
                // 320 ns ???
                TIMEOUT_SET(TIMEOUT_SACK, MILLISECS(ARB_MASTER_SACK_TIMOUT_MS));
            }
        }
    } else {
        bool timeout_reached;
        TIMEOUT_REACHED(TIMEOUT_SACK, timeout_reached);
        if (sm_arb.arbitrator_grant_mask && timeout_reached) {
            // no SACK, no requests, but GRANTs: SACK timeout?
            sm_arb.arbitrator_grant_mask = 0;
        }
    }
    // put the single BR/NPR GRANT onto GRANT OUT BUS line, latches inverted.
    // visible for physical devices, not for emulated devices on this QBone
    buslatches_setbits(0, PRIORITY_ARBITRATION_BIT_MASK, ~sm_arb.arbitrator_grant_mask );

    // do not produce GRANTs until next ARM call of ARM2PRU_ARB_GRANT_INTR_REQUESTS
    mailbox.arbitrator.ifs_intr_arbitration_pending = false;

    return sm_arb.arbitrator_grant_mask;

}

