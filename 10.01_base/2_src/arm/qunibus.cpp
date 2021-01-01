/* qunibus.cpp: utilities to handle QBUS/UNIBUS functions

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

 jul-2019     JH      rewrite: multiple parallel arbitration levels
 12-nov-2018  JH      entered beta phase
 */

#define _QUNIBUS_CPP_

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>

#include "pru.hpp"
#include "logger.hpp"
#include "gpios.hpp"
#include "bitcalc.h"
#include "memoryimage.hpp"
#include "mailbox.h" // for test of PRU code
#include "utils.hpp" // for test of PRU code
#include "qunibusadapter.hpp" // DMA, INTR

#include "qunibus.h"

/* Singleton */
qunibus_c *qunibus;

qunibus_c::qunibus_c() {
	log_label = "QUNIBUS";
	addr_width = 0; // has to be set by user with set_addr_width()
	addr_space_word_count = 0;
	addr_space_byte_count = 0;
	iopage_start_addr = 0;
#if defined(UNIBUS)
	set_addr_width(18); // const
#endif

	dma_request = new dma_request_c(NULL);
	// priority backplane slot # for helper DMA not important, as typically used stand-alone
	// (no other devioces on the backplane active, except perhaps "testcontroller")
	dma_request->set_priority_slot(16);
}

qunibus_c::~qunibus_c() {
	delete dma_request;
}

// recalc memory and iopage limtis
void qunibus_c::set_addr_width(unsigned addr_width) {
	switch (addr_width) {
	case 18:
		addr_space_word_count = 0x20000; // 128 KWord = 256 KByte
		iopage_start_addr = 0760000;
		break;
#if defined(QBUS) 	// UNIBUS allows only 18 bit
		case 16:
		addr_space_word_count = 0x8000; // 32 KWord = 64 KByte
		iopage_start_addr = 0160000;
		break;
		case 22:
		addr_space_word_count = 0x200000;// 2 MWord = 4 MByte
		iopage_start_addr = 017760000;
		break;
#endif		
	default:
		FATAL("Address width of %d bits invalid!", addr_width);
	}
	this->addr_width = addr_width;
	addr_space_byte_count = 2 * addr_space_word_count;
}

// verify user selected address width, 
// address width is determined by PDP-11 CPU and cannot be guessed.
// Example: a 16 bit LSI operates in an 18 bit backplane,
// then QBOne must generate BS7 for addresses >= 160000
// but  addresses 0.. 777776 are valid.
void qunibus_c::assert_addr_width(void) {
#if defined(QBUS)
	if (!addr_width) {
		FATAL("Select address width of CPU via global parameter\n(command line -aw 16/28/22)") ;
	}
#endif	
}


/* return a 16 bit result, or TIMEOUT
 * result: 0 = timeout, else OK
 */
char *qunibus_c::data2text(unsigned val) {
	char *buffer = rolling_text_buffers.get_next();
	if (val <= 0177777)
		sprintf(buffer, "%06o", val);
	else
		strcpy(buffer, "??????");
	return buffer;
}


/* return UNOBUS control as text: "DATI", DATO", ....
 */
char *qunibus_c::control2text(uint8_t control) {
	char *buffer = rolling_text_buffers.get_next();
	switch (control) {
	case QUNIBUS_CYCLE_DATI:
		strcpy(buffer, "DATI");
		break;
	case QUNIBUS_CYCLE_DATIP:
		strcpy(buffer, "DATIP");
		break;
	case QUNIBUS_CYCLE_DATO:
		strcpy(buffer, "DATO");
		break;
	case QUNIBUS_CYCLE_DATOB:
		strcpy(buffer, "DATOB");
		break;
	default:
		strcpy(buffer, "???");
	}
	return buffer;
}

// multiple static buffers: many calls allowed per printf() !
char *qunibus_c::addr2text(unsigned addr) {
	const char *iopagestr ;
	char *buffer = rolling_text_buffers.get_next();
	if ((addr & ~QUNIBUS_IOPAGE_ADDR_BITMASK)  >= iopage_start_addr)
		iopagestr = "io";
	else
		iopagestr = "";
	switch (addr_width) {
	case 16:
		sprintf(buffer, "%s%06o", iopagestr, addr & 0177777);
		break;
	case 18:
		sprintf(buffer, "%s%06o", iopagestr, addr & 0777777);
		break;
	case 22:
		sprintf(buffer, "%s%08o", iopagestr, addr & 017777777);
		break;
	default:
		FATAL("Address width of %d bits invalid!", addr_width);
	}
	return buffer;
}



