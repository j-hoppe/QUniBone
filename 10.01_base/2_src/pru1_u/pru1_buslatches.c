/* pru1_buslatches.c: PRU function to access to multiplex signal registers

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
 */

#define _BUSLATCHES_C_

#include <stdlib.h>
#include <stdint.h>

#include "tuning.h"
#include "pru1_utils.h"
#include "mailbox.h"

#include "pru_pru_mailbox.h"
#include "pru1_buslatches.h"

volatile register uint32_t __R30;
volatile register uint32_t __R31;

buslatches_t buslatches;

// Set certain ADDR lines to const "1"-levels
// Needed for M9312 boot logic
uint32_t address_overlay ;


/* central function instead of macros eliminates
 optimizer influence.
 */

void buslatches_setbits_helper(uint32_t val /* R14 */, uint32_t reg_sel /* R15 */,
		uint8_t *cur_reg_val /* R16 */) {

	/*
	 ; See Compiler 2.2 Guide, Chapter 6.6

	 XFR val to PRU0 in R14
	 code loop on PRU00:	15ns
	 ; loop:
	 ;	xin 14,&r14,4
	 ;	mov	r30,r14
	 ;	br	loop
	 Device ID 14 = "other PRU"

	 Timing data path:
	 15ns PRU0 loop
	 + 10ns 74LS377 setup (only +5ns for 74AHCT377)
	 + 5ns wires
	 => 30ns

	 Timing register select & strobe:
	 10ns setup time for 74ac138 (worst)
	 5ns wires
	 => 15ns

	 With optimized circuitry (PCB 2018-12, adapted terminators, 74AHC138):
	 Both BBB and BBG can reach
	 setbits: __delay_cycles(3)
	 setyte:  __delay_cycles(5)
	 */

	/* On optimized PCBs, speed is better if R30 (REGSEL) is set
	 BEFORE DATOUT is put to PRU0.
	 However on non-optimized boards this leads to instabilities ...
	 so software must remain at "REGSEL after DATOUT".
	 */

	//__R30 = (reg_sel << 8);
	// 14 = device id of other PRU
	// 14 = to R14
	__xout(14, 14, 0, val);
	// generates 2 cycles, additional NOP

	// select is PRU1_<8:10>
	// WRITE is PRU1_11, set L to prepare L->H pulse
	__R30 = (reg_sel << 8);

	*cur_reg_val = val; // remember register state
	// compiles to  SBBO &... : 2 cycles ?

	// => 30ns - 3 cycles for code + 1 reserve
	// wait 25ns for PRU0 datout and 74LS377 setup time
	__delay_cycles(BUSLATCHES_SETBITS_DELAY);

	// E0 at 74LS377 reached
	// strobe WRITE L->H, latch data and WRITE back to idle.
	// keep reg_sel, 74LS377 has "holdtime" of 5ns, the AC138 guarantees only 1 ns.
	__R30 |= (1 << 11);
}

void buslatches_setbyte_helper(uint32_t val /*R14*/, uint32_t reg_sel /* R15 */) {
	// timing see above
	//__R30 = (reg_sel << 8);
	__xout(14, 14, 0, val);
	// 2 cycles, generates additional NOP
//		__asm(" xout	14,&r14,4") ;
	__R30 = (reg_sel << 8);

	// => 30ns - 2 cycle2 for code + 1 reserve
	// wait 30ns for PRU0 datout and 74LS377 setup time
	__delay_cycles(7); // Test
	// __delay_cycles(6); // Standard
	//__delay_cycles(5) ; // possible on optimized PCB

	__R30 |= (1 << 11);
}

// set register signals to standard:
// all outputs to "inactive"
// init state
// UNIBUS lines all H / only BR4567, NPR_OUT auf LOW
void buslatches_reset() {
	// chips are all 8bit width, but not all input/outputs are
	// connected to bidirectional terminated UNIBUS lines.
	// see PCB schematic!

	// init all outputs and register caches:
	// UNIBUS lines now all H = inactive

	buslatches_setbits(0, 0xff, 0x1f); // BG,NPG OUT: inactive = driver H = UNIBUS L
	buslatches_setbits(1, 0xff, 0x00); // all other: inactive = driver L = UNIBUS H
	buslatches_setbyte(2, 0x00);
	buslatches_setbyte(3, 0x00);
	buslatches_setbits(4, 0xff, 0x00);
	buslatches_setbyte(5, 0x00);
	buslatches_setbyte(6, 0x00);
	buslatches_setbits(7, 0xff, 0x00);

	address_overlay = 0 ;
}

