/* iopageregister.cpp: handle ARM-PRU shared struct with device register descriptors

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

#define _IOPAGEREGISTER_CPP_

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "pru.hpp"

#include "qunibus.h"
#include "iopageregister.h"

// Device register struct shared between PRU and ARM.
// Place section with register struct at begin of 8K PRU_DMEM_1_0.
volatile pru_iopage_registers_t *pru_iopage_registers;

int iopageregisters_connect(void) {
	void *pru_shared_dataram;
	// get pointer to RAM
	if (prussdrv_map_prumem(PRU_DEVICEREGISTER_RAM_ID, &pru_shared_dataram)) {
		fprintf(stderr, "prussdrv_map_prumem() failed\n");
		return -1;

	}
	// prussdrv_map_prumem( PRU_MAILBOX_RAM_ID, &pru_shared_dataram) ;
	// point to struct inside RAM
	pru_iopage_registers = (pru_iopage_registers_t *) ((uint8_t *) pru_shared_dataram
			+ PRU_DEVICEREGISTER_RAM_OFFSET);

	// now ARM and PRU can access the device register descriptors

	return 0;
}

//  initialize register tables to "empty"
void iopageregisters_init() {
	qunibus->assert_addr_width() ; // address width already known, bus sizing done
	assert(qunibus->iopage_start_addr) ;

	// clear the pagetable: no address emulated, only IO page marked
	pru_iopage_registers->iopage_start_addr = qunibus->iopage_start_addr ;
	pru_iopage_registers->memory_start_addr = 0 ;
	pru_iopage_registers->memory_limit_addr = 0 ; // no mem emulation

  	// clear the iopage addr map: no register assigned
	memset((void *) pru_iopage_registers->register_handles, 0,
			sizeof(pru_iopage_registers->register_handles));
}

void iopageregisters_print_tables(void) {
	unsigned i, n;
	uint32_t addr;

	
	printf("Start of IO page: %s\n", qunibus->addr2text(pru_iopage_registers->iopage_start_addr));
	if (pru_iopage_registers->memory_limit_addr == 0)
		printf("  No memory emulation.\n") ;
	else 
		printf("  Memory emulation in range %s..%s (excluding).\n", 
		qunibus->addr2text(pru_iopage_registers->memory_start_addr),qunibus->addr2text(pru_iopage_registers->memory_limit_addr)) ;
	
	printf("\n");
	printf("IO page register table:");
	n = 0; // counts valid registers
	for (addr = qunibus->iopage_start_addr; addr < qunibus->addr_space_byte_count; addr += 2) {
		uint8_t reghandle;
		i = (addr - qunibus->iopage_start_addr) / 2; // register handle
		reghandle = pru_iopage_registers->register_handles[i];
		if (reghandle != 0) {
			if ((n % 4) == 0) // 4 in a row
				printf("\n");
			printf("  [%3d]@%s = 0x%02x    ", i, qunibus->addr2text(addr), (unsigned) reghandle);
			n++;
		}
	}
	if (n == 0)
		printf("  no registers defined.");
	printf("\n");
}

