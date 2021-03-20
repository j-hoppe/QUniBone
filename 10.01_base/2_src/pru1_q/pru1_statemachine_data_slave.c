

/* pru1_statemachine_data_slave.c: state machine for execution of slave DATO* or DATI* cycles

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


27-aug-2020    JH      QBUS variant
29-jun-2019    JH      rework: state returns ptr to next state func
12-nov-2018    JH  entered beta phase

Statemachine for execution of slave DATO* or DATI* cycles.
All references "PDP11BUS handbook 1979"

*/
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "pru1_utils.h"
#include "mailbox.h"
#include "ddrmem.h"
#include "iopageregister.h"
#include "pru1_buslatches.h"
#include "pru1_statemachine_data_slave.h"
// static variables
statemachine_data_slave_t sm_data_slave;

// returns next state
// big switch(), 5ns per case
enum states_data_slave_enum  sm_data_slave_func(enum states_data_slave_enum  /**/ state) {
    while(1) {
        // normally process states in high speed loop.
        // some long states return so main() can process other SMs
        // MUST return to main() if !EVENT_IS_ACKED, so PRU checks for AMR2PPRU opcodes on devcie register access!
        switch(state) {
        case state_data_slave_stop:
        case state_data_slave_start:
        {
            uint8_t latch0val, latch1val, latch2val ;
            uint32_t addr;
            latch2val = buslatches_getbyte(2);
            //	  latch3val = buslatches_getbyte(3); // SYNC latched signals
            if (! (latch2val & BIT(7)))
                return state_data_slave_stop; // no SYNC, or refresh cycle
            // SYNC: DAL is latched
            latch0val = buslatches_getbyte(0);
            latch1val = buslatches_getbyte(1);

            // decode addresses from DAL and BS7, always 22+1bit
            // encode BS7 as high addr bit #22. on 16 and 18 bit systems, pullup must set unused ADDRs to 1 = negated
            addr = latch0val | ((uint32_t) latch1val << 8) | ((uint32_t) (latch2val & 0x7f) << 16);
            sm_data_slave.addr = addr; // save

            //	  if (!(latch2val & BIT(6))) // pulse when not BS7
            //		  PRU_DEBUG_PIN0(1);

            // DO NOT EVALUATED WTBT for early DIN/DOUT decision ... perhaps later optimization
            state = state_data_slave_dindout_start; //
            break ;
        }
        case state_data_slave_dindout_start:
            // wait for DIN, then return prefetched memory value,
            // or start ioregister event to ARM
        {
            uint8_t latch4val;

            latch4val = buslatches_getbyte(4);
            if (((latch4val & BIT(0)) == 0) || (latch4val & BIT(3)) != 0 || (latch4val & BIT(7)) != 0) {
                // ! SYNC || RPLY || INIT
                // KD11F M8186 holds SYNC asserted in DCOK-INIT -> deadlock on PRU-power cycle
                return state_data_slave_stop; // master aborted, or other device accepted (missing SYNC cycle?)
            }

            if (latch4val & BIT(1)) { // DIN?
                // fast return of present value, then long delay
                // until ARM is ready when event acked
                uint16_t val;
                uint8_t emulated_addr_res = emulated_addr_read(sm_data_slave.addr, &val) ;

                if (!emulated_addr_res) { // read val from iopage register
                    return state_data_slave_stop; // non existing address
                }
                //		  PRU_DEBUG_PIN0(1);

                buslatches_setbyte(0, val & 0xff);					// DAL7..0
                buslatches_setbyte(1, (val >> 8) & 0xff);			// DAL15..8
                // DAL21 must be negated to indicate "no parity".
                // Implicitely true, as master removes address from DAL after SYNC, and we don't set DAL21 with other vlaues
				
                // "setbyte(2,0) only once, see above
                buslatches_setbits(4, BIT(3)+BIT(6), 0xff); 			 // REF=1: more DIN allowed, RPLY

                // RPLY asserted while ARM processes iopage register event, else timeout
                // from now on, "Abort" must clear REF: 		buslatches_setbits(4, BIT(6), 0) ; // clear REF: allow Block mode
                if (emulated_addr_res == 1)
                    state = state_data_slave_din_single_complete ; // mem access: no need to wait for ARM EVENT_ACK
                else
                    return state_data_slave_din_single_complete; // main(): check for ARM EVENT_ACK
            } else if (latch4val & BIT(2)) {  // DOUT ?
                uint32_t addr = sm_data_slave.addr;
                uint8_t emulated_addr_res ;
                // access to DAL in latches[0,1,2] transparent
                buslatches_setbyte(3, BIT(0)) ; // unlatch DAL
                if (latch4val & BIT(4)) { // WTBT? byte access!
                    uint8_t b;

                    if (addr & 1) { // DAL[8..15] = latch[1]
                        b = buslatches_getbyte(1);
                    } else { // DAL[0..7] = latch[0]
                        b = buslatches_getbyte(0);
                    }
                    emulated_addr_res = emulated_addr_write_b(addr, b);
                } else {   // word access
                    uint16_t w = buslatches_getbyte(0) ; // DAL7..0
                    // dom't use single combined "|"-term ... trash result?!
                    w |= (uint16_t) buslatches_getbyte(1) << 8; // DAL15..8
                    emulated_addr_res = emulated_addr_write_w(addr, w);			// ARM ack of even may take long!
                }

                if (!emulated_addr_res)
                    return state_data_slave_stop; // non existing address

                buslatches_setbits(4, BIT(3)+BIT(6), 0xff); 			 // REF=1: more DIN allowed. RPLY=1

                // RPLY asserted while ARM processes iopage register event, else timeout
                // from now on, "Abort" must clear REF: 		buslatches_setbits(4, BIT(6), 0) ; // clear REF: allow Block mode
                if (emulated_addr_res == 1)
                    state = state_data_slave_dout_single_complete ; // mem access: no need to wait for ARM EVENT_ACK
                else
                    return state_data_slave_dout_single_complete; // main(): check for ARM EVENT_ACK
            }
            else {
                // wait for DIN or DOUT
                state = state_data_slave_dindout_start;
            }
        }
        break ;
        case state_data_slave_din_single_complete:
        {
            uint8_t latch4val = buslatches_getbyte(4) ;

            /* done in din_end
            if (! (latch4val & BIT(0)) || (latch4val & BIT(7))) { // !SYNC || INIT?
            	buslatches_setbits(4, BIT(3)+BIT(6), 0);		   // RPLY=REF=0, cleanup
            	return NULL; // master aborted cycle?
            }
            */

            // wait for master to negate DIN or DOUT
            if (latch4val  & BIT(1)) { // DIN?
                state = state_data_slave_din_single_complete; // wait for master to negate DIN
            } else if (!EVENT_IS_ACKED(mailbox, deviceregister)) {
				return state_data_slave_din_single_complete; //  main(): check for ARM EVENT_ACK			 
//                state = state_data_slave_din_single_complete; // wait for ARM to process device register access
            } else {
                buslatches_setbits(4, BIT(3)+BIT(6), 0); // RPLY=0, ARM-elongated cycle finished.  REF=0, cleanup
                // "The Bus slave continues to gate TDATA onto the Bus for 0 ns
                // minimum and 100 ns maximum after negating TRPLY"
                buslatches_setbyte(3, BIT(1));	// "clr DAL", remove data from DAL lines

                state = state_data_slave_din_block_complete;
            }
        }
        break ;
        case state_data_slave_dout_single_complete:
        {
            uint8_t latch4val = buslatches_getbyte(4) ;
            /* done in dout_end
            if (! (latch4val & BIT(0)) || (latch4val & BIT(7))) { // !SYNC || INIT?
            	buslatches_setbits(4, BIT(6), 0);			// REF=0, cleanup
            	return NULL; // master aborted cycle?
            }
            */

            if (latch4val & BIT(2)) { // DOUT?
                state = state_data_slave_dout_single_complete; // wait for master to negate DOUT
            } else if (!EVENT_IS_ACKED(mailbox, deviceregister)) {
  				  return state_data_slave_dout_single_complete; //  main(): check for ARM EVENT_ACK            
//                state = state_data_slave_dout_single_complete; // wait for ARM to process device register access
            } else {
                buslatches_setbits(4, BIT(3)+BIT(6), 0); // RPLY=0, ARM-elongated cycle finished.  REF=0, cleanup
                state = state_data_slave_dout_block_complete;
            }
        }
        break ;
        case state_data_slave_din_block_complete:
            // another cycle after DIN+RPLY?
        {
            uint8_t latch4val = buslatches_getbyte(4) ;

            // data portion ready, DIN/DOUT/RPLY negated
            // another data part?
            if (! (latch4val & BIT(0)) || (latch4val & BIT(7))) { // !SYNC || INIT?
                return state_data_slave_stop; // ready, accept next address with next SYNC
            }

            // DATIO: DOUT following DATIN?
            // next is DOUT to same address
            if (latch4val & BIT(2)) { // DOUT?
                state = state_data_slave_dindout_start; // process DOUT
            } else if (latch4val & BIT(1)) { // DIN?
                // next DIN from next address
                sm_data_slave.addr += 2;
                state = state_data_slave_dindout_start; // process DIN
            }
            else
                state = state_data_slave_din_block_complete; // wait for SYNC,DIN or DOUT
        }
        break ;

        case state_data_slave_dout_block_complete:
            // another cycle after DOUT+RPLY?
        {
            uint8_t latch4val = buslatches_getbyte(4) ;

            // data portion ready, DIN/DOUT/RPLY negated
            // another data part?
            if (! (latch4val & BIT(0)) || (latch4val & BIT(7))) { // !SYNC || INIT?
                return state_data_slave_stop; // ready, accept next address with next SYNC
            }

            if (latch4val & BIT(2)) { // DOUT?
                // process another DOUT
                sm_data_slave.addr += 2;
                state = state_data_slave_dindout_start;
            } else
                state = state_data_slave_dout_block_complete; // wait for SYNC or DOUT
        }
        break ;
        } // switch(state)
    }
}