// octal, or '<char>'
bool qunibus_c::parse_word(char *txt, uint16_t *word) {
	*word = 0;
	if (!txt || *txt == 0)
		return false;

	if (*txt == '\'') {
		txt++;
		if (*txt)
			*word = *txt; // ASCII code of first char after ''
	} else
		*word = strtol(txt, NULL, 8); // octal literal
	return true;
}

// octal, trunc to 18 bit
bool qunibus_c::parse_addr(char *txt, uint32_t *addr) {
	unsigned maxval = 0 ;
	*addr = strtol(txt, NULL, 8);
	switch (addr_width) {
	case 16:
		maxval = 0177777;
		break;
	case 18:
		maxval = 0777777;
		break;
	case 22:
		maxval = 017777777;
		break;
	default:
		FATAL("Address width of %d bits invalid!", addr_width);
	}
	
	if (*addr > maxval) {
		*addr = maxval;
		return false;
	}
	return true;
}

bool qunibus_c::parse_level(char *txt, uint8_t *level) {
	*level = strtol(txt, NULL, 8);
	if (*level < 4 || *level > 7) {
		printf("Illegal interrupt level %u, must be 4..7.\n", *level);
		return false;
	}
	return true;
}

bool qunibus_c::parse_vector(char *txt, uint16_t max_vector, uint16_t *vector) {
	*vector = strtol(txt, NULL, 8);
	if (*vector > max_vector) {
		printf("Illegal interrupt vector %06o, must be <= %06o.\n", (unsigned) *vector,
				(unsigned) max_vector);
		return false;
	} else if ((*vector & 3) != 0) {
		printf("Illegal interrupt vector %06o, must be multiple of 4.\n", *vector);
		return false;
	}
	return true;
}

bool qunibus_c::parse_slot(char *txt, uint8_t *priority_slot) {
	*priority_slot = strtol(txt, NULL, 10);
	if (*priority_slot <= 0 || *priority_slot > 31) {
		printf("Illegal priority slot %u, must be 1..31.\n", *priority_slot);
		return false;
	}
	return true;
}


/* pulse INIT cycle for some milliseconds
 */
void qunibus_c::init() {
	mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_INIT;
	mailbox->initializationsignal.val = 1;
	mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
#if defined(UNIBUS)
	timeout_c::wait_ms(10); // UNIBUS: PDP-11/70 = 10ms
#elif defined(QBUS)
	timeout_c::wait_us(10); // QBUS only 10us !
#endif
	mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_INIT;
	mailbox->initializationsignal.val = 0;
	mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
}

// return: bitmask with shortcount BG*/NPG IN_OUT signals
// Values see PRIORITY_ARBITRATION_BIT_*
// Fiddling with the BG*/NPG signal may crash running CPU, also
// the M9302 will generate a SACK.
// So CPU is stopped with a surrounding a power cycle
uint8_t qunibus_c::probe_grant_continuity(bool error_if_closed) {
	uint8_t grant_mask = 0;
	// simulate POWER OFF
	powercycle(1);
#if 0
	// CPU should be stopped now, holding BG*/NPG lines active LOW = logic 0.
	// If the power vector 24 does something weird, we may have
	// BG*/NPG set and have malfunctions now.

	// Test algorithm is difficult to implement.

	// First, pull INIT low to disable fucntion of other cards

	// 3 cases:
	// 	1) Running CPU on BUS: set a BR, wait for BGIN
	//	(not tested here)
	//
	// 2) If HALTed CPU on bus: BG*/NPG held LOW
	// Need M9302 with SACK turnaround.
	// 	- set each BGOUT/NPGOUT  (assume IN  0 by CPU)
	//  - if M9302 responds with SACK:
	//      it sees a BG 1 => no jumper IN-OUT
	//		if no SACK: M9302 sees a "0" => jumper set

	// 3) If no CPU on bus: BG*/NPG pulled up
	//     set BG OUT = 0, if IN 0 -> jumper!

	// Set BG*_OUT/NPG_OUT bits at latch 0
	// and read back
	mailbox->buslatch.addr = 0;
	mailbox->buslatch.bitmask = PRIORITY_ARBITRATION_BIT_MASK;
	mailbox->buslatch.val = 0x00;// output 0 = against pullups
	mailbox_execute(ARM2PRU_BUSLATCH_SET);

	// Read back BG*_IN/NPG_IN bits from latch 0
//	mailbox->buslatch.addr = 0;
//	mailbox_execute(ARM2PRU_BUSLATCH_GET);
	uint8_t grant_mask = ~ (mailbox->buslatch.val & PRIORITY_ARBITRATION_BIT_MASK);
#endif
	// simulate POWER ON
	powercycle(2);

	if (grant_mask && error_if_closed) {
		printf("Error: GRANT IN-OUT closed on UNIBUS backplane:");
		if (grant_mask & PRIORITY_ARBITRATION_BIT_B4)
			printf(" BG4");
		if (grant_mask & PRIORITY_ARBITRATION_BIT_B5)
			printf(" BG5");
		if (grant_mask & PRIORITY_ARBITRATION_BIT_B6)
			printf(" BG6");
		if (grant_mask & PRIORITY_ARBITRATION_BIT_B7)
			printf(" BG7");
		if (grant_mask & PRIORITY_ARBITRATION_BIT_NP)
			printf(" NPG");
		printf(".\n");
		exit(1);
	}

	return grant_mask;
}

