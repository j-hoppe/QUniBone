/* buslatches_q.cpp: PRU GPIO multiplier latches, functions common for QBone and UniBone.

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


 16-jul-2020  JH      refactored from gpio.hpp
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "mailbox.h"

#include "utils.hpp"
#include "gpios.hpp"
#include "pru.hpp"

#include "buslatches.hpp"

// search a register bit by QUNIBUS signal name and direction
buslatches_wire_info_t *buslatches_wire_info_get(const char *unibus_name, unsigned is_input) {
	unsigned i;
	buslatches_wire_info_t *wi;

	for (i = 0; (wi = &buslatches_wire_info[i]) && wi->path; i++)
		if (wi->is_input == is_input && !strcasecmp(wi->qunibus_name, unibus_name))
			return wi;
	return NULL; // not found
}

// print info for a loop back mismatch bitmask
static void buslatches_wire_info_print_path(buslatch_c *bl, unsigned mismatch_bitmask) {
	unsigned bit;
	unsigned bitmask;
	unsigned i;
	buslatches_wire_info_t *wi;

	for (bit = 0; bit < 8; bit++) {
		bitmask = 1 << bit;
		if (mismatch_bitmask & bitmask) {
			// search for write path

			printf("Signal path for bus latch %u, bit %u (mask 0x%02x):\n", bl->addr, bit,
					(1 << bit));
			for (i = 0; (wi = &buslatches_wire_info[i]) && wi->path; i++)
				if (wi->reg_sel == bl->addr && !wi->is_input && wi->bit_nr == bit)
					printf("  Write: %s\n", wi->path);
			for (i = 0; (wi = &buslatches_wire_info[i]) && wi->path; i++)
				if (wi->reg_sel == bl->addr && wi->is_input && wi->bit_nr == bit)
					printf("  Read : %s\n", wi->path);
		}
	}
}
// enable=1: activate QBUS/UNIBUS drivers
// activate AFTER RPU code started and reset bus latches values
void buslatches_c::output_enable(bool enable) {
	enable = !!enable;
	GPIO_SETVAL(gpios->bus_enable, enable);

	// tie LED to driver enable via software
	if (gpios->qunibus_led) 
		GPIO_SETVAL(gpios->qunibus_led, !enable);
		
	cur_output_enable = enable;
}

// QBUS: all H / BOK,DCOK in CPLD inverted.
// PRU1 does it
void buslatches_c::pru_reset() {
	assert(pru->prucode_id == pru_c::PRUCODE_TEST);
	mailbox_execute(ARM2PRU_BUSLATCH_INIT);
}

// read the REG_DATIN[0..7] pins
unsigned buslatch_c::getval() {
// PRU1 does it
	mailbox->buslatch.addr = addr;
	while (mailbox->buslatch.addr != addr)
		; // cache !

	mailbox_execute(ARM2PRU_BUSLATCH_GET);

	return mailbox->buslatch.val; // PRU1 has put the result here
}

// write the REG_DATOUT[0..7] pins into one latch
// only bits "valmask" are written. Other bits are cleared (PRU logic)
void buslatch_c::setval(unsigned valmask, unsigned val) {
	// write value into latch
	mailbox->buslatch.addr = addr;
	mailbox->buslatch.bitmask = valmask & 0xff;
	mailbox->buslatch.val = val;
	mailbox_execute(ARM2PRU_BUSLATCH_SET);
}

bool buslatches_c::get_pin_val(buslatches_wire_info_t *wi) {
	// read register
	assert(wi->is_input); // only input signals
	buslatch_c *bl = at(wi->reg_sel);
	unsigned val = bl->getval();
	// mask out register bit
	if (val & (1 << wi->bit_nr))
		return 1;
	else
		return 0;
}

// write single signal wire
void buslatches_c::set_pin_val(buslatches_wire_info_t *wi, unsigned val) {
	// set single bit
	assert(!wi->is_input); // only output signals
	buslatch_c *bl = at(wi->reg_sel);
	if (val)
		bl->outreg_val |= (1 << wi->bit_nr);
	else
		bl->outreg_val &= ~(1 << wi->bit_nr);
	bl->setval(0xff, bl->outreg_val);
}

// some pattern tests on a register latch
// register is set, drives QBUS/UNIBUS lines, and is
// immediately read back and compared.
// pattern:
// 1: count upwards
// 2: moving one
// 3: moving zero
// 4: toggle between 0x00 and 0xff
// 5: random values
// read back and compare values
// stop with ^C
// DOES TEST MUXED ADDR
void buslatches_c::test_simple_pattern(unsigned pattern, buslatch_c *bl) {
	unsigned idx, setval = 0, chkval;
	unsigned count;

	switch (pattern) {
	case 1:
		printf("Highspeed count register latch %d, stop with ^C.\n", bl->addr);
		break;
	case 2:
		printf("Highspeed \"moving ones\" in register latch %d, stop with ^C.\n", bl->addr);
		break;
	case 3:
		printf("Highspeed \"moving zeros\" in register latch %d, stop with ^C.\n", bl->addr);
		break;
	case 4:
		printf("Highspeed toggle 0x00 - 0xff in register latch %d, stop with ^C.\n", bl->addr);
		break;
	case 5:
		printf("Highspeed random values in register latch %d, stop with ^C.\n", bl->addr);
		break;
	}

// Setup ^C catcher
	SIGINTcatchnext();
// high speed loop
	idx = 0;
	count = 0;
	while (!SIGINTreceived) {
		/* 1. generate pattern */
		switch (pattern) {
		case 1: // count upwards
			setval = idx;
			idx = (idx + 1) & bl->bitmask; // period = bitwidth
			break;
		case 2: // moving ones
			setval = (1 << idx);
			do {
				idx = (idx+1) & 7 ; // cycle to next settable bit
			} while (((1 << idx) & bl->bitmask) == 0) ;
			//		idx = (idx + 1) % bl->bitwidth; // period = bitwidth
			break;
		case 3: // moving zeros
			setval = ~(1 << idx);
			do {
				idx = (idx+1) & 7 ; // cycle to next settable bit
			} while (((1 << idx) & bl->bitmask) == 0) ;
			break;
		case 4: // toggle 0x00 0xff
			setval = idx & 1 ? 0x00 : 0xff; // period = 2
			idx = !idx;
			break;
		case 5:
			setval = rand() & 0xff; // slow?
			break;
		}

		/* 2. write pattern into output latches. */
		setval &= bl->rw_bitmask; // mask out untestable bits
		bl->setval(0xff, setval);

		/* 3. read back pattern in output latches over QUNIBUS into input muxes */
		chkval = bl->getval();
		if (bl->read_inverted)
			chkval = ~chkval; // input latches invert
		chkval &= bl->rw_bitmask;
		if (chkval != setval) {
			printf("pass %u test_register_simple_pattern(%d, %d): wrote 0x%x, read 0x%x\n",
					count, pattern, bl->addr, setval, chkval);
			if (bl->addr == 6) {
				printf("Testing IAK and DMG GRANT forward signals.\n");
				printf("Are there 2*3 loopback jumpers in the \"||\"\n");
				printf("                                      \"--\" position?\n");
			}
			buslatches_wire_info_print_path(bl, setval ^ chkval);
			return;
		}
		count++;
	}
	printf("\n%u tests successful.\n", count);
}

