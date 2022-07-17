/* pru1_main.c: main loop with mailbox cmd interface. Test functions.

 Copyright (c) 2018-2019, Joerg Hoppe
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

 28-mar-2019  JH      split off from "all-function" main
 12-nov-2018  JH      entered beta phase

 Functions to test GPIOs, shared memory and mailbox.
 Separated from "all-function" main() because of PRU code size limits.
 Application has to load this into PRU1 depending on system state.

 from d:\RetroCmp\dec\pdp11\UniBone\91_3rd_party\pru-c-compile\pru-software-support-package\examples\am335x\PRU_gpioToggle
 Test GPIO, shared mem and interrupt
 a) waits until ARM writes a value to mailbox.arm2pru_req
 b) ACKs with clear of arm2pru_req
 c) toggles 1 mio times GPIO, with delay as set by ARM
 d) signal EVENT0
 e) goto a
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pru_cfg.h>
#include "resource_table_empty.h"

#include "pru1_utils.h"
#include "pru1_timeouts.h"

#include "pru_pru_mailbox.h"
#include "mailbox.h"
#include "ddrmem.h"
#include "iopageregister.h"

#include "pru1_buslatches.h"
#include "pru1_statemachine_arbitration.h"
#include "pru1_statemachine_dma.h"
//#include "pru1_statemachine_intr_master.h"
#include "pru1_statemachine_data_slave.h"

// Suppress warnings about using void * as function pointers
//		sm_slave_state = (statemachine_state_func)&sm_data_slave_start;
// while (sm_slave_state = sm_slave_state()) << usage
#pragma diag_push
#pragma diag_remark=515

void main(void) {

	/* Clear SYSCFG[STANDBY_INIT] to enable OCP master port */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	timeout_init();

	// clear all tables, as backup if ARM fails todo
	iopageregisters_init();

	buslatches_reset(); // all negated

	// init mailbox
	memset((void *) &mailbox, 0, sizeof(mailbox));

	while (1) {
//		timeout_test() ;

		// display opcode (active for one cycle
//		__R30 = (mailbox.arm2pru_req & 0xf) << 8;
		/*** Attention: arm2pru_req (and all mailbox vars) change at ANY TIME
		 * - ARM must set arm2pru_req as last operation on mailbox,
		 *    memory barrier needed.
		 * - ARM may not access mailbox until arm2pru_req is 0
		 * - PRU only clears arm2pru_req after actual processing if mailbox
		 * ***/
		switch (mailbox.arm2pru_req) {
		case ARM2PRU_NONE: // == 0
			// reloop
			break;
		case ARM2PRU_NOP: // needed to probe PRU run state
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		case ARM2PRU_HALT:
			__halt(); // that's it
			break;
#ifdef USED
			case ARM2PRU_MAILBOXTEST1:
			// simulate a register read access.
#ifdef TEST_TIMEOUT
			while (1) {
				timeout_test() ;
			}
#endif

			// show on REG_DATAOUT
			buslatches_pru0_dataout(mailbox.mailbox_test.addr);
			// pru_pru_mailbox.pru0_r30 = mailbox.mailbox_test.addr & 0xff;
			// __R30 = (mailbox.mailbox_test.addr & 0xf) << 8;
			mailbox.mailbox_test.val = mailbox.mailbox_test.addr;
			__R30 = (mailbox.arm2pru_req & 0xf) << 8;// optical ACK
			mailbox.arm2pru_req = ARM2PRU_NONE;// ACK: done
			break;
#endif
		case ARM2PRU_BUSLATCH_INIT: // set all mux registers to "neutral"
			buslatches_reset();
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;

		case ARM2PRU_BUSLATCH_SET: { // set a mux register
			// don't feed "volatile" vars into buslatch_macros !!!
			uint8_t reg_sel = mailbox.buslatch.addr & 7;
			uint8_t bitmask = mailbox.buslatch.bitmask;
			uint8_t val = mailbox.buslatch.val;
			if (BUSLATCHES_REG_IS_BYTE(reg_sel))
				buslatches_setbyte(reg_sel, val);
			else
				buslatches_setbits(reg_sel, bitmask, val);
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		}
		case ARM2PRU_BUSLATCH_GET: {
			// don't feed "volatile" vars into buslatch_macros !!!
			uint8_t reg_sel = mailbox.buslatch.addr & 7;
			// buslatches.cur_reg_sel = 0xff; // force new setting of reg_sel
			mailbox.buslatch.val = buslatches_getbyte(reg_sel);
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		}
		case ARM2PRU_BUSLATCH_EXERCISER: 	// exercise 8 byte accesses to mux registers
			buslatches_exerciser();
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;

		case ARM2PRU_BUSLATCH_TEST: {
			buslatches_test(mailbox.buslatch_test.addr_0_7, mailbox.buslatch_test.addr_8_15,
					mailbox.buslatch_test.data_0_7, mailbox.buslatch_test.data_8_15);
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		}
		case ARM2PRU_INITALIZATIONSIGNAL_SET:
			switch (mailbox.initializationsignal.id) {
			case INITIALIZATIONSIGNAL_POK:
				// assert/negate POK
				buslatches_setbits(6, INITIALIZATIONSIGNAL_POK, mailbox.initializationsignal.val? INITIALIZATIONSIGNAL_POK:0);
				break;
			case INITIALIZATIONSIGNAL_DCOK:
				// assert/negate DCLO
				buslatches_setbits(6, INITIALIZATIONSIGNAL_DCOK, mailbox.initializationsignal.val? INITIALIZATIONSIGNAL_DCOK:0);
				break;
			case INITIALIZATIONSIGNAL_INIT:
				// assert/negate INIT
				buslatches_setbits(6, INITIALIZATIONSIGNAL_INIT, mailbox.initializationsignal.val? INITIALIZATIONSIGNAL_INIT:0);
				break;
			}
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		case ARM2PRU_DMA: {
			// without NPR/NPG arbitration
			statemachine_state_func sm_dma_state = (statemachine_state_func) &sm_dma_start;
			// simply call current state function, until stopped
			// parallel the BUS-slave statemachine is triggered
			// by master logic.
			while (sm_dma_state = sm_dma_state())
				;
		}
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		case ARM2PRU_DDR_FILL_PATTERN:
			ddrmem_fill_pattern();
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		case ARM2PRU_DDR_SLAVE_MEMORY:
			// respond to QBUS cycles as slave and
			// access DDR as QBUS memory.

			// only debugging: all signals negated
			buslatches_reset();

			// do QBUS slave cycles, until ARM abort this by
			// writing into mailbox.arm2pru_req
			while (mailbox.arm2pru_req == ARM2PRU_DDR_SLAVE_MEMORY) {
				statemachine_data_slave_t sm_slave ;
				sm_slave.state = state_data_slave_stop ;
				// do all states of an access, start when MSYN found.
				while (sm_slave.state = sm_data_slave_func(sm_slave.state))
					;
			}
			mailbox.arm2pru_req = ARM2PRU_NONE; // ACK: done
			break;
		} // switch
	} // while
}
#pragma diag_pop