/* Simulate a power cycle
 * phase: 0x01 = only OFF, 0x02 = only ON, 0x03 = ON and OFF
 */
void qunibus_c::powercycle(int phase) {
	const unsigned delay_ms = 200; // time between phases. 70ns for QBUS
#if defined(UNIBUS)	
	/* Sequence: 
	 * 1. Line power fail -> ACLO active
	 * 2. Power supply capacitors empty -> DCLO active
	 * 3. Logic power OK -> DCLO inactive
	 * 4. Line power back -> ACLO inactive
	 *	 ACLO ist specified to go unasserted AFTER DCLO.
	 *	 For example, M9312 works only on ACLO as startup condition.
	 */
	if (phase & 0x01) { // Power Down
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_ACLO;
		mailbox->initializationsignal.val = 1;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		timeout_c::wait_ms(delay_ms);
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_DCLO;
		mailbox->initializationsignal.val = 1;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		timeout_c::wait_ms(delay_ms);
	}
	if (phase & 0x02) { // Power Up
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_DCLO;
		mailbox->initializationsignal.val = 0;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		timeout_c::wait_ms(delay_ms);
		// CPU generates INIT	  
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_ACLO;
		mailbox->initializationsignal.val = 0;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		timeout_c::wait_ms(delay_ms);
		// CPU executes power fail vector
	}
#elif defined(QBUS)	
	if (phase & 0x01) { // Power Down
		// "If the ac voltage to a power supply drops below 75% of the
		// nominal voltage for one full line cycle (15 - 24 ms), BPOK H
		// is negated by the power supply. Once BPOK H is negated the
		// entire power down sequence must be completed.
		// A device that requested bus mastership before the power
		// failure, and has not become bus master, may maintain the
		// request u n til BINIT L is asserted or the request is
		// acknowledged (in which case regular bus protocol is followed)."
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_POK;
		mailbox->initializationsignal.val = 0;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		// "Processor software should execute a RESET Instruction 3 ms
		// minimun after the negation of BPOK H. This asserts BINIT L
		// for from 8 to 20 us. Processor software executes a HALT
		// instruction imnediately following the RESET instruction."

		//	"BDCOK H must be negated a minimum of 4 ms after the negation
		// of BPOK H. This 4 ms allots mass storage and similar devices
		// to protect themselves against erasures and erroneous writes
		// during a power failure.""
		timeout_c::wait_ms(delay_ms);
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_DCOK;
		mailbox->initializationsignal.val = 0;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		// "The Processor asserts BINIT L 1 us minimum after the negation of BDCOK H."
		// "Dc power must remain stable for a minimum of 5 us after the negation of BDCOK H."
		// "BDCOK H must remain negated for a minimnn of 3 ms."
		timeout_c::wait_ms(delay_ms);
	}
	if (phase & 0x02) { // Power Up
		// "Power supply logic negates BDCOK H during power up and asserts
		// BDCOK H 3 ms minimum after dc power is restored to voltages 
		// within specification."
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_DCOK;
		mailbox->initializationsignal.val = 1;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		timeout_c::wait_ms(delay_ms);
		// "The processor asserts BINIT L after receiving nominal power 
		// and negates BINIT L 0 nsec minimum after the assertion of BDCOK H.""
		// "Power supply logic negates BPOK H during power up and asserts 
		// BPOK H 70 ms minimum after the assertion of BDCOK H>. If 
		// power does not remain stable for 70 ms, BDCOK H will be 
		// negated, therefore, devices should suspend critical actions 
		// until BPOK H is asserted. The assertion of BPOK H will cause 
		// a processor interrupt."
		mailbox->initializationsignal.id = INITIALIZATIONSIGNAL_POK;
		mailbox->initializationsignal.val = 1;
		mailbox_execute(ARM2PRU_INITALIZATIONSIGNAL_SET);
		timeout_c::wait_ms(delay_ms);
		// "BPOK H must remain asserted for a minimum of 3 ms. BDCOK H 
		// must remain asserted 4 ms minimum after the negation of BPOK H."
		// CPU executes power fail vector
	}
#endif
}