// Test burst of 8 bus latch accesses in read/write mix in max speed
// input/output from mailbox.buslatch_exerciser
// Register access sequence given by addr[] list
// Does not test fast write-after-read
void buslatches_exerciser() {
	// Max speed:
	// - unroll the test loops
	// - copy volatile indexed array data to local registers
	uint8_t addr0, addr1, addr2, addr3, addr4, addr5, addr6, addr7;
	uint8_t val0, val1, val2, val3, val4, val5, val6, val7;
	addr0 = mailbox.buslatch_exerciser.addr[0];
	addr1 = mailbox.buslatch_exerciser.addr[1];
	addr2 = mailbox.buslatch_exerciser.addr[2];
	addr3 = mailbox.buslatch_exerciser.addr[3];
	addr4 = mailbox.buslatch_exerciser.addr[4];
	addr5 = mailbox.buslatch_exerciser.addr[5];
	addr6 = mailbox.buslatch_exerciser.addr[6];
	addr7 = mailbox.buslatch_exerciser.addr[7];
	val0 = mailbox.buslatch_exerciser.writeval[0];
	val1 = mailbox.buslatch_exerciser.writeval[1];
	val2 = mailbox.buslatch_exerciser.writeval[2];
	val3 = mailbox.buslatch_exerciser.writeval[3];
	val4 = mailbox.buslatch_exerciser.writeval[4];
	val5 = mailbox.buslatch_exerciser.writeval[5];
	val6 = mailbox.buslatch_exerciser.writeval[6];
	val7 = mailbox.buslatch_exerciser.writeval[7];

	// see MAILBOX_BUSLATCH_EXERCISER_PATTERN_COUNT

	switch (mailbox.buslatch_exerciser.pattern % MAILBOX_BUSLATCH_EXERCISER_PATTERN_COUNT) {
	// now high-speed parts
	case 0: // byte accesses, UNIBUS signals
		buslatches_setbyte(addr0,val0)
		;
		buslatches_setbyte(addr1,val1)
		;
		buslatches_setbyte(addr2,val2)
		;
		buslatches_setbyte(addr3,val3)
		;
		buslatches_setbyte(addr4,val4)
		;
		buslatches_setbyte(addr5,val5)
		;
		buslatches_setbyte(addr6,val6)
		;
		buslatches_setbyte(addr7,val7)
		;
		// here a read-after-write transition
		val0 = buslatches_getbyte(addr0);
		val1 = buslatches_getbyte(addr1);
		val2 = buslatches_getbyte(addr2);
		val3 = buslatches_getbyte(addr3);
		val4 = buslatches_getbyte(addr4);
		val5 = buslatches_getbyte(addr5);
		val6 = buslatches_getbyte(addr6);
		val7 = buslatches_getbyte(addr7);
		break;
	case 1: // bit accesses, UNIBUS signals
		buslatches_setbits(addr0, 0xff, val0)
		;
		buslatches_setbits(addr1, 0xff, val1)
		;
		buslatches_setbits(addr2, 0xff, val2)
		;
		buslatches_setbits(addr3, 0xff, val3)
		;
		buslatches_setbits(addr4, 0xff, val4)
		;
		buslatches_setbits(addr5, 0xff, val5)
		;
		buslatches_setbits(addr6, 0xff, val6)
		;
		buslatches_setbits(addr7, 0xff, val7)
		;
		val0 = buslatches_getbyte(addr0);
		val1 = buslatches_getbyte(addr1);
		val2 = buslatches_getbyte(addr2);
		val3 = buslatches_getbyte(addr3);
		val4 = buslatches_getbyte(addr4);
		val5 = buslatches_getbyte(addr5);
		val6 = buslatches_getbyte(addr6);
		val7 = buslatches_getbyte(addr7);
		break;
	case 2: // fast alteration of bit and byte accesses, r/w sequential
		// pattern: byte byte bit byte byte bit bit bit
		buslatches_setbyte(addr0, val0)
		;
		buslatches_setbyte(addr1, val1)
		;
		buslatches_setbits(addr2, 0xff, val2)
		;
		buslatches_setbyte(addr3, val3)
		;
		buslatches_setbyte(addr4, val4)
		;
		buslatches_setbits(addr5, 0xff, val5)
		;
		buslatches_setbits(addr6, 0xff, val6)
		;
		buslatches_setbits(addr7, 0xff, val7)
		;
		val0 = buslatches_getbyte(addr0);
		val1 = buslatches_getbyte(addr1);
		val2 = buslatches_getbyte(addr2);
		val3 = buslatches_getbyte(addr3);
		val4 = buslatches_getbyte(addr4);
		val5 = buslatches_getbyte(addr5);
		val6 = buslatches_getbyte(addr6);
		val7 = buslatches_getbyte(addr7);
		break;
	case 3: // fast alteration of write and read
		// pattern: w w w w r w r w r w r w r r r r
		//          i i y y   i   y   i   y
		buslatches_setbits(addr0, 0xff, val0)
		;
		buslatches_setbits(addr1, 0xff, val1)
		;
		buslatches_setbyte(addr2, val2)
		;
		buslatches_setbyte(addr3, val3)
		;
		val0 = buslatches_getbyte(addr0);
		buslatches_setbits(addr4, 0xff, val4)
		;
		val1 = buslatches_getbyte(addr1);
		buslatches_setbyte(addr5, val5)
		;
		val2 = buslatches_getbyte(addr2);
		buslatches_setbits(addr6, 0xff, val6)
		;
		val3 = buslatches_getbyte(addr3);
		buslatches_setbyte(addr7, val7)
		;
		val4 = buslatches_getbyte(addr4);
		val5 = buslatches_getbyte(addr5);
		val6 = buslatches_getbyte(addr6);
		val7 = buslatches_getbyte(addr7);
		break;
	}
	// write back read values
	mailbox.buslatch_exerciser.readval[0] = val0;
	mailbox.buslatch_exerciser.readval[1] = val1;
	mailbox.buslatch_exerciser.readval[2] = val2;
	mailbox.buslatch_exerciser.readval[3] = val3;
	mailbox.buslatch_exerciser.readval[4] = val4;
	mailbox.buslatch_exerciser.readval[5] = val5;
	mailbox.buslatch_exerciser.readval[6] = val6;
	mailbox.buslatch_exerciser.readval[7] = val7;
}