// shuffles entries in mailbox.exerciser work list
void buslatches_c::exerciser_random_order() {
	for (unsigned i = 0; i < 2 * BUSLATCHES_COUNT; i++) {
		unsigned reg_sel1 = rand() % BUSLATCHES_COUNT;
		unsigned reg_sel2 = rand() % BUSLATCHES_COUNT;
		// swap addr and testval
		std::swap(mailbox->buslatch_exerciser.addr[reg_sel1], mailbox->buslatch_exerciser.addr[reg_sel2]) ;
		std::swap(mailbox->buslatch_exerciser.writeval[reg_sel1], mailbox->buslatch_exerciser.writeval[reg_sel2]) ;
		std::swap(mailbox->buslatch_exerciser.readval[reg_sel1], mailbox->buslatch_exerciser.readval[reg_sel2]) ;
	}
}

// Test several pattern high speed on the PRU.
// always reg0..7 are read&written, but muxed ADDR in 3,4,5 are ignored
// because of "rw_bitmask" == 0
// DOES NOT TEST MUXED ADDR
void buslatches_c::test_simple_pattern_multi(unsigned pattern, bool stop_on_error) {
	unsigned pass_no; // global test number counter
	uint64_t total_errors, total_tests;
	unsigned reg_sel; // register address
	unsigned testval[BUSLATCHES_COUNT]; // test data for all latches
	const char * stop_phrase = stop_on_error? "stops on error or by ^C":"stop with ^C" ;
#if defined(UNIBUS)
#define QUPHRASE ""
#else
#define QUPHRASE " (including demuxed ADDR)"
#endif
	switch (pattern) {
//	case 1:
//		printf("Highspeed count register latch %d, stop with ^C.\n", reg_sel);
//		break;
	case 2:
		printf(
				"Highspeed \"moving ones\" in register latches" QUPHRASE ", %s.\n", stop_phrase);
		break;
	case 3:
		printf(
				"Highspeed \"moving zeros\" in register latches" QUPHRASE ", %s.\n", stop_phrase);
		break;
	case 4:
		printf(
				"Highspeed toggle 0x00 - 0xff in register latches" QUPHRASE ", %s.\n", stop_phrase);
		break;
	case 5:
		printf(
				"Highspeed random values in register latches" QUPHRASE ", %s.\n", stop_phrase);
		break;
	default:
		printf("Error: unknown test pattern %u.\n", pattern);
	}

	pass_no = 0;
	total_errors = 0;
	total_tests = 0;

	// Setup ^C catcher
	SIGINTcatchnext();
	// high speed loop
	while ((!stop_on_error || (total_errors == 0)) && !SIGINTreceived) {
		// 1 cycle = 8 bits of 8 registers
		// some tests are no-op because of reduced bitwidth

		// UNIBUS: all PRU latches can be tested independently of each other
		// QBUS: ADDR latches 3,4,5 are SYNC-cpatured DAL lines.
		// Setting 3,4,5 needs 0,1,2 to be changed temporarily
		// PRU buslatches_setbits_mux_helper() handles this transparently

		/* 1. generate pattern. Output: testval[reg_addr] */
		switch (pattern) {
		case 2: // moving ones, linear addressing
			for (reg_sel = 0; reg_sel < BUSLATCHES_COUNT; reg_sel++) {
				unsigned bitidx = pass_no % 8; // circle all 8 bits per register
				unsigned regidx = (pass_no / 8) % BUSLATCHES_COUNT; // circle all registers
				// set only one bit
				if (reg_sel == regidx)
					testval[reg_sel] = 1 << bitidx;
				else
					testval[reg_sel] = 0;
			}
			break;
		case 3: // moving zeros, linear addressing
			for (reg_sel = 0; reg_sel < BUSLATCHES_COUNT; reg_sel++) {
				// clear only one bit
				unsigned bitidx = pass_no % 8; // circle all 8 bits per register
				unsigned regidx = (pass_no / 8) % BUSLATCHES_COUNT; // circle all registers
				if (reg_sel == regidx)
					testval[reg_sel] = ~(1 << bitidx);
				else
					testval[reg_sel] = 0xff;
			}
			break;
		case 4: // toggle all regs simultaneously 0x00, 0xff, 0xff, ...
			// linear addressing
			for (reg_sel = 0; reg_sel < BUSLATCHES_COUNT; reg_sel++) {
				if (pass_no & 1)
					testval[reg_sel] = 0xff;
				else
					testval[reg_sel] = 0x00;
			}
			break;
		case 5:
			// random values, random addressing
			for (reg_sel = 0; reg_sel < BUSLATCHES_COUNT; reg_sel++)
				testval[reg_sel] = rand() & 0xff; // slow?
			break;
		default:
			printf("Error: unknown test pattern %u.\n", pattern);
		}

		// mask out un-testable bits
		for (reg_sel = 0; reg_sel < BUSLATCHES_COUNT; reg_sel++)
			testval[reg_sel] &= buslatches[reg_sel]->rw_bitmask;

		// Setup mailbox for PRU buslatch exerciser
		// it tests always 8 accesses
		for (reg_sel = 0; reg_sel < BUSLATCHES_COUNT; reg_sel++) {
			mailbox->buslatch_exerciser.addr[reg_sel] = reg_sel;
			mailbox->buslatch_exerciser.writeval[reg_sel] = testval[reg_sel];
			mailbox->buslatch_exerciser.readval[reg_sel] = 0xff; // invalid at the moment
		}

		// shuffle worklist to create random access order
		buslatches.exerciser_random_order();

		// alternatingly use byte or bit access procedures
		mailbox->buslatch_exerciser.pattern = (pass_no
				% MAILBOX_BUSLATCH_EXERCISER_PATTERN_COUNT);

		mailbox_execute(ARM2PRU_BUSLATCH_EXERCISER);

		// check: mailbox readvalues == write values ?
		for (unsigned i = 0; i < BUSLATCHES_COUNT; i++) {
			reg_sel = mailbox->buslatch_exerciser.addr[i];
			buslatch_c *bl = buslatches[reg_sel];
			unsigned writeval = mailbox->buslatch_exerciser.writeval[i];
			unsigned readval = mailbox->buslatch_exerciser.readval[i];
			total_tests++;
//			assert (!bl->read_inverted ||reg_sel==0) ;
			if (bl->read_inverted)
				readval = ~readval; // input latches invert
			readval &= bl->rw_bitmask; // mask out untestable bits
			if (readval != writeval) {
				total_errors++;
				printf(
						"Error buslatches_test_simple_pattern_multi(pattern=%d), pass %u, PRU exerciser pattern=%d:\n",
						pattern, pass_no, (unsigned) mailbox->buslatch_exerciser.pattern);
				printf("  register %u: wrote 0x%02x, read back 0x%02x, error bit mask 0x%02x\n",
						reg_sel, writeval, readval, writeval ^ readval);
				if (i == 0)
					printf("  No prev addr/val history\n");
				else {
					// printout previous test data. for access pattern see "pattern" and sourcecode
					printf("  Prev addr/val history:");
					for (unsigned j = 0; j < i; j++)
						printf(" %u/0x%02x", mailbox->buslatch_exerciser.addr[j],
								mailbox->buslatch_exerciser.writeval[j]);
					printf(".\n");
				}
#if defined(UNIBUS)			
							if (reg_sel == 0) {
								printf("Testing BR*,NPR with BG*,NPG feedback.\n");
								printf("Are the 5*3 terminator/loopback jumpers set?\n");
							}

#elif defined(QBUS)				
				if (reg_sel == 6) {
					printf("Testing IAK and DMG GRANT forward signals.\n");
					printf("Are the 2*3 terminator/loopback jumpers set?\n");
				}
#endif				
				buslatches_wire_info_print_path(bl, writeval ^ readval);
				printf("%llu of %llu tests failed, error rate = %0.5f%% = %gppm)\n\n",
						total_errors, total_tests, 100.0 * total_errors / total_tests,
						1000000.0 * total_errors / total_tests);
			}
		}

		pass_no++;
	}

	if (total_errors == 0)
		printf("\n%llu tests successful.\n", total_tests);
	else
		printf("\n%llu of %llu tests failed, error rate = %0.5f%% = %gppm)\n", total_errors,
				total_tests, 100.0 * total_errors / total_tests,
				1000000.0 * total_errors / total_tests);
}

