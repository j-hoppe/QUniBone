/* pru1_buslatches.h: PRU function to access to multiplex signal registers

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
#ifndef _BUSLATCH_H_
#define _BUSLATCH_H_

#include <stdint.h>

#include "tuning.h"
#include "pru_pru_mailbox.h"

typedef struct {
	uint8_t cur_reg_val[8]; // content of output latches

	// # of bits in each register connected bidirectionally to UNIBUS
	// ( for example, LTC ignored)
	uint8_t bidi_bitwidth[8]; // # of bits in each
//	uint32_t 	bidi_bitmask[8] ; // mask with valid bits

//	uint8_t 	cur_reg_sel; // state of SEL A0,A1,A2 = PRU1_<8:10>
//	uint32_t 	cur_reg_write ; // state of REG_WRITE= PRU1_11>

} buslatches_t;

#ifndef _BUSLATCH_C_
extern buslatches_t buslatches;
// Set certain ADDR lines to const "1"-levels
// Needed for M9312 boot logic
extern uint32_t address_overlay ;


#endif

/*
 * Read timing:
 * 5ns on PRU to ouptput 0/1 level
 * 10ns until register select is stable (includes jitter on addr0:2)
 * 10 ns for 74LVTH to switch
 * 5ns for changing edge voltage level of DATIN signals
 * 5ns for PRU to sync with DATIN signals
 *
 * Timing verified with buslatches_test().
 *
 * With optimized circuitry (PCB 2018-12, adapted terminators, 74AHC138):
 * BBB can reach __delay_cycles(8)
 * BBG can reach *ALMOST* __delay_cycles(9)
 * */
#define buslatches_getbyte(reg_sel)	(			\
	    ( __R30 = ((reg_sel) << 8) | (1 << 11),		\
	    __delay_cycles(BUSLATCHES_GETBYTE_DELAY)							\
	), 												\
	(__R31 & 0xff)									\
	)

// identify register which must be set byte-wise
#define BUSLATCHES_REG_IS_BYTE(reg_sel) (                                            \
	((reg_sel) == 2) || ((reg_sel) == 3) || ((reg_sel) == 5) || ((reg_sel) == 6) \
	)

/*******************************************************************************
 Timing write latches 74xx377

 1 char = 5ns
 lower letter = program event
 Upper letter = circuit event

 Circuit timing 74HCT377: (74LS a few percent faster)
 ---------------------------------------------------
 Reference = Clock L->H = E
 A-E = Setup E* = 22 ns (typ. 12)
 C-E = Setup Data = 12 ns (typ. 4)
 D-E = pulsewidth = 20ns (typ. 8)
 E-B = setup E* = 22 ns (typ 12) deselect

 a  A  b  B
 (A-B)Select E*     ------______--
 c    C
 (C)  Data          XXXXXXXX--XXXX
 dD  eE
 (D+E) Strobe CP       ---____---

 => ac -> d = 10ns (minimal)
 d -> be = 15ns


 Delay program-circuit

 a-A: 5 + 10 ns (PRU + 3:8 74ac138)
 b-B = a-A
 c-D: 25ns  (pru1_buslatches_pru0_datout.asmsrc)
 d-D: 5ns
 e-E: 5ns
 *******************************************************************************/

#define buslatches_setbits(reg_sel,bitmask,val) do {	\
	/* merge new value with existing latch content                        */\
	buslatches_setbits_helper(											\
      /*val=*/(buslatches.cur_reg_val[reg_sel] & ~(bitmask)) | ((val) & (bitmask)), \
	  reg_sel, &buslatches.cur_reg_val[reg_sel] ) ;	\
	} while(0)

void buslatches_setbits_helper(uint32_t val /*R14*/, uint32_t reg_sel /* R15 */,
		uint8_t *cur_reg_val /* R16 */);

// set a register as byte.
// no value caching, so register may never be accessed bitwise
// only to be used for 2 (addr0..7), 3 (adr 8..15), 5 (data0..7), 6(data 8..15)
#define buslatches_setbyte(reg_sel,val) do {	\
	buslatches_setbyte_helper(val,reg_sel) ;	\
} while(0)

void buslatches_setbyte_helper(uint32_t val /*R14*/, uint32_t reg_sel /* R15 */);

void buslatches_reset(void);

void buslatches_exerciser(void);

void buslatches_test(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

#endif
