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
#include <stdbool.h>

#include "tuning.h"
#include "pru1_utils.h"
#include "mailbox.h"

#include "pru_pru_mailbox.h"
#include "pru1_buslatches.h"

volatile register uint32_t __R30;
volatile register uint32_t __R31;

buslatches_t buslatches;

/* central function instead of macros eliminates
 optimizer influence.
 */
/*
 ; See Compiler 2.2 Guide, Chapter 6.6

 XFR val to PRU0 in R14
 code loop on PRU00:	15ns
 ; loop:
 ;	xin 14,&r14,4
 ;	mov r30,r14
 ;	br	loop
 Device ID 14 = "other PRU"
 */

/* On optimized PCBs, speed is better if R30 (REGSEL) is set
 BEFORE DATOUT is put to PRU0.
 However on non-optimized boards this leads to instabilities ...
 so software must remain at "REGSEL after DATOUT".
 */





void buslatches_setbyte_helper(uint32_t val /*R14*/, uint32_t reg_sel /* R15 */) {
	// timing: delayed parallel processes. 
	// PRU_DATOUT delayed by PRU0, PRU_WRITE latching delayed by CPLD
	
	//__R30 = (reg_sel << 8);
	// 14 = device id of other PRU
	// 14 = to R14
	__xout(14, 14, 0, val);
	// 2 cycles = 10ns, generates additional NOP
//		__asm(" xout	14,&r14,4") ;
	// PRU0 needs 2..5 = 10..25ns cycles to output PRU_DATOUT
	// select is PRU1_<8:10>
	// WRITE is PRU1_11, set L to prepare L->H pulse
	__R30 = (reg_sel << 8);  // 1 cycle , reg_sel, pulse PRU_WRITE low
	__delay_cycles(1+BUSLATCHES_DATOUT_DELAY); // make sure fastest PRU_WRITE latch in CPLD (10ns) is after longest PRU_OUT (25ns)
	__R30 |= (1 << 11) ; // set WRITE. 1 cycle 
	// reg_sel immediate, PRU_WRITE delayed in CPLD by 10..20ns

	// keep PRU_OUT and REGSEL stable until PRU_WRITE edge detect in CPLD.
	// next could be a "getbyte()" macro, which immediately sets REG_SEL
	// return is 5ns keep REG_SEL stable for at least 25ns
	__delay_cycles(3+BUSLATCHES_WRITE_DELAY); 
	// can be shortened when 
	// - CPLD PRU_WRITE shift register stages reduced (with increased metastability)
	// - or CPLD faster clock (100MHz -> 200 MHz)
	// - or REG_SEL delayed by same shift register count as PRU_WRITE (so it can be deled on CPLD input early)
}

void buslatches_setbits_helper(uint32_t val /* R14 */, uint32_t reg_sel /* R15 */,
		uint8_t *cur_reg_val /* R16 */) {

	//__R30 = (reg_sel << 8);
	__xout(14, 14, 0, val);
	// generates 2 cycles, additional NOP

	__R30 = (reg_sel << 8);

	*cur_reg_val = val; // remember register state
	// compiles to  SBBO &... : 2 cycles ?

	__R30 |= (1 << 11);
	__delay_cycles(3+BUSLATCHES_WRITE_DELAY); // can be shortened when CPLD shift regsiter is faster
}


#ifdef USED
void xbuslatches_setbyte_helper(uint32_t val /*R14*/, uint32_t reg_sel /* R15 */) {
	// timing see above
	//__R30 = (reg_sel << 8);
	__xout(14, 14, 0, val);
	// 2 cycles, generates additional NOP
//		__asm(" xout	14,&r14,4") ;
	__R30 = (reg_sel << 8);

	// => 30ns - 2 cycle2 for code + 1 reserve
	// wait for PRU0 datout (15ns) and register select setup time
	__delay_cycles(BUSLATCHES_SETBYTE_DELAY);

	__R30 |= (1 << 11);
}

