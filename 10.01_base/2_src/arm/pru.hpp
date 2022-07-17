/* pru.hpp: Management interface to PRU0 & PRU1.

 Copyright (c) 2018, Joerg Hoppe, j_hoppe@t-online.de, www.retrocmp.com

 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 - Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.

 - Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.

 - Neither the name of the copyright holder nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 18-apr-2019  JH      added PRU code dictionary
 12-nov-2018  JH      entered beta phase
 */

#ifndef _PRU_HPP_
#define _PRU_HPP_

#include <stdint.h>
#include "prussdrv.h"

#include "logsource.hpp"

/*** PRU Shared addresses ***/
// Mailbox page & offset in PRU internal shared 12 KB RAM
// Accessible by both PRUs, must be located in shared RAM
// offset 0 == addr 0x10000 in linker cmd files for PRU0 AND PRU1 projects.
// For use with prussdrv_map_prumem()
#define PRUSS_MAX_IRAM_SIZE 8192

// all entry addresses at 0
// code entry point "_c_int00_noinit_noargs" from linker map file:
#define PRU0_ENTRY_ADDR	0x00000000 
#define PRU1_ENTRY_ADDR	0x00000000

#ifndef PRU_MAILBOX_RAM_ID
#define PRU_MAILBOX_RAM_ID	PRUSS0_SHARED_DATARAM
#define PRU_MAILBOX_RAM_OFFSET	0
#endif

// Device register page & offset in PRU0 8KB RAM mapped into PRU1 space
// offset 0 == addr 0x2000 in linker cmd files for PRU1 projects.
// For use with prussdrv_map_prumem()
#ifndef PRU_DEVICEREGISTER_RAM_ID
#define PRU_DEVICEREGISTER_RAM_ID	PRUSS0_PRU0_DATARAM
#define PRU_DEVICEREGISTER_RAM_OFFSET	0
#endif

class pru_c: public logsource_c {
public:
	// IDs for code variants, so callers can select one
	enum prucode_enum {
		PRUCODE_EOD = 0, // special marker: end of dictionary
		PRUCODE_NONE = 0, // no code running, RPU reset
		PRUCODE_TEST = 1,	// only self-test functions
		PRUCODE_EMULATION = 2 // regular QBUS/UNIBUS operation
	// with or without physical CPU for arbitration
	};
public:
	enum prucode_enum prucode_id; // currently running code

	pru_c();
	int start(enum prucode_enum prucode_id);
	int stop(void);
};

extern pru_c *pru; // singleton

#endif