#ifdef USED
// transfers a value in r14 to PRU0
// PRU0 writes this then to DATAOUT pins
// read-in on PRU 0 with
// loop:
//	xin	14,&r14,4
//  mov r40,r14
//	jmp loop
void pru1_pru0_dataout(uint32_t val) {
	// A 32bit parameter is received in r14
	// copy to PRU0 XFR area
	// "14" = device id of "other PRU"
	__asm(" xout 	14,&r14,4");
}
#endif

// timing test of register select logic
// write 4 values to full 8 bit latches 2,3,5,6
// read back and compare.
// errorflag is PRU1.12
volatile register uint32_t __R30;
volatile register uint32_t __R31;

// #define TEST_66MHZ
#define TEST_WRITE_READ_DELAY
// #define TEST_CROSSTALK
// #define TEST_WRITE_READ_VERIFY

// 8 OK?
#define buslatches_test_get(reg_sel,resvar)	do {					\
	__R30 = ((reg_sel) << 8) | (1 << 11) ;				\
	__delay_cycles(10) ;                     				\
	resvar = __R31 & 0xff ;                            				\
} while(0)

void buslatches_test(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {

	// be sure the PRU1 GPI are in "Direct Input Mode"
	// spruh73n, chapter 4.4.1.2.3.2,
	CT_CFG.GPCFG1_bit.PRU1_GPI_MODE = 0;

#ifdef TEST_66MHZ
	while (1) {
		__R30 |= (1 << 12); // set PRU1.12
		__R30 &= ~(1 << 12);// clear PRU1.12
	}
#endif

#ifdef TEST_WRITE_READ_DELAY
	// measures total time between GPI out and GPUIO in.
	// should be 10ns, is 40ns !
	// separate DATIN7 from 74LVTH, connect to PRU1.12
	while (1) {
		__R30 |= (1 << 12); // set PRU1.12
		while (!(__R31 & 0x80))
			; // wait until readback on DATAIN7

		__R30 &= ~(1 << 12); // clear PRU1.12
		while (__R31 & 0x80)
			; // wait until readback on DATAIN7
	}
#endif

#ifdef TEST_CROSSTALK
	// const pattern of 00 ff 00 ff on latch inputs.
	// register selcet causes fast switch of all 8 DATAIN.
	// Crosstalk on logic analyzers?
	a = c = 0x00;
	b = d = 0xff;
	// read/write sequence: mix of read-read and read-write
	buslatches_setbyte(2, a)
	;
	buslatches_setbyte(3, b)
	;
	buslatches_setbyte(5, c)
	;
	buslatches_setbyte(6, d)
	;

	while (mailbox.arm2pru_req == ARM2PRU_BUSLATCH_TEST) {
		uint8_t resvar;
		// echo DATA0 read only
		buslatches_test_get(2,resvar);
		PRU_DEBUG_PIN0(buslatches_getbyte(2) != a);
		// buslatches_debug_set(resvar & 1);
		buslatches_test_get(3,resvar);
		PRU_DEBUG_PIN0(buslatches_getbyte(3) != b);
		//buslatches_debug_set(resvar & 1);
		buslatches_test_get(5,resvar);
		PRU_DEBUG_PIN0(buslatches_getbyte(5) != c);
		//buslatches_debug_set(resvar & 1);
		buslatches_test_get(6,resvar);
		PRU_DEBUG_PIN0(buslatches_getbyte(6) != d);
		//buslatches_debug_set(resvar & 1);
	}
#endif

#ifdef TEST_WRITE_READ_VERIFY
	// write moving patterns into latches, read back and verify.
	// PRU1.12 is set on mismatch
	while (mailbox.arm2pru_req == ARM2PRU_BUSLATCH_TEST) {

		// read/write sequence: mix of read-read and read-write
		buslatches_setbyte(2, a)
		;
		buslatches_setbyte(3, b)
		;
		buslatches_setbyte(5, c)
		;
		if (buslatches_getbyte(2) != a)
		PRU_DEBUG_PIN0_PULSE(100);// show error flag. cleared by next reg_sel
		buslatches_setbyte(6, d)
		;
		if (buslatches_getbyte(3) != b)
		PRU_DEBUG_PIN0_PULSE(100);
		if (buslatches_getbyte(5) != c)
		PRU_DEBUG_PIN0_PULSE(100);
		if (buslatches_getbyte(6) != d)
		PRU_DEBUG_PIN0_PULSE(100);
		a++;
		b++;
		c++;
		d++;
	}
#endif
}