#if defined(UNIBUS)
void qunibus_c::set_address_overlay(uint32_t address_overlay) {
	mailbox->address_overlay = address_overlay;
	mailbox_execute (ARM2PRU_ADDRESS_OVERLAY);
}

// check: UNIBUS ADDR lines manipulated by (M9312) overlay?
bool qunibus_c::is_address_overlay_active() {
	return (mailbox->address_overlay != 0);
}
#endif

// force CPU to be silent on BUS
// Only necessary on QBUS: 
// even a HALTed CPU runs ODT and polls the SLU for user I/O.
void qunibus_c::set_cpu_bus_activity(bool active) {
	UNUSED(active) ;
#if defined(QBUS)
	mailbox->param = active ;
	mailbox_execute(ARM2PRU_CPU_BUS_ACCESS);
#endif
}

void qunibus_c::set_arbitrator_active(bool active) {
	if (active) {
		mailbox_execute(ARM2PRU_ARB_MODE_CLIENT);
	} else {
		mailbox_execute(ARM2PRU_ARB_MODE_NONE);
	}
	arbitrator_active = active;
}

bool qunibus_c::get_arbitrator_active(void) {
	return arbitrator_active;
}

// do a DMA transaction with or without arbitration (arbitration_client)
// mailbox.dma.words already filled
// if result = timeout: =
// 0 = bus time, error address =  mailbox->dma.cur_addr
// 1 = all transfered
// A limit for time used by DMA can be compiled-in
bool qunibus_c::dma(bool blocking, uint8_t qunibus_cycle, uint32_t startaddr, uint16_t *buffer,
		unsigned wordcount) {
	int dma_bandwidth_percent = 50; // use 50% of time for DMA, rest for running PDP-11 CPU
	uint64_t dmatime_ns, totaltime_ns;
	// can access bus with DMA when there's a Bus Arbitrator
	assert(pru->prucode_id == pru_c::PRUCODE_EMULATION);

	timeout.start_ns(0); // no timeout, just running timer
	qunibusadapter->DMA(*dma_request, blocking, qunibus_cycle, startaddr, buffer, wordcount);

	dmatime_ns = timeout.elapsed_ns();
	// wait before next transaction, to reduce QBUS/UNIBUS bandwidth
	// calc required total time for DMA time + wait
	// 100% -> total = dma
	// 50% -> total = 2*dma
	// 25% -> total = 4* dma
	totaltime_ns = (dmatime_ns * 100) / dma_bandwidth_percent;
	// whole transaction requires totaltime, dma already done
	timeout.wait_ns(totaltime_ns - dmatime_ns);

	return dma_request->success; // only useful if blocking
}

/* scan qunibus addresses ascending from 0.
 * Stop on error, return first invalid address
 * return 0: no memory found at all
 * arbitration_active: if 1, perform NPR/NPG/SACK resp. DMR/DMG/SACK arbitration before mem accesses
 * words[]: buffer for whole QBUS/UNIBUS address range, is filled with data
 */
uint32_t qunibus_c::test_sizer(void) {
	// tests chunks of 128 word
	unsigned addr = 0;

	// one big transaction, automatically split in chunks
	qunibusadapter->DMA(*dma_request, true, QUNIBUS_CYCLE_DATI, addr, testwords,
			qunibus->addr_space_word_count);
	return dma_request->unibus_end_addr; // first non implemented address
}

/*
 * Test memory from 0 .. end_addr
 * mode = 1: fill every word with its address, then check endlessly,
 */

