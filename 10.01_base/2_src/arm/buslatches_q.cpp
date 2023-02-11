/* buslatches_q.cpp: PRU GPIO multiplier latches on QBone PCB.

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

#include "buslatches.hpp"

buslatches_c buslatches; // Singleton


/* return a string with board signal path for an UNIBUS/QBUS signal
 * used as error info for loopback failures
 */
buslatches_wire_info_t buslatches_wire_info[] = { //
//
		// Register 0 write (PRU -> CPLD1 -> DS8641)
				{ 0, 0, 0, 0, "DAL00",
						"P9.31 -> J24.1 DATOUT_0 -> CPLD1 -> U09.02 -> U09.01 -> BDAL00" },//
				{ 0, 1, 0, 0, "DAL01",
						"P9.29 -> J24.2 DATOUT_1 -> CPLD1 -> U09.05 -> U09.04 -> BDAL01" },//
				{ 0, 2, 0, 0, "DAL02",
						"P9.30 -> J24.3 DATOUT_2 -> CPLD1 -> U04.14 -> U04.15 -> BDAL02" },//
				{ 0, 3, 0, 0, "DAL03",
						"P9.28 -> J24.4 DATOUT_3 -> CPLD1 -> U04.11 -> U04.12 -> BDAL03" },//
				{ 0, 4, 0, 0, "DAL04",
						"P9.42 -> J24.5 DATOUT_4 -> CPLD1 -> U04.02 -> U04.01 -> BDAL04" },//
				{ 0, 5, 0, 0, "DAL05",
						"P9.27 -> J24.6 DATOUT_5 -> CPLD1 -> U04.05 -> U04.04 -> BDAL05" },//
				{ 0, 6, 0, 0, "DAL06",
						"P9.41 -> J24.7 DATOUT_6 -> CPLD1 -> U12.13 -> U12.15 -> BDAL06" },//
				{ 0, 7, 0, 0, "DAL07",
						"P9.25 -> J24.8 DATOUT_7 -> CPLD1 -> U12.10 -> U12.12 -> BDAL07" },//
//
// Register 0 read (PRU <- CPLD1 <- 74LVC245 <- DS8641)
				{ 0, 0, 1, 0, "DAL00",
						"P8.45 <- J29.1 DATIN_0 <- CPLD1 <- U06.12 <- U06.08 <- U09.03 <- U09.01 <- BDAL00" },	//
				{ 0, 1, 1, 0, "DAL01",
						"P8.46 <- J29.2 DATIN_1 <- CPLD1 <- U06.11 <- U06.09 <- U09.06 <- U09.04 <- BDAL01" },	//
				{ 0, 2, 1, 0, "DAL02",
						"P8.43 <- J29.3 DATIN_2 <- CPLD1 <- U06.18 <- U06.02 <- U04.13 <- U04.15 <- BDAL02" },	//
				{ 0, 3, 1, 0, "DAL03",
						"P8.44 <- J29.4 DATIN_3 <- CPLD1 <- U06.17 <- U06.03 <- U04.10 <- U04.12 <- BDAL03" },	//
				{ 0, 4, 1, 0, "DAL04",
						"P8.41 <- J29.5 DATIN_4 <- CPLD1 <- U06.16 <- U06.04 <- U04.03 <- U04.01 <- BDAL04" },	//
				{ 0, 5, 1, 0, "DAL05",
						"P8.42 <- J29.6 DATIN_5 <- CPLD1 <- U06.15 <- U06.05 <- U04.06 <- U04.04 <- BDAL05" },	//
				{ 0, 6, 1, 0, "DAL06",
						"P8.39 <- J29.7 DATIN_6 <- CPLD1 <- U11.14 <- U11.06 <- U12.13 <- U12.15 <- BDAL06" },	//
				{ 0, 7, 1, 0, "DAL07",
						"P8.40 <- J29.8 DATIN_7 <- CPLD1 <- U11.13 <- U11.07 <- U12.10 <- U12.12 <- BDAL07" },	//
//
// Register 1 write  (PRU -> CPLD1 -> DS8641)
				{ 1, 0, 0, 0, "DAL08",
						"P9.31 -> J24.1 DATOUT_0 -> CPLD1 -> U12.02 -> U12.01 -> BDAL08" },//
				{ 1, 1, 0, 0, "DAL09",
						"P9.29 -> J24.2 DATOUT_1 -> CPLD1 -> U12.05 -> U12.04 -> BDAL09" },//
				{ 1, 2, 0, 0, "DAL10",
						"P9.30 -> J24.3 DATOUT_2 -> CPLD1 -> U10.14 -> U10.15 -> BDAL10" },//
				{ 1, 3, 0, 0, "DAL11",
						"P9.28 -> J24.4 DATOUT_3 -> CPLD1 -> U10.11 -> U10.12 -> BDAL11" },//
				{ 1, 4, 0, 0, "DAL12",
						"P9.42 -> J24.5 DATOUT_4 -> CPLD1 -> U10.02 -> U10.01 -> BDAL12" },//
				{ 1, 5, 0, 0, "DAL13",
						"P9.27 -> J24.6 DATOUT_5 -> CPLD1 -> U10.05 -> U10.04 -> BDAL13" },//
				{ 1, 6, 0, 0, "DAL14",
						"P9.41 -> J24.7 DATOUT_6 -> CPLD1 -> U08.14 -> U08.15 -> BDAL14" },//
				{ 1, 7, 0, 0, "DAL15",
						"P9.25 -> J24.8 DATOUT_7 -> CPLD1 -> U08.11 -> U08.12 -> BDAL15" },//
//
// Register 1 read (PRU <- CPLD1 <- 74LVC245 <- DS8641)
				{ 1, 0, 1, 0, "DAL08",
						"P8.45 <- J29.1 DATIN_0 <- CPLD1 <- U11.12 <- U11.08 <- U12.03 <- U12.01 <- BDAL08" },	//
				{ 1, 1, 1, 0, "DAL09",
						"P8.46 <- J29.2 DATIN_1 <- CPLD1 <- U11.11 <- U11.09 <- U12.06 <- U12.04 <- BDAL09" },	//
				{ 1, 2, 1, 0, "DAL10",
						"P8.43 <- J29.3 DATIN_2 <- CPLD1 <- U11.18 <- U11.02 <- U10.13 <- U10.15 <- BDAL10" },	//
				{ 1, 3, 1, 0, "DAL11",
						"P8.44 <- J29.4 DATIN_3 <- CPLD1 <- U11.17 <- U11.03 <- U10.10 <- U10.12 <- BDAL11" },	//
				{ 1, 4, 1, 0, "DAL12",
						"P8.41 <- J29.5 DATIN_4 <- CPLD1 <- U11.16 <- U11.04 <- U10.03 <- U10.01 <- BDAL12" },	//
				{ 1, 5, 1, 0, "DAL13",
						"P8.42 <- J29.6 DATIN_5 <- CPLD1 <- U11.15 <- U11.05 <- U10.06 <- U10.04 <- BDAL13" },	//
				{ 1, 6, 1, 0, "DAL14",
						"P8.39 <- J29.7 DATIN_6 <- CPLD1 <- U05.14 <- U05.06 <- U08.13 <- U08.15 <- BDAL14" },	//
				{ 1, 7, 1, 0, "DAL15",
						"P8.40 <- J29.8 DATIN_7 <- CPLD1 <- U05.13 <- U05.07 <- U08.10 <- U08.12 <- BDAL15" },	//
//
// Register 2 write  (PRU -> CPLD1/2 -> DS8641)
				{ 2, 0, 0, 0, "DAL16",
						"P9.31 -> J24.1 DATOUT_0 -> CPLD1 -> U08.02 -> U08.01 -> BDAL16" },//
				{ 2, 1, 0, 0, "DAL17",
						"P9.29 -> J24.2 DATOUT_1 -> CPLD1 -> U08.05 -> U08.04 -> BDAL17" },//
				{ 2, 2, 0, 0, "DAL18",
						"P9.30 -> J24.3 DATOUT_2 -> CPLD1 -> U03.14 -> U03.15 -> BDAL18" },//
				{ 2, 3, 0, 0, "DAL19",
						"P9.28 -> J24.4 DATOUT_3 -> CPLD1 -> U03.11 -> U03.12 -> BDAL19" },//
				{ 2, 4, 0, 0, "DAL20",
						"P9.42 -> J24.5 DATOUT_4 -> CPLD1 -> U03.02 -> U03.01 -> BDAL20" },//
				{ 2, 5, 0, 0, "DAL21",
						"P9.27 -> J24.6 DATOUT_5 -> CPLD1 -> U03.05 -> U03.04 -> BDAL21" },//
				{ 2, 6, 0, 2, "BS7*",
						"P9.41 -> J24.7 DATOUT_6 -> CPLD1 -> U09.11 -> U09.12 -> BBS7"},//


//
// Register 2 read (PRU <- CPLD1/2 <- 74LVC245 <- DS8641)
				{ 2, 0, 1, 0, "DAL16",
						"P8.45 <- J29.1 DATIN_0 <- CPLD1 <- U05.12 <- U05.08 <- U08.03 <- U08.01 <- BDAL16" },	//
				{ 2, 1, 1, 0, "DAL17",
						"P8.46 <- J29.2 DATIN_1 <- CPLD1 <- U05.11 <- U05.09 <- U08.06 <- U08.04 <- BDAL17" },	//
				{ 2, 2, 1, 0, "DAL18",
						"P8.43 <- J29.3 DATIN_2 <- CPLD1 <- U05.18 <- U05.02 <- U03.13 <- U03.15 <- BDAL18" },	//
				{ 2, 3, 1, 0, "DAL19",
						"P8.44 <- J29.4 DATIN_3 <- CPLD1 <- U05.17 <- U05.03 <- U03.10 <- U03.12 <- BDAL19" },	//
				{ 2, 4, 1, 0, "DAL20",
						"P8.41 <- J29.5 DATIN_4 <- CPLD1 <- U05.16 <- U05.04 <- U03.03 <- U03.01 <- BDAL20" },	//
				{ 2, 5, 1, 0, "DAL21",
						"P8.42 <- J29.6 DATIN_5 <- CPLD1 <- U05.15 <- U05.05 <- U03.06 <- U03.04 <- BDAL21" },	//
				{ 2, 6, 1, 2, "BS7*",
						"P8.39 <- J29.7 DATIN_6 <- CPLD1 <- U06.13 <- U06.07 <- U09.10 <- U09.12 <- BBS7" },//
				{ 2, 7, 1, 4, "SYNClatch",
						"P8.40 <- J29.8 DATIN_7 <- CPLD1" }, // SYNC+REF
//
// Register 3 write: only commands
//
// Register 3 read:  (PRU <- CPLD1/2) latched address, same physical routing as register 0 DAL<7:0>
				{ 3, 0, 1, 4, "SYNClatch",
						"P8.45 <- J29.1 DATIN_0 <- CPLD1" },	// SYNC+REF
				{ 3, 1, 1, 2, "BS7*",
						"P8.46 <- J29.2 DATIN_1 <- CPLD1 <- U06.13 <- U06.07 <- U09.10 <- U09.12 <- BBS7" },	//
				{ 3, 2, 1, 2, "WTBT*",
						"P8.43 <- J29.3 DATIN_2 <- CPLD2 <- U21.12 <- U21.08 <- U22.03 <- U22.01 <- BWTBT" },	//
				{ 3, 3, 1, 2, "REF*",
						"P8.44 <- J29.4 DATIN_3 <- CPLD2 <- U15.14 <- U15.06 <- U18.13 <- U18.15 <- BREF" },	//
//
// Register 4 write:  (PRU -> CPLD2 -> DS8641) DATA cycle control signals
//
				{ 4, 0, 0, 0, "SYNC",
						"P9.31 -> J24.1 DATOUT_0 -> CPLD1 -> U09.14 -> U09.15 -> BSYNC" },//
				{ 4, 1, 0, 0, "DIN",
						"P9.29 -> J24.2 DATOUT_1 -> CPLD2 -> U22.14 -> U22.15 -> BDIN" },//
				{ 4, 2, 0, 0, "DOUT",
						"P9.30 -> J24.3 DATOUT_2 -> CPLD2 -> U22.11 -> U22.12 -> BDOUT" },//
				{ 4, 3, 0, 0, "RPLY",
						"P9.28 -> J24.4 DATOUT_3 -> CPLD2 -> U14.14 -> U14.15 -> BRPLY" },//
				{ 4, 4, 0, 0, "WTBT",
						"P9.42 -> J24.5 DATOUT_4 -> CPLD2 -> U22.02 -> U22.01 -> BWTBT" },//
				{ 4, 5, 0, 0, "BS7",
						"P9.27 -> J24.6 DATOUT_5 -> CPLD1 -> U09.11 -> U09.12 -> BBS7" },//
				{ 4, 6, 0, 0, "REF",
						"P9.41 -> J24.7 DATOUT_6 -> CPLD2 -> U18.14 -> U18.15 -> BREF" },//


// Register 4 read:  (PRU <- CPLD1/2 <- 74LVC245 <- DS8641) DATA cycle control signals
				{ 4, 0, 1, 0, "SYNC",
						"P8.45 <- J29.1 DATIN_0 <- CPLD1 <- U06.14 <- U06.06 <- U09.13 <- U09.15 <- BSYNC" },	//
				{ 4, 1, 1, 0, "DIN",
						"P8.46 <- J29.2 DATIN_1 <- CPLD2 <- U21.14 <- U21.06 <- U22.13 <- U22.15 <- BDIN" },	//
				{ 4, 2, 1, 0, "DOUT",
						"P8.43 <- J29.3 DATIN_2 <- CPLD2 <- U21.13 <- U21.07 <- U22.10 <- U22.12 <- BDOUT" },	//
				{ 4, 3, 1, 0, "RPLY",
						"P8.44 <- J29.4 DATIN_3 <- CPLD2 <- U16.18 <- U16.02 <- U14.13 <- U14.15 <- BRPLY" },	//
				{ 4, 4, 1, 0, "WTBT",
						"P8.41 <- J29.5 DATIN_4 <- CPLD2 <- U21.12 <- U21.08 <- U22.03 <- U22.01 <- BWTBT" },	//
				{ 4, 5, 1, 0, "BS7",
						"P8.42 <- J29.6 DATIN_5 <- CPLD1 <- U06.13 <- U06.07 <- U09.10 <- U09.12 <- BBS7" },	//
				{ 4, 6, 1, 0, "REF",
						"P8.39 <- J29.7 DATIN_6 <- CPLD2 <- U15.14 <- U15.06 <- U18.13 <- U18.15 <- BREF" },	//

//
// Register 5 write:  (PRU -> CPLD2 -> DS8641) SYSTEM signals
//
				{ 5, 0, 0, 0, "INIT",
						"P9.31 -> J24.1 DATOUT_0 -> CPLD2 -> U13.02 -> U13.01 -> BINIT" },//
				{ 5, 1, 0, 0, "HALT",
						"P9.29 -> J24.2 DATOUT_1 -> CPLD2 -> U13.05 -> U13.04 -> BHALT" },//
				{ 5, 2, 0, 0, "EVNT",
						"P9.30 -> J24.3 DATOUT_2 -> CPLD2 -> U18.11 -> U18.12 -> BEVNT" },//
				{ 5, 3, 0, 0, "POK",
						"P9.28 -> J24.4 DATOUT_3 -> CPLD2 -> U13.14 -> U13.15 -> BPOK" },//
				{ 5, 4, 0, 0, "DCOK",
						"P9.42 -> J24.5 DATOUT_4 -> CPLD2 -> U13.11 -> U13.12 -> BDCOK" },//
				{ 5, 5, 0, 0, "SRUN",
						"P9.27 -> J24.6 DATOUT_5 -> CPLD2 -> U18.02 -> U18.01 -> SSPARE2,3" },//


//
// Register 5 read:  (PRU <- CPLD1/2 <- 74LVC245 <- DS8641) SYSTEM signals
//
				{ 5, 0, 1, 0, "INIT",
						"P8.45 <- J29.1 DATIN_0 <- CPLD2 <- U15.16 <- U15.04 <- U13.03 <- U13.01 <- BINIT" },	//
				{ 5, 1, 1, 0, "HALT",
						"P8.46 <- J29.2 DATIN_1 <- CPLD2 <- U15.15 <- U15.05 <- U13.06 <- U13.04 <- BHALT" },	//
				{ 5, 2, 1, 0, "EVNT",
						"P8.43 <- J29.3 DATIN_2 <- CPLD2 <- U15.13 <- U15.07 <- U18.10 <- U18.12 <- BEVNT" },	//
				{ 5, 3, 1, 0, "POK",
						"P8.44 <- J29.4 DATIN_3 <- CPLD2 <- U15.18 <- U15.02 <- U13.13 <- U13.15 <- BPOK" },	//
				{ 5, 4, 1, 0, "DCOK",
						"P8.41 <- J29.5 DATIN_4 <- CPLD2 <- U15.17 <- U15.03 <- U13.10 <- U13.12 <- BDCOK" },	//
//				{ 5, 5, 1, 0, "SRUN",
//						"P8.42 <- J29.6 DATIN_5 <- CPLD2 <- U05.15 <- U05.05 <- U03.06 <- U03.04 <- BDAL21" },	//

//
// Register 6 write  (PRU -> CPLD2 -> DS8641) IRQ/DMA
				{ 6, 0, 0, 0, "IRQ4",
						"P9.31 -> J24.1 DATOUT_0 -> CPLD2 -> U19.14 -> U19.15 -> BIRQ4" },//
				{ 6, 1, 0, 0, "IRQ5",
						"P9.29 -> J24.2 DATOUT_1 -> CPLD2 -> U19.11 -> U19.12 -> BIRQ5" },	//
				{ 6, 2, 0, 0, "IRQ6",
						"P9.30 -> J24.3 DATOUT_2 -> CPLD2 -> U19.02 -> U19.01 -> BIRQ6" },	//
				{ 6, 3, 0, 0, "IRQ7",
						"P9.28 -> J24.4 DATOUT_3 -> CPLD2 -> U19.05 -> U19.04 -> BIRQ7" },	//
				{ 6, 4, 0, 0, "DMR",
						"P9.42 -> J24.5 DATOUT_4 -> CPLD2 -> U20.05 -> U20.04 -> BDMR" },	//
				{ 6, 5, 0, 0, "IAKO",
						"P9.27 -> J24.6 DATOUT_5 -> CPLD2 -> U14.02 -> U14.01 -> BIAKO" },	//
				{ 6, 6, 0, 0, "DMGO",
						"P9.41 -> J24.7 DATOUT_6 -> CPLD2 -> U20.02 -> U20.01 -> BDMGO" },//
				{ 6, 7, 0, 0, "SACK",
						"P9.25 -> J24.8 DATOUT_7 -> CPLD2 -> U20.14 -> U20.15 -> BSACK" },	//
//
// Register 6 read (PRU <- CPLD2 <- 74LVC245 <- DS8641) IRQ/DMA
				{ 6, 0, 1, 0, "IRQ4",
						"P8.45 <- J29.1 DATIN_0 <- CPLD2 <- U16.14 <- U16.06 <- U19.13 <- U19.15 <- BIRQ4" },//
				{ 6, 1, 1, 0, "IRQ5",
						"P8.46 <- J29.2 DATIN_1 <- CPLD2 <- U16.13 <- U16.07 <- U19.10 <- U19.12 <- BIRQ5" },//
				{ 6, 2, 1, 0, "IRQ6",
						"P8.43 <- J29.3 DATIN_2 <- CPLD2 <- U16.12 <- U16.08 <- U19.03 <- U19.01 <- BIRQ6" },//
				{ 6, 3, 1, 0, "IRQ7",
						"P8.44 <- J29.4 DATIN_3 <- CPLD2 <- U16.11 <- U16.09 <- U19.06 <- U19.04 <- BIRQ7" },//
				{ 6, 4, 1, 0, "DMR",
						"P8.41 <- J29.5 DATIN_4 <- CPLD2 <- U21.15 <- U21.05 <- U20.06 <- U20.04 <- BDMR" },//
				{ 6, 5, 1, 0, "IAKI",
						"P8.42 <- J29.6 DATIN_5 <- CPLD2 <- U16.17 <- U16.03 <- U14.10 <- U14.12 <- BIAKI" },//
				{ 6, 6, 1, 0, "DMGI",
						"P8.39 <- J29.7 DATIN_6 <- CPLD2 <- U21.17 <- U21.03 <- U20.10 <- U20.12 <- BDMGI" },//
				{ 6, 7, 1, 0, "SACK",
						"P8.40 <- J29.8 DATIN_7 <- CPLD2 <- U21.18 <- U21.02 <- U20.13 <- U20.15 <- BSACK" },//
//
// Register 7 not used
//

//
// End mark
//

				{ 0, 0, 0, 0, NULL, NULL } 	//
		};


