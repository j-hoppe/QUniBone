/* buslatches_u.cpp: PRU GPIO multiplier latches on UniBone PCB.

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
#include "buslatches.hpp"
#include "pru.hpp"

buslatches_c buslatches; // Singleton


/* return a string with board signal path for an UNIBUS/QBUS signal
 * used as error info for loopback failures
 */
buslatches_wire_info_t buslatches_wire_info[] = { //
//
		// Register 0 write (PRU -> 74LS377 -> DS8641)
				{ 0, 0, 0, 1, "BG4_OUT",
						"P9.31 -> J10.1 DATOUT_0 -> U05.03 -> U05.02 -> U08.14 -> U08.15 -> BG4_OUT" },	//
				{ 0, 1, 0, 1, "BG5_OUT",
						"P9.29 -> J10.2 DATOUT_1 -> U05.04 -> U05.05 -> U08.11 -> U08.12 -> BG5_OUT" },	//
				{ 0, 2, 0, 1, "BG6_OUT",
						"P9.30 -> J10.3 DATOUT_2 -> U05.07 -> U05.06 -> U08.02 -> U08.01 -> BG6_OUT" },	//
				{ 0, 3, 0, 1, "BG7_OUT",
						"P9.28 -> J10.4 DATOUT_3 -> U05.08 -> U05.09 -> U08.05 -> U08.04 -> BG7_OUT" },	//
				{ 0, 4, 0, 1, "NPG_OUT",
						"P9.42 -> J10.5 DATOUT_4 -> U05.13 -> U05.12 -> U07.14 -> U07.15 -> NPG_OUT" },	//
				{ 0, 5, 0, 0, "n.c.",
						"P9.27 -> J10.6 DATOUT_5 -> U05.14 -> U05.15 -> U07.11 -> U07.12 -> n.c." },//
				{ 0, 6, 0, 0, "n.c.",
						"P9.41 -> J10.7 DATOUT_6 -> U05.17 -> U05.16 -> U07.02 -> U07.01 -> n.c." },//
				{ 0, 7, 0, 0, "n.c.",
						"P9.25 -> J10.8 DATOUT_7 -> U05.18 -> U05.19 -> U07.05 -> U07.04 -> n.c." },//
//
// Register 0 read (PRU <- 74LVTH541 <- 74LS244)
				{ 0, 0, 1, 0, "BG4_IN",
						"P8.45 <- J17.1 DATIN_0 <- U04.18 <- U04.02 <- U06.18 <- U06.02 <- BG4_IN" },//
				{ 0, 1, 1, 0, "BG5_IN",
						"P8.46 <- J17.2 DATIN_1 <- U04.17 <- U04.03 <- U06.16 <- U06.04 <- BG5_IN" },//
				{ 0, 2, 1, 0, "BG6_IN",
						"P8.43 <- J17.3 DATIN_2 <- U04.16 <- U04.04 <- U06.14 <- U06.06 <- BG6_IN" },//
				{ 0, 3, 1, 0, "BG7_IN",
						"P8.44 <- J17.4 DATIN_3 <- U04.15 <- U04.05 <- U06.12 <- U06.08 <- BG7_IN" },//
				{ 0, 4, 1, 0, "NPG_IN",
						"P8.41 <- J17.5 DATIN_4 <- U04.14 <- U04.06 <- U06.09 <- U06.11 <- NPG_IN" },//
				{ 0, 5, 1, 0, "LTC",
						"P8.42 <- J17.6 DATIN_5 <- U04.13 <- U04.07 <- U06.07 <- U06.13 <- LTC" },//
				{ 0, 6, 1, 0, "n.c.",
						"P8.39 <- J17.7 DATIN_6 <- U04.12 <- U04.08 <- U06.05 <- U06.15 <- n.c." },	//
				{ 0, 7, 1, 0, "n.c.",
						"P8.40 <- J17.8 DATIN_7 <- U04.11 <- U04.09 <- U06.03 <- U06.13 <- n.c." },	//
//
// Register 1 write  (PRU -> 74LS377 -> DS8641)
				{ 1, 0, 0, 0, "BR4",
						"P9.31 -> J10.1 DATOUT_0 -> U30.03 -> U30.02 -> U31.14 -> U31.15 -> BR4" },	//
				{ 1, 1, 0, 0, "BR5",
						"P9.29 -> J10.2 DATOUT_1 -> U30.04 -> U30.05 -> U31.11 -> U31.12 -> BR5" },	//
				{ 1, 2, 0, 0, "BR6",
						"P9.30 -> J10.3 DATOUT_2 -> U30.07 -> U30.06 -> U31.02 -> U31.01 -> BR6" },	//
				{ 1, 3, 0, 0, "BR7",
						"P9.28 -> J10.4 DATOUT_3 -> U30.08 -> U30.09 -> U31.05 -> U31.04 -> BR7" },	//
				{ 1, 4, 0, 0, "NPR",
						"P9.42 -> J10.5 DATOUT_4 -> U30.13 -> U30.12 -> U32.14 -> U32.15 -> NPR" },	//
				{ 1, 5, 0, 0, "SACK",
						"P9.27 -> J10.6 DATOUT_5 -> U30.14 -> U30.15 -> U32.11 -> U32.12 -> SACK" },//
				{ 1, 6, 0, 0, "BBSY",
						"P9.41 -> J10.7 DATOUT_6 -> U30.17 -> U30.16 -> U32.02 -> U32.01 -> BBSY" },//
				{ 1, 7, 0, 0, "n.c.",
						"P9.25 -> J10.8 DATOUT_7 -> U30.18 -> U30.19 -> U32.05 -> U32.04 -> n.c." },//
//
// Register 1 read (PRU <- 74LVTH541 <- DS8641)
				{ 1, 0, 1, 0, "BR4",
						"P8.45 <- J17.1 DATIN_0 <- U29.18 <- U29.02 <- U31.13 <- U31.15 <- BR4" },//
				{ 1, 1, 1, 0, "BR5",
						"P8.46 <- J17.2 DATIN_1 <- U29.17 <- U29.03 <- U31.10 <- U31.12 <- BR5" },//
				{ 1, 2, 1, 0, "BR6",
						"P8.43 <- J17.3 DATIN_2 <- U29.16 <- U29.04 <- U31.03 <- U31.01 <- BR6" },//
				{ 1, 3, 1, 0, "BR7",
						"P8.44 <- J17.4 DATIN_3 <- U29.15 <- U29.05 <- U31.06 <- U31.04 <- BR7" },//
				{ 1, 4, 1, 0, "NPR",
						"P8.41 <- J17.5 DATIN_4 <- U29.14 <- U29.06 <- U32.13 <- U32.15 <- NPR" },//
				{ 1, 5, 1, 0, "SACK",
						"P8.42 <- J17.6 DATIN_5 <- U29.13 <- U29.07 <- U32.10 <- U32.12 <- SACK" },	//
				{ 1, 6, 1, 0, "BBSY",
						"P8.39 <- J17.7 DATIN_6 <- U29.12 <- U29.08 <- U32.03 <- U32.01 <- BBSY" },	//
				{ 1, 7, 1, 0, "n.c.",
						"P8.40 <- J17.8 DATIN_7 <- U29.11 <- U29.09 <- U32.06 <- U32.04 <- n.c." },	//
//
// Register 2 write  (PRU -> 74LS377 -> DS8641)
				{ 2, 0, 0, 0, "A00",
						"P9.31 -> J10.1 DATOUT_0 -> U10.03 -> U10.02 -> U11.14 -> U11.15 -> A00" },	//
				{ 2, 1, 0, 0, "A01",
						"P9.29 -> J10.2 DATOUT_1 -> U10.04 -> U10.05 -> U11.11 -> U11.12 -> A01" },	//
				{ 2, 2, 0, 0, "A02",
						"P9.30 -> J10.3 DATOUT_2 -> U10.07 -> U10.06 -> U11.02 -> U11.01 -> A02" },	//
				{ 2, 3, 0, 0, "A03",
						"P9.28 -> J10.4 DATOUT_3 -> U10.08 -> U10.09 -> U11.05 -> U11.04 -> A03" },	//
				{ 2, 4, 0, 0, "A04",
						"P9.42 -> J10.5 DATOUT_4 -> U10.13 -> U10.12 -> U12.14 -> U12.15 -> A04" },	//
				{ 2, 5, 0, 0, "A05",
						"P9.27 -> J10.6 DATOUT_5 -> U10.14 -> U10.15 -> U12.11 -> U12.12 -> A05" },	//
				{ 2, 6, 0, 0, "A06",
						"P9.41 -> J10.7 DATOUT_6 -> U10.17 -> U10.16 -> U12.02 -> U12.01 -> A06" },	//
				{ 2, 7, 0, 0, "A07",
						"P9.25 -> J10.8 DATOUT_7 -> U10.18 -> U10.19 -> U12.05 -> U12.04 -> A07" },	//
//
// Register 2 read (PRU <- 74LVTH541 <- DS8641)
				{ 2, 0, 1, 0, "A00",
						"P8.45 <- J17.1 DATIN_0 <- U09.18 <- U09.02 <- U11.13 <- U11.15 <- A00" },//
				{ 2, 1, 1, 0, "A01",
						"P8.46 <- J17.2 DATIN_1 <- U09.17 <- U09.03 <- U11.10 <- U11.12 <- A01" },//
				{ 2, 2, 1, 0, "A02",
						"P8.43 <- J17.3 DATIN_2 <- U09.16 <- U09.04 <- U11.03 <- U11.01 <- A02" },//
				{ 2, 3, 1, 0, "A03",
						"P8.44 <- J17.4 DATIN_3 <- U09.15 <- U09.05 <- U11.06 <- U11.04 <- A03" },//
				{ 2, 4, 1, 0, "A04",
						"P8.41 <- J17.5 DATIN_4 <- U09.14 <- U09.06 <- U12.13 <- U12.15 <- A04" },//
				{ 2, 5, 1, 0, "A05",
						"P8.42 <- J17.6 DATIN_5 <- U09.13 <- U09.07 <- U12.10 <- U12.12 <- A05" },//
				{ 2, 6, 1, 0, "A06",
						"P8.39 <- J17.7 DATIN_6 <- U09.12 <- U09.08 <- U12.03 <- U12.01 <- A06" },//
				{ 2, 7, 1, 0, "A07",
						"P8.40 <- J17.8 DATIN_7 <- U09.11 <- U09.09 <- U12.06 <- U12.04 <- A07" },//
//
// Register 3 write  (PRU -> 74LS377 -> DS8641)
				{ 3, 0, 0, 0, "A08",
						"P9.31 -> J10.1 DATOUT_0 -> U14.03 -> U14.02 -> U15.14 -> U15.15 -> A08" },	//
				{ 3, 1, 0, 0, "A09",
						"P9.29 -> J10.2 DATOUT_1 -> U14.04 -> U14.05 -> U15.11 -> U15.12 -> A09" },	//
				{ 3, 2, 0, 0, "A10",
						"P9.30 -> J10.3 DATOUT_2 -> U14.07 -> U14.06 -> U15.02 -> U15.01 -> A10" },	//
				{ 3, 3, 0, 0, "A11",
						"P9.28 -> J10.4 DATOUT_3 -> U14.08 -> U14.09 -> U15.05 -> U15.04 -> A11" },	//
				{ 3, 4, 0, 0, "A12",
						"P9.42 -> J10.5 DATOUT_4 -> U14.13 -> U14.12 -> U16.14 -> U16.15 -> A12" },	//
				{ 3, 5, 0, 0, "A13",
						"P9.27 -> J10.6 DATOUT_5 -> U14.14 -> U14.15 -> U16.11 -> U16.12 -> A13" },	//
				{ 3, 6, 0, 0, "A14",
						"P9.41 -> J10.7 DATOUT_6 -> U14.17 -> U14.16 -> U16.02 -> U16.01 -> A14" },	//
				{ 3, 7, 0, 0, "A15",
						"P9.25 -> J10.8 DATOUT_7 -> U14.18 -> U14.19 -> U16.05 -> U16.04 -> A15" },	//
//
// Register 3 read (PRU <- 74LVTH541 <- DS8641)
				{ 3, 0, 1, 0, "A08",
						"P8.45 <- J17.1 DATIN_0 <- U13.18 <- U13.02 <- U15.13 <- U15.15 <- A08" },//
				{ 3, 1, 1, 0, "A09",
						"P8.46 <- J17.2 DATIN_1 <- U13.17 <- U13.03 <- U15.10 <- U15.12 <- A09" },//
				{ 3, 2, 1, 0, "A10",
						"P8.43 <- J17.3 DATIN_2 <- U13.16 <- U13.04 <- U15.03 <- U15.01 <- A10" },//
				{ 3, 3, 1, 0, "A11",
						"P8.44 <- J17.4 DATIN_3 <- U13.15 <- U13.05 <- U15.06 <- U15.04 <- A11" },//
				{ 3, 4, 1, 0, "A12",
						"P8.41 <- J17.5 DATIN_4 <- U13.14 <- U13.06 <- U16.13 <- U16.15 <- A12" },//
				{ 3, 5, 1, 0, "A13",
						"P8.42 <- J17.6 DATIN_5 <- U13.13 <- U13.07 <- U16.10 <- U16.12 <- A13" },//
				{ 3, 6, 1, 0, "A14",
						"P8.39 <- J17.7 DATIN_6 <- U13.12 <- U13.08 <- U16.03 <- U16.01 <- A14" },//
				{ 3, 7, 1, 0, "A15",
						"P8.40 <- J17.8 DATIN_7 <- U13.11 <- U13.09 <- U16.06 <- U16.04 <- A15" },//
//
// Register 4 write  (PRU -> 74LS377 -> DS8641)
				{ 4, 0, 0, 0, "A16",
						"P9.31 -> J10.1 DATOUT_0 -> U26.03 -> U26.02 -> U27.14 -> U27.15 -> A16" },	//
				{ 4, 1, 0, 0, "A17",
						"P9.29 -> J10.2 DATOUT_1 -> U26.04 -> U26.05 -> U27.11 -> U27.12 -> A17" },	//
				{ 4, 2, 0, 0, "C0",
						"P9.30 -> J10.3 DATOUT_2 -> U26.07 -> U26.06 -> U27.02 -> U27.01 -> C0" },//
				{ 4, 3, 0, 0, "C1",
						"P9.28 -> J10.4 DATOUT_3 -> U26.08 -> U26.09 -> U27.05 -> U27.04 -> C1" },//
				{ 4, 4, 0, 0, "MSYN",
						"P9.42 -> J10.5 DATOUT_4 -> U26.13 -> U26.12 -> U28.14 -> U28.15 -> MSYN" },//
				{ 4, 5, 0, 0, "SSYN",
						"P9.27 -> J10.6 DATOUT_5 -> U26.14 -> U26.15 -> U28.11 -> U28.12 -> SSYN" },//
				{ 4, 6, 0, 0, "n.c.",
						"P9.41 -> J10.7 DATOUT_6 -> U26.17 -> U26.16 -> U28.02 -> U28.01 -> n.c." },//
				{ 4, 7, 0, 0, "n.c.",
						"P9.25 -> J10.8 DATOUT_7 -> U26.18 -> U26.19 -> U28.05 -> U28.04 -> n.c." },//
//
// Register 4 read (PRU <- 74LVTH541 <- DS8641)
				{ 4, 0, 1, 0, "A16",
						"P8.45 <- J17.1 DATIN_0 <- U25.18 <- U25.02 <- U27.13 <- U27.15 <- A16" },//
				{ 4, 1, 1, 0, "A17",
						"P8.46 <- J17.2 DATIN_1 <- U25.17 <- U25.03 <- U27.10 <- U27.12 <- A17" },//
				{ 4, 2, 1, 0, "C0",
						"P8.43 <- J17.3 DATIN_2 <- U13.16 <- U13.04 <- U27.03 <- U27.01 <- C0" },//
				{ 4, 3, 1, 0, "C1",
						"P8.44 <- J17.4 DATIN_3 <- U13.15 <- U13.05 <- U27.06 <- U27.04 <- C1" },//
				{ 4, 4, 1, 0, "MSYN",
						"P8.41 <- J17.5 DATIN_4 <- U25.14 <- U25.06 <- U28.13 <- U28.15 <- MSYN" },	//
				{ 4, 5, 1, 0, "SSYN",
						"P8.42 <- J17.6 DATIN_5 <- U25.13 <- U25.07 <- U28.10 <- U28.12 <- SSYN" },	//
				{ 4, 6, 1, 0, "n.c.",
						"P8.39 <- J17.7 DATIN_6 <- U25.12 <- U25.08 <- U28.03 <- U28.01 <- n.c." },	//
				{ 4, 7, 1, 0, "n.c.",
						"P8.40 <- J17.8 DATIN_7 <- U25.11 <- U25.09 <- U28.06 <- U28.04 <- n.c." },	//
//
// Register 5 write  (PRU -> 74LS377 -> DS8641)
				{ 5, 0, 0, 0, "D00",
						"P9.31 -> J10.1 DATOUT_0 -> U18.03 -> U18.02 -> U19.14 -> U19.15 -> D00" },	//
				{ 5, 1, 0, 0, "D01",
						"P9.29 -> J10.2 DATOUT_1 -> U18.04 -> U18.05 -> U19.11 -> U19.12 -> D01" },	//
				{ 5, 2, 0, 0, "D02",
						"P9.30 -> J10.3 DATOUT_2 -> U18.07 -> U18.06 -> U19.02 -> U19.01 -> D02" },	//
				{ 5, 3, 0, 0, "D03",
						"P9.28 -> J10.4 DATOUT_3 -> U18.08 -> U18.09 -> U19.05 -> U19.04 -> D03" },	//
				{ 5, 4, 0, 0, "D04",
						"P9.42 -> J10.5 DATOUT_4 -> U18.13 -> U18.12 -> U20.14 -> U20.15 -> D04" },	//
				{ 5, 5, 0, 0, "D05",
						"P9.27 -> J10.6 DATOUT_5 -> U18.14 -> U18.15 -> U20.11 -> U20.12 -> D05" },	//
				{ 5, 6, 0, 0, "D06",
						"P9.41 -> J10.7 DATOUT_6 -> U18.17 -> U18.16 -> U20.02 -> U20.01 -> D06" },	//
				{ 5, 7, 0, 0, "D07",
						"P9.25 -> J10.8 DATOUT_7 -> U18.18 -> U18.19 -> U20.05 -> U20.04 -> D07" },	//
//
// Register 5 read (PRU <- 74LVTH541 <- DS8641)
				{ 5, 0, 1, 0, "D00",
						"P8.45 <- J17.1 DATIN_0 <- U17.18 <- U17.02 <- U19.13 <- U19.15 <- D00" },//
				{ 5, 1, 1, 0, "D01",
						"P8.46 <- J17.2 DATIN_1 <- U17.17 <- U17.03 <- U19.10 <- U19.12 <- D01" },//
				{ 5, 2, 1, 0, "D02",
						"P8.43 <- J17.3 DATIN_2 <- U17.16 <- U17.04 <- U19.03 <- U19.01 <- D02" },//
				{ 5, 3, 1, 0, "D03",
						"P8.44 <- J17.4 DATIN_3 <- U17.15 <- U17.05 <- U19.06 <- U19.04 <- D03" },//
				{ 5, 4, 1, 0, "D04",
						"P8.41 <- J17.5 DATIN_4 <- U17.14 <- U17.06 <- U20.13 <- U20.15 <- D04" },//
				{ 5, 5, 1, 0, "D05",
						"P8.42 <- J17.6 DATIN_5 <- U17.13 <- U17.07 <- U20.10 <- U20.12 <- D05" },//
				{ 5, 6, 1, 0, "D06",
						"P8.39 <- J17.7 DATIN_6 <- U17.12 <- U17.08 <- U20.03 <- U20.01 <- D06" },//
				{ 5, 7, 1, 0, "D07",
						"P8.40 <- J17.8 DATIN_7 <- U17.11 <- U17.09 <- U20.06 <- U20.04 <- D07" },//
//
// Register 6 write  (PRU -> 74LS377 -> DS8641)
				{ 6, 0, 0, 0, "D08",
						"P9.31 -> J10.1 DATOUT_0 -> U22.03 -> U22.02 -> U23.14 -> U23.15 -> D08" },	//
				{ 6, 1, 0, 0, "D09",
						"P9.29 -> J10.2 DATOUT_1 -> U22.04 -> U22.05 -> U23.11 -> U23.12 -> D09" },	//
				{ 6, 2, 0, 0, "D10",
						"P9.30 -> J10.3 DATOUT_2 -> U22.07 -> U22.06 -> U23.02 -> U23.01 -> D10" },	//
				{ 6, 3, 0, 0, "D11",
						"P9.28 -> J10.4 DATOUT_3 -> U22.08 -> U22.09 -> U23.05 -> U23.04 -> D11" },	//
				{ 6, 4, 0, 0, "D12",
						"P9.42 -> J10.5 DATOUT_4 -> U22.13 -> U22.12 -> U24.14 -> U24.15 -> D12" },	//
				{ 6, 5, 0, 0, "D13",
						"P9.27 -> J10.6 DATOUT_5 -> U22.14 -> U22.15 -> U24.11 -> U24.12 -> D13" },	//
				{ 6, 6, 0, 0, "D14",
						"P9.41 -> J10.7 DATOUT_6 -> U22.17 -> U22.16 -> U24.02 -> U24.01 -> D14" },	//
				{ 6, 7, 0, 0, "D15",
						"P9.25 -> J10.8 DATOUT_7 -> U22.18 -> U22.19 -> U24.05 -> U24.04 -> D15" },	//
//
// Register 6 read (PRU <- 74LVTH541 <- DS8641)
				{ 6, 0, 1, 0, "D08",
						"P8.45 <- J17.1 DATIN_0 <- U21.18 <- U21.02 <- U23.13 <- U23.15 <- D08" },//
				{ 6, 1, 1, 0, "D09",
						"P8.46 <- J17.2 DATIN_1 <- U21.17 <- U21.03 <- U23.10 <- U23.12 <- D09" },//
				{ 6, 2, 1, 0, "D10",
						"P8.43 <- J17.3 DATIN_2 <- U21.16 <- U21.04 <- U23.03 <- U23.01 <- D10" },//
				{ 6, 3, 1, 0, "D11",
						"P8.44 <- J17.4 DATIN_3 <- U21.15 <- U21.05 <- U23.06 <- U23.04 <- D11" },//
				{ 6, 4, 1, 0, "D12",
						"P8.41 <- J17.5 DATIN_4 <- U21.14 <- U21.06 <- U24.13 <- U24.15 <- D12" },//
				{ 6, 5, 1, 0, "D13",
						"P8.42 <- J17.6 DATIN_5 <- U21.13 <- U21.07 <- U24.10 <- U24.12 <- D13" },//
				{ 6, 6, 1, 0, "D14",
						"P8.39 <- J17.7 DATIN_6 <- U21.12 <- U21.08 <- U24.03 <- U24.01 <- D14" },//
				{ 6, 7, 1, 0, "D15",
						"P8.40 <- J17.8 DATIN_7 <- U21.11 <- U21.09 <- U24.06 <- U24.04 <- D15" },//
//
// Register 7 write  (PRU -> 74LS377 -> DS8641)
				{ 7, 0, 0, 0, "INTR",
						"P9.31 -> J10.1 DATOUT_0 -> U34.03 -> U34.02 -> U35.14 -> U35.15 -> INTR" },//
				{ 7, 1, 0, 0, "PA",
						"P9.29 -> J10.2 DATOUT_1 -> U34.04 -> U34.05 -> U35.11 -> U35.12 -> PA" },//
				{ 7, 2, 0, 0, "PB",
						"P9.30 -> J10.3 DATOUT_2 -> U34.07 -> U34.06 -> U35.02 -> U35.01 -> PB" },//
				{ 7, 3, 0, 0, "INIT",
						"P9.28 -> J10.4 DATOUT_3 -> U34.08 -> U34.09 -> U35.05 -> U35.04 -> INIT" },//
				{ 7, 4, 0, 0, "ACLO",
						"P9.42 -> J10.5 DATOUT_4 -> U34.13 -> U34.12 -> U36.14 -> U36.15 -> ACLO" },//
				{ 7, 5, 0, 0, "DCLO",
						"P9.27 -> J10.6 DATOUT_5 -> U34.14 -> U34.15 -> U36.11 -> U36.12 -> DCLO" },//
				{ 7, 6, 0, 0, "n.c.",
						"P9.41 -> J10.7 DATOUT_6 -> U34.17 -> U34.16 -> U36.02 -> U36.01 -> n.c." },//
				{ 7, 7, 0, 0, "n.c.",
						"P9.25 -> J10.8 DATOUT_7 -> U34.18 -> U34.19 -> U36.05 -> U36.04 -> n.c." },//
//
// Register 7 read  (PRU <- 74LVTH541 <- DS8641)
				{ 7, 0, 1, 0, "INTR",
						"P8.45 <- J17.1 DATIN_0 <- U33.18 <- U33.02 <- U35.13 <- U35.15 <- INTR" },	//
				{ 7, 1, 1, 0, "PA",
						"P8.46 <- J17.2 DATIN_1 <- U33.17 <- U33.03 <- U35.10 <- U35.12 <- PA" },//
				{ 7, 2, 1, 0, "PB",
						"P8.43 <- J17.3 DATIN_2 <- U33.16 <- U33.04 <- U35.03 <- U35.01 <- PB" },//
				{ 7, 3, 1, 0, "INIT",
						"P8.44 <- J17.4 DATIN_3 <- U33.15 <- U33.05 <- U35.06 <- U35.04 <- INIT" },	//
				{ 7, 4, 1, 0, "ACLO",
						"P8.41 <- J17.5 DATIN_4 <- U33.14 <- U33.06 <- U36.13 <- U36.15 <- ACLO" },	//
				{ 7, 5, 1, 0, "DCLO",
						"P8.42 <- J17.6 DATIN_5 <- U33.13 <- U33.07 <- U36.10 <- U36.12 <- DCLO" },	//
				{ 7, 6, 1, 0, "n.c.",
						"P8.39 <- J17.7 DATIN_6 <- U33.12 <- U33.08 <- U36.03 <- U36.01 <- n.c." },	//
				{ 7, 7, 1, 0, "n.c.",
						"P8.40 <- J17.8 DATIN_7 <- U33.11 <- U33.09 <- U36.06 <- U36.04 <- n.c." },	//
				{ 0, 0, 0, 0, NULL, NULL } 	// End mark
		};