// write a subset of words[] with QBUS/UNIBUS DMA:
// all words from start_addr to including end_addr
//
// DMA blocksize can be choosen arbitrarily
void qunibus_c::mem_write(uint16_t *words, unsigned unibus_start_addr, unsigned unibus_end_addr,
bool *timeout) {
	unsigned wordcount = (unibus_end_addr - unibus_start_addr) / 2 + 1;
	uint16_t *buffer_start_addr = words + unibus_start_addr / 2;
	assert(pru->prucode_id == pru_c::PRUCODE_EMULATION);
	*timeout = !dma(true, QUNIBUS_CYCLE_DATO, unibus_start_addr, buffer_start_addr, wordcount);
	if (*timeout) {
		printf("\nWrite timeout @ %s\n", qunibus->addr2text(mailbox->dma.cur_addr));
		return;
	}
}

// Read a subset of words[] with QBUS/UNIBUS DMA
// all words from start_addr to including end_addr
// DMA blocksize can be choosen arbitrarily
// arbitration_active: if 1, perform NPR/NPG/SACK resp. DMR/DMG/SACK arbitration before mem accesses
void qunibus_c::mem_read(uint16_t *words, uint32_t unibus_start_addr, uint32_t unibus_end_addr,
bool *timeout) {
	unsigned wordcount = (unibus_end_addr - unibus_start_addr) / 2 + 1;
	uint16_t *buffer_start_addr = words + unibus_start_addr / 2;
	assert(pru->prucode_id == pru_c::PRUCODE_EMULATION);

	*timeout = !dma(true, QUNIBUS_CYCLE_DATI, unibus_start_addr, buffer_start_addr, wordcount);
	if (*timeout) {
		printf("\nRead timeout @ %s\n", qunibus->addr2text(mailbox->dma.cur_addr));
		return;
	}
}

// read or write
void qunibus_c::mem_access_random(uint8_t unibus_control, uint16_t *words,
		uint32_t unibus_start_addr, uint32_t unibus_end_addr, bool *timeout,
		uint32_t *block_counter) {
	uint32_t block_unibus_start_addr, block_unibus_end_addr;
	// in average, make 16 sub transactions 
	assert(pru->prucode_id == pru_c::PRUCODE_EMULATION);
	assert(unibus_control == QUNIBUS_CYCLE_DATI || unibus_control == QUNIBUS_CYCLE_DATO);
	block_unibus_start_addr = unibus_start_addr;
	// split transaction in random sized blocks
	uint32_t max_block_wordcount = (unibus_end_addr - unibus_start_addr + 2) / 2;

	do {
		uint16_t *block_buffer_start = words + block_unibus_start_addr / 2;
		uint32_t block_wordcount;
		do {
			block_wordcount = random32_log(max_block_wordcount);
		} while (block_wordcount < 1);
		assert(block_wordcount < max_block_wordcount);
		// wordcount limited by "words left to transfer"
		block_wordcount = std::min(block_wordcount,
				(unibus_end_addr - block_unibus_start_addr) / 2 + 1);
		block_unibus_end_addr = block_unibus_start_addr + 2 * block_wordcount - 2;
		assert(block_unibus_end_addr <= unibus_end_addr);
		(*block_counter) += 1;
		// printf("%06d: %5u words %06o-%06o\n", *block_counter, block_wordcount, block_unibus_start_addr, block_unibus_end_addr) ;
		*timeout = !dma(true, unibus_control, block_unibus_start_addr, block_buffer_start,
				block_wordcount);
		if (*timeout) {
			printf("\n%s timeout @ %s\n", control2text(unibus_control),
					qunibus->addr2text(mailbox->dma.cur_addr));
			return;
		}
		block_unibus_start_addr = block_unibus_end_addr + 2;
	} while (block_unibus_start_addr <= unibus_end_addr);
}

// print a "memory test mismatch" message
// uses "testwords[]"
void qunibus_c::test_mem_print_error(uint32_t mismatch_count, uint32_t start_addr,
		uint32_t end_addr, uint32_t cur_test_addr, uint16_t found_mem_val) {
	uint16_t expected_mem_val = testwords[cur_test_addr / 2];
	// print bitwise error mask
	printf("\nMemory mismatch #%u at %s: expected %06o, found %06o, diff mask = %06o.  ",
			mismatch_count, qunibus->addr2text(cur_test_addr), expected_mem_val, found_mem_val,
			expected_mem_val ^ found_mem_val);

	// to analyze address errors: into which addresses should the test value have been written.
	int mem_val_found_count = 0;
	for (uint32_t addr = start_addr; addr < end_addr; addr += 2)
		if (testwords[addr / 2] == found_mem_val) {
			if (mem_val_found_count == 0)
				printf("\n  Found mem value %06o was written to addresses:", found_mem_val);
			printf(" %s", qunibus->addr2text(addr));
			mem_val_found_count++;
		}
	if (mem_val_found_count == 0)
		printf("\n Test value %06o was never written in this pass.", expected_mem_val);
}