// register signals to standard
// all outputs to standard:
// init state
void buslatches_c::setup() 
{
	// CPLD registers are all 8bit width, but not all input/outputs are
	// connected to bidirectional terminated QBUS lines.
	// see CPLD1+2 design!
	clear() ; // will not delete instances
	push_back(new buslatch_c(0, /*bitmask*/0xff)); // DAL<7..0>
	push_back(new buslatch_c(1, /*bitmask*/0xff)); // DAL<15..8>
	push_back(new buslatch_c(2, /*bitmask*/0xff)); // DAL<21..16>,BS7*,SYNC
	push_back(new buslatch_c(3, /*bitmask*/0x8f)); // read: SYNClatch,BS7*,WTBT*,REF*, write: cmds, 80=version
	push_back(new buslatch_c(4, /*bitmask*/0x7f)); // DATA control signals
	push_back(new buslatch_c(5, /*bitmask*/0x3f)); // system signals
	push_back(new buslatch_c(6, /*bitmask*/0xff)); // INTR/DMA
	push_back(new buslatch_c(7, /*bitmask*/0));
	for (unsigned i=0 ; i < BUSLATCHES_COUNT ; i++)
		assert(at(i)->addr == i) ; // will index by address

	// mask out bits which are not automatically testable
	// PRU manages SYNClatch for DAL,WTBT,BS7,REF
	at(2)->rw_bitmask = 0x3f ; // test BS7,SYNC via reg 4
	at(3)->rw_bitmask = 0 ; // r/w content unrelated, not testable
	at(5)->rw_bitmask = 0x1f ; // exclude SRUN
}

