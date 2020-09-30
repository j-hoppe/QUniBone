/* tuning.h: Constants to adapt QBUS/UNIBUS functions

 Copyright (c) 2019-2020, Joerg Hoppe
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


 7-jun-2019  JH      entered beta phase
 */

#if defined (UNIBUS)
#define TUNING_PCB_LEGACY_SECURE
//#define TUNING_PCB_2018_12_OPTIMIZED
//#define TUNING_PCB_2019_12_OPTIMIZED
//#define TUNING_PCB_TEST
#elif defined(QBUS)
//#define TUNING_QBONE_TEST
#define TUNING_QBONE_PRODUCTION
#endif


/*** Wait cycles for buslatch access. Depends on PCB, used chips and alofirth ***/
// A BBB with optimized terminators can reach 8
// BBG can reach *ALMOST* 9
// #define BUSLATCHES_GETBYTE_DELAY	10	// Standard
#if defined(TUNING_PCB_TEST)
// experimental to test error rates
#define BUSLATCHES_GETBYTE_DELAY	9
#define BUSLATCHES_SETBITS_DELAY	3
#define BUSLATCHES_SETBYTE_DELAY	3

#elif defined(TUNING_PCB_LEGACY_SECURE)
/* Secure setting for PCBs <= 2018-12, delivered before June 2019.
 Necessary for longtime ZKMA on critical PCBs.
 BeagleBone: BBB (no BBG)
 U2 (REGSEL): 74AC138
 RN8,9 (DATIN) :   47
 RN10 <1:6>(REGADR):   33
 RN10 <7:8>(REGWRITE): 33
 R6,R7 (REGWRITE TERM): none
 RN6,RN7 (DATOUT inline): 22
 RN4,RN5 [[/DATOUT]] end) -> 1K/-
 */
#define BUSLATCHES_GETBYTE_DELAY	11
#define BUSLATCHES_SETBITS_DELAY	5
#define BUSLATCHES_SETBYTE_DELAY	7

#elif defined(TUNING_PCB_2018_12_OPTIMIZED)
/* Setting for PCB v2018_12 with optimized timing (ticket 21, June 2019)
 BeagleBone: BBB (no BBG)
 U2 (REGSEL): 74AC138 -> 74AHC138
 RN8,9 (DATIN) :   47 -> 68 Ohm
 RN10 <1:6>(REGADR):   33->0 Ohm
 RN10 <7:8>(REGWRITE): 33->0 Ohm
 R6,R7 (REGWRITE TERM): none
 RN6,RN7 (DATOUT inline): 22 -> 27
 RN4,RN5 [[/DATOUT]] end) -> 180/-
 */
#define BUSLATCHES_GETBYTE_DELAY	9
#define BUSLATCHES_SETBITS_DELAY	4
#define BUSLATCHES_SETBYTE_DELAY	6
//#define BUSLATCHES_GETBYTE_DELAY	8
//#define BUSLATCHES_SETBITS_DELAY	3
//#define BUSLATCHES_SETBYTE_DELAY	5
#elif defined(TUNING_PCB_2019_12_OPTIMIZED)
/* Setting for PCB v2018_12 with optimized timing (ticket 21, June 2019)
 BeagleBone: BBB (no BBG)
 U2 (REGSEL): 74AC138 -> 74AHC138
 RN8,9 (DATIN) :   47 -> 68 Ohm
 RN10 <1:6>(REGADR):   33->0 Ohm
 RN10 <7:8>(REGWRITE): 33->0 Ohm
 R6,R7 (REGWRITE TERM): none
 RN6,RN7 (DATOUT inline): 22 -> 27
 RN4,RN5 [[/DATOUT]] end) -> 180/-
 */
#define BUSLATCHES_GETBYTE_DELAY	7
#define BUSLATCHES_SETBITS_DELAY	0
#define BUSLATCHES_SETBYTE_DELAY	0



#elif defined(TUNING_QBONE_TEST)
/*
RN1,2 (DATIN) :   22 Ohm
RN3 <1:6>(REGADR):   22 Ohm
RN3 <7:8>(REGWRITE): 22 Ohm
R14,R18 (REGWRITE TERM): none
RN4,RN5 (DATOUT inline): 22
RN9,RN10 [[/DATOUT]] end) -> none
*/
/* Limit for QBone with 22 Ohm inline terminators */
#define BUSLATCHES_GETBYTE_DELAY	7 // 6: errors (LA attached)
#define BUSLATCHES_SETBITS_DELAY	5 // 0  
#define BUSLATCHES_SETBYTE_DELAY	6 // 1 // more critical than setbits


#elif defined(TUNING_QBONE_PRODUCTION)
// conservative, 6,1,1 possible?
#define BUSLATCHES_GETBYTE_DELAY	7
#define BUSLATCHES_DATOUT_DELAY	1	// extra PCB delay PRU0 DATOUT
#define BUSLATCHES_WRITE_DELAY	0	// extra PCB  delay PRU_WRITE
#endif

// UNIBUS timing: Wait to stabilize DATA before MSYN asserted
// per DEC spec
// #define UNIBUS_DMA_MASTER_PRE_MSYN_NS	150

// Josh Dersch on 11/84, also for VAX 11/750
// Additional delay on PDP11s with private memory interconnect (PMI)
// and UNIBUS/PMI translation?
// Experiments with "250" made still occasional errors.
#define UNIBUS_DMA_MASTER_PRE_MSYN_NS	400