// arbitration_active: if 1, perform NPR/NPG/SACK arbitration before mem accesses
void qunibus_c::test_mem(uint32_t start_addr, uint32_t end_addr, unsigned mode) {
#define MAX_ERROR_COUNT	8
	progress_c progress = progress_c(80);
	bool timeout = 0, mismatch = 0;
	unsigned mismatch_count = 0;
	uint32_t cur_test_addr;
	unsigned pass_count = 0, total_read_block_count = 0, total_write_block_count = 0;

	assert(pru->prucode_id == pru_c::PRUCODE_EMULATION);

	// Setup ^C catcher
	SIGINTcatchnext();
	switch (mode) {
	case 1: // single write, multiple read, "address" pattern
		/**** 1. Generate test values: only for even addresses
		 */
		for (cur_test_addr = start_addr; cur_test_addr <= end_addr; cur_test_addr += 2)
			testwords[cur_test_addr / 2] =
			// even 18 bit address  => 17 bits significant => msb bit 17 as XOR
					((cur_test_addr >> 1) & 0xffff) ^ (cur_test_addr >> 17);
		/**** 2. Write memory ****/
		progress.put("W");  //info : full memory write
		mem_write(testwords, start_addr, end_addr, &timeout);

		/**** 3. read until ^C ****/
		while (!SIGINTreceived && !timeout && !mismatch_count) {
			pass_count++;
			if (pass_count % 10 == 0)
				progress.putf(" %d ", pass_count);
			total_write_block_count++; // not randomized
			total_read_block_count++;
			progress.put("R");
			// read back into unibus_membuffer[]
			mem_read(membuffer->data.words, start_addr, end_addr, &timeout);
			// compare
			for (mismatch_count = 0, cur_test_addr = start_addr; cur_test_addr <= end_addr;
					cur_test_addr += 2) {
				uint16_t cur_mem_val = membuffer->data.words[cur_test_addr / 2];
				mismatch = (testwords[cur_test_addr / 2] != cur_mem_val);
				if (mismatch && ++mismatch_count <= MAX_ERROR_COUNT) // print only first errors
					test_mem_print_error(mismatch_count, start_addr, end_addr, cur_test_addr,
							cur_mem_val);
			}
		} // while
		break;

	case 2: // full write, full read
		/**** 1. Full write generate test values */
//		start_addr = 0;
//		end_addr = 076;
		while (!SIGINTreceived && !timeout && !mismatch_count) {
			pass_count++;
			if (pass_count % 10 == 0)
				progress.putf(" %d ", pass_count);

			for (cur_test_addr = start_addr; cur_test_addr <= end_addr; cur_test_addr += 2)
				testwords[cur_test_addr / 2] = random24() & 0xffff; // random
//				testwords[cur_test_addr / 2] = (cur_test_addr >> 1) & 0xffff; // linear

			progress.put("W");  //info : full memory write
			mem_access_random(QUNIBUS_CYCLE_DATO, testwords, start_addr, end_addr, &timeout,
					&total_write_block_count);

			if (SIGINTreceived || timeout)
				break; // leave loop

			// first full read
			progress.put("R");  //info : full memory write
			// read back into unibus_membuffer[]
			mem_access_random(QUNIBUS_CYCLE_DATI, membuffer->data.words, start_addr, end_addr,
					&timeout, &total_read_block_count);
			// compare
			for (mismatch_count = 0, cur_test_addr = start_addr; cur_test_addr <= end_addr;
					cur_test_addr += 2) {
				uint16_t cur_mem_val = membuffer->data.words[cur_test_addr / 2];
				mismatch = (testwords[cur_test_addr / 2] != cur_mem_val);
				if (mismatch && ++mismatch_count <= MAX_ERROR_COUNT) // print only first errors
					test_mem_print_error(mismatch_count, start_addr, end_addr, cur_test_addr,
							cur_mem_val);
			}
		} // while
		break;
	} // switch(mode)
	printf("\n");
	if (timeout || mismatch_count)
		printf("Stopped by error: %stimeout, %d mismatches\n", (timeout ? "" : "no "),
				mismatch_count);
	else
		printf("All OK! Total %d passes, split into %d block writes and %d block reads\n",
				pass_count, total_write_block_count, total_read_block_count);
}