/* stress test on highspeed timing
 * PRU generates max speed read/write sequences on
 // ADDR<0:7>, ADDR <8:15>, DATA<0:7> and <DATA8:15>
 */
void buslatches_c::test_timing(uint8_t addr_0_7, uint8_t addr_8_15, uint8_t data_0_7,
		uint8_t data_8_15) {
	timeout_c timeout;
	printf("PRU generates max speed read/write sequences on 4 full 8bit\n");
	printf("latches with these start patterns:\n");
	printf(
			"ADDR<0:7> = 0x%02x, ADDR<8:15> = 0x%02x, DATA<0:7> = 0x%02x, <DATA8:15> = 0x%02x.\n",
			addr_0_7, addr_8_15, data_0_7, data_8_15);
	printf("Read/write mismatches are signaled with PRU1.12 == 1.\n");
	printf("Connect logic analyzer probes to: \n");
	printf("  REG_SEL, REG_WRITE, REG_DATIN, REG_DATOUT, PRU1.12 .\n");
	printf("End with ^C.\n");

	mailbox->buslatch_test.addr_0_7 = addr_0_7;
	mailbox->buslatch_test.addr_8_15 = addr_8_15;
	mailbox->buslatch_test.data_0_7 = data_0_7;
	mailbox->buslatch_test.data_8_15 = data_8_15;

// Setup ^C catcher
	SIGINTcatchnext();

	mailbox->arm2pru_req = ARM2PRU_BUSLATCH_TEST; // start PRU test loop

	while (!SIGINTreceived) {
		timeout.wait_ms(0);
	}
// stop PRU loop by setting something != ARM2PRU_BUSLATCH_TEST
	mailbox->arm2pru_req = ARM2PRU_BUSLATCH_INIT; //
	timeout.wait_ms(1);
	if (mailbox->arm2pru_req != ARM2PRU_NONE)
		printf("Stopping PRU test loop failed!\n");
	else
		printf("PRU test loop stopped.\n");
}