void xbuslatches_setbits_helper(uint32_t val /* R14 */, uint32_t reg_sel /* R15 */,
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
#endif

/* special logic for latched ADDR register 3,4,5(partly)
 Value is written via DAL in reg 0,1,2 with SYNC mux.
 Only for selftest purposes, Lots of side effects!
 */
void buslatches_setbits_mux_helper(uint32_t reg_sel, uint32_t bitmask, uint32_t val) {
	// save state of DAL lines, SYNC and HALT
	// do not toch HALT
	uint8_t saved_reg0_val = buslatches_getbyte(0) ;
	uint8_t saved_reg1_val = buslatches_getbyte(1) ;
	uint8_t saved_reg2_val = buslatches_getbyte(2) ;
	// set DAL new with content of other ADR registers

	uint8_t data_reg0_val = buslatches_getbyte(3) ;
	uint8_t data_reg1_val = buslatches_getbyte(4) ;
	uint8_t data_reg2_val = buslatches_getbyte(5) ;
	switch (reg_sel) {
	case 3: // ADDR<7:0>
		data_reg0_val = (data_reg0_val & ~bitmask) | (val & bitmask) ;
		break;
	case 4: // <ADDR15:8>
		data_reg1_val = (data_reg1_val & ~bitmask) | (val & bitmask) ;
		break;
	case 5: // ADDR<21:16>,BS7,WTBT. WTBT not lacthed on SYNC
		data_reg2_val = (data_reg2_val & ~bitmask) | (val & bitmask) ;
		// write bit7 WTBT of reg 5 directly,
		buslatches_setbits(5, 0xc0, val) ; // write BS7, WTBT directly onto BUS
		break;
	}
	// output mix of new and old bits onto DAL
	buslatches_setbyte(0, data_reg0_val) ;
	buslatches_setbyte(1, data_reg1_val) ;
	data_reg2_val &= ~0x80 ; // clear SYNC
	buslatches_setbits(2, 0xbf, data_reg2_val) ; // do not touch bit6 HALT, output SYNC=0
	// set SYNC after data, so 74374 in CPLD1 latches ADDR and BS7 on raising edge
	buslatches_setbits(2, 0x80, 0x80) ;

	// restore all DAL lines and SYNC	
	buslatches_setbyte(0, saved_reg0_val) ;
	buslatches_setbyte(1, saved_reg1_val) ;
	buslatches_setbits(2, 0xff, saved_reg2_val) ;
	// ad hoc deleay to pass selftest. signal run time to following "readbyte()" ?
	__delay_cycles(NANOSECS(10)) ; 

}

// set register signals to standard:
// all outputs to "inactive"
// init state
// QBUS lines all H
void buslatches_reset() {
	// registers are all 8bit width, but not all input/outputs are
	// connected to bidirectional terminated QBUS lines.
	// see PCB schematic!

	// init all outputs and register caches:
	// QBUS lines now all H = inactive
	buslatches_setbyte(0, 0x00); // TDAL<7:0>
	buslatches_setbyte(1, 0x00); // TDAL<15:8>
	buslatches_setbits(2, 0xff, 0x00); // TDAL<21:16>,TBS7,TSYNC
	buslatches_setbyte(3, 0x03); // cmd: clear RSYNClatch, clear TDAL (again)
	buslatches_setbits(4, 0xff, 0x00); // clear TSYNC,TDIN;DOUT...REF
	buslatches_setbits(5, 0xff, 0x18); // SYSTEM: POK, DCOK asserted
	buslatches_setbits(6, 0xff, 0x00); // IRQs, GRANTs,;ACKs
	buslatches_setbits(7, 0xff, 0x00); // unused
}


/*  Test burst of 8 bus latch accesses in read/write mix in max speed
 input/output from mailbox.buslatch_exerciser
 Register access sequence given by addr[] list
 Does not test fast write-after-read

 DAL/ADDRMUX logic:
 CPLD latch DAL and other on SYNC, then set status bit SYNClatch.
 R/W test can be done on SYNC, but before reading latched signals, SNYClatch must be cleared
 */


// capsules byte, bit and multiplexe accesses. 
// Arbitrary use of byte and bit access
// MAcro for speed reasons? code size!
 #define xexerciser_latch_set(addr,val) do {		\
	if (BUSLATCHES_REG_IS_BYTE(addr))	\
		buslatches_setbyte(addr,val) ;	\
	else 	\
		buslatches_setbits(addr,0xff, val) ; /* update cur_reg_val */	\
} while(0)