// register signals to standard
// all outputs to standard:
// init state
void buslatches_c::setup() {
	// chips are all 8bit width, but not all input/outputs are
	// connected to bidirectional terminated UNIBUS lines.
	// see PCB schematic!
    clear() ;// will not delete instances

	push_back(new buslatch_c(0, /*bitmask*/0x1f)); // BG4567, NPG
// LTC on .6 ignored, is input only
	push_back(new buslatch_c(1, /*bitmask*/0x7f)); // BR4..BR7,NPR,SACK,BBSY
	push_back(new buslatch_c(2, /*bitmask*/0xff)); // addresses 0..7 ;
	push_back(new buslatch_c(3, /*bitmask*/0xff)); // addresses 8..15
	push_back(new buslatch_c(4, /*bitmask*/0x3f)); // A16,17,C0,C1, MSYN,SSYN
	push_back(new buslatch_c(5, /*bitmask*/0xff)); // data 0..7
	push_back(new buslatch_c(6, /*bitmask*/0xff)); // data 8..15
	push_back(new buslatch_c(7, /*bitmask*/0x3f)); // INTR,PA,PB,INIT,ACLO,DCLO
	for (unsigned i=0 ; i < BUSLATCHES_COUNT ; i++)
		assert(at(i)->addr == i) ; // will index by address
//  BG4567, NPG are read back non inverted from UNIBUS
	at(0)->read_inverted = true;
}