// helper logic to test bus latches:
// - clear SYNClatch before read of latched DAL in reg0,1,2
// - don't test reg 3,7
// - save bs7 on write via reg4. use cached bs7 on write into reg2, (else reg2 write clears bs7)
bool exerciser_bs7 ; 
void exerciser_latch_set(uint8_t addr, uint8_t val)	 {
	switch(addr) {
		case 0:
		case 1:
			buslatches_setbyte(addr,val) ;
			break ;
		case 2:
			// ARM sets bit7,6 always 0
			if (exerciser_bs7)
				val |= 0x40 ; 
				buslatches_setbyte(addr,val) ;		
			break ;
		case 4:
		exerciser_bs7 = val & 0x20 ? 1 : 0 ; // bit 5
		buslatches_setbits(addr,0xff, val) ; // update cur_reg_val
		break ;
		case 5: 
		case 6:
			buslatches_setbits(addr,0xff, val) ; // update cur_reg_val
		break ;
		default:; // ignore 3, 7
	}
}	

uint8_t execerciser_latch_get(uint8_t addr) {
	// make SYNC latched bits transparent to show QBUS signals.
	if (addr <= 2)
		buslatches_setbyte(3, 0x01) ; // SYNClatch
	return buslatches_getbyte(addr) ;
}
 	

 
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
	case 0: // write all, read all
		exerciser_latch_set(addr0,val0) ;
		exerciser_latch_set(addr1,val1)	;
		exerciser_latch_set(addr2,val2)	;
		exerciser_latch_set(addr3,val3)	;
		exerciser_latch_set(addr4,val4)	;
		exerciser_latch_set(addr5,val5)	;
		exerciser_latch_set(addr6,val6)	;
		exerciser_latch_set(addr7,val7)	;
		// here a read-after-write transition
		val0 = execerciser_latch_get(addr0);
		val1 = execerciser_latch_get(addr1);
		val2 = execerciser_latch_get(addr2);
		val3 = execerciser_latch_get(addr3);
		val4 = execerciser_latch_get(addr4);
		val5 = execerciser_latch_get(addr5);
		val6 = execerciser_latch_get(addr6);
		val7 = execerciser_latch_get(addr7);
		break;
	case 1: // fast write-read for all. verifies the PRU-CPLD-8641-QBUS-8641-LVC245-CPLD-PRU turnaround time
#define SIGNAL_TURNAROUND_NS 30 	// ad hoc
		exerciser_latch_set(addr0,val0) ;
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val0 = execerciser_latch_get(addr0);
		exerciser_latch_set(addr1,val1)	;
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val1 = execerciser_latch_get(addr1);
		exerciser_latch_set(addr2,val2);
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val2 = execerciser_latch_get(addr2);
		exerciser_latch_set(addr3,val3);
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val3 = execerciser_latch_get(addr3);
		exerciser_latch_set(addr4,val4);
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val4 = execerciser_latch_get(addr4);
		exerciser_latch_set(addr5,val5);
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val5 = execerciser_latch_get(addr5);
		exerciser_latch_set(addr6,val6);
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val6 = execerciser_latch_get(addr6);
		exerciser_latch_set(addr7,val7);
		__delay_cycles(NANOSECS(SIGNAL_TURNAROUND_NS)) ;
		val7 = execerciser_latch_get(addr7);
		break;
		
	case 2: // mixed alteration of write and read
		// pattern: w w w w r w r w r w r w r r r r
		//          i i y y   i   y   i   y
		exerciser_latch_set(addr0, val0);
		exerciser_latch_set(addr1, val1);
		exerciser_latch_set(addr2, val2);
		exerciser_latch_set(addr3, val3);
		val0 = execerciser_latch_get(addr0);
		exerciser_latch_set(addr4, val4);
		val1 = execerciser_latch_get(addr1);
		exerciser_latch_set(addr5, val5);
		val2 = execerciser_latch_get(addr2);
		exerciser_latch_set(addr6, val6);
		val3 = execerciser_latch_get(addr3);
		exerciser_latch_set(addr7, val7);
		val4 = execerciser_latch_get(addr4);
		val5 = execerciser_latch_get(addr5);
		val6 = execerciser_latch_get(addr6);
		val7 = execerciser_latch_get(addr7);
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
