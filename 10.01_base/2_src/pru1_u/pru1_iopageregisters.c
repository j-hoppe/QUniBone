/* pru1_iopageregisters.c: handle UNIBUS behaviour of emulated devices

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

#define _IOPAGEREGISTERS_C_

#include <string.h>
#include <stdint.h>

#include "pru1_utils.h"
#include "pru1_buslatches.h" // PRU_DEBUG_PIN0
#include "mailbox.h"
#include "iopageregister.h"
#include "ddrmem.h"

// place section with register struct at begin of 8K PRU_DMEM_1_0.
// see linker command file
#pragma DATA_SECTION(pru_iopage_registers,".deviceregisters_sec")

// not volatile: data seldom changed by ARM, speed matters!
pru_iopage_registers_t pru_iopage_registers;

/* request value from a device register
 * page_table_entry already calculated for addr
 * may have side effects onto other registers!
 * result 1 = sucessful, 0 = register not implemented
 * for "active" registers:
 * put prepared register value onto bus, then set mailbox event to ARM
 * for post processing. SSYN must remain asserted until ARM is complete
 */
uint8_t emulated_addr_read(uint32_t addr, uint16_t *val) {
	if (addr < pru_iopage_registers.memory_limit_addr && addr >= pru_iopage_registers.memory_start_addr) {
		// speed priority on memory access: test for end_addr first
		// addr in allowed memory range, not in I/O page
		*val = DDRMEM_MEMGET_W(addr);
		return 1;
	} else if (addr >= pru_iopage_registers.iopage_start_addr) {
		uint8_t reghandle;
		reghandle = IOPAGE_REGISTER_ENTRY(pru_iopage_registers, addr);
		if (reghandle == 0) {
			return 0; // register not implemented as "active"
		} else if (reghandle == IOPAGE_REGISTER_HANDLE_ROM) {
			*val = DDRMEM_MEMGET_W(addr);
			return 1;
		} else {
			// return register value. remove "volatile" attribute
			// indexing this records takes 4,6 us, if record size != 8
			pru_iopage_register_t *reg = (pru_iopage_register_t *) &(pru_iopage_registers.registers[reghandle]); // alias
			*val = reg->value;
			if (reg->event_flags & IOPAGEREGISTER_EVENT_FLAG_DATI)
				DO_EVENT_DEVICEREGISTER(reg, QUNIBUS_CYCLE_DATI, addr, *val);
			// ARM is clearing this, while SSYN asserted, so no concurrent next bus cycle.
			// no concurrent ARP+PRU access

			return 1;
		}
	} else
		return 0;
}

/* write value into a device register
 * may have side effects onto other registers!
 * result 1 = sucessful, 0 = not implemented
 * may set mailbox event to ARM, then SSYN must remain asserted until ARM is complete
 */
uint8_t emulated_addr_write_w(uint32_t addr, uint16_t w) {
	if (addr < pru_iopage_registers.memory_limit_addr && addr >= pru_iopage_registers.memory_start_addr) {
		// speed priority on memory access: test for end_addr first
		// addr in allowed memory range, not in I/O page
   		// no check wether addr is even (A00=0)
		// write 16 bits
		DDRMEM_MEMSET_W(addr, w);
		return 1;
	} else if (addr >= pru_iopage_registers.iopage_start_addr) {
		uint8_t reghandle = IOPAGE_REGISTER_ENTRY(pru_iopage_registers, addr);
		if (reghandle == 0) {
			return 0; // register not implemented
		} else if (reghandle == IOPAGE_REGISTER_HANDLE_ROM) {
			return 0; // ROM does not respond to DATO
		} else {
			// change register value
			pru_iopage_register_t *reg = (pru_iopage_register_t *) &(pru_iopage_registers.registers[reghandle]); // alias
			uint16_t reg_val = (reg->value & ~reg->writable_bits) | (w & reg->writable_bits);
			reg->value = reg_val;
			if (reg->event_flags & IOPAGEREGISTER_EVENT_FLAG_DATO)
				DO_EVENT_DEVICEREGISTER(reg, QUNIBUS_CYCLE_DATO, addr, reg_val);
			return 1;
		}
	} else
		return 0;
}

uint8_t emulated_addr_write_b(uint32_t addr, uint8_t b) {
	if (addr < pru_iopage_registers.memory_limit_addr && addr >= pru_iopage_registers.memory_start_addr) {
		// speed priority on memory access: test for end_addr first
		// addr in allowed memory range, not in I/O page
		DDRMEM_MEMSET_B(addr, b);
		return 1;
	} else if (addr >= pru_iopage_registers.iopage_start_addr) {
		uint8_t reghandle = IOPAGE_REGISTER_ENTRY(pru_iopage_registers, addr);
		if (reghandle == 0) {
			return 0; // register not implemented
		} else if (reghandle == IOPAGE_REGISTER_HANDLE_ROM) {
			return 0; // ROM does not respond to DATOB
		} else {
			// change register value
			pru_iopage_register_t *reg = (pru_iopage_register_t *) &(pru_iopage_registers.registers[reghandle]); // alias
			uint16_t reg_val;
			if (addr & 1)  // odd address = write upper byte
				reg_val = (reg->value & 0x00ff) // don't touch lower byte
				| (reg->value & ~reg->writable_bits & 0xff00) // protected upper byte bits
						| (((uint16_t) b << 8) & reg->writable_bits); // changed upper byte bits
			else
				// even address: write lower byte
				reg_val = (reg->value & 0xff00) // don' touch upper byte
				| (reg->value & ~reg->writable_bits & 0x00ff) // protected upper byte bits
						| (b & reg->writable_bits); // changed lower byte bits
			reg->value = reg_val;
			if (reg->event_flags & IOPAGEREGISTER_EVENT_FLAG_DATO)
				DO_EVENT_DEVICEREGISTER(reg, QUNIBUS_CYCLE_DATOB, addr, reg_val);
			return 1;
		}
	} else
		return 0;

}


// reset all registers to their "reset_value"
// Todo as fast response to INIT bus signal
// 7,6 usecs for 256 loop
void iopageregisters_reset_values() {
	unsigned i ;
	pru_iopage_register_t *regptr = pru_iopage_registers.registers ;
	for (i=0 ; i < MAX_IOPAGE_REGISTER_COUNT ; i++, regptr++)
		regptr->value = regptr->reset_value ;
}


//
void iopageregisters_init() {
	// clear the pagetable: no address emulated, no device register defined
	memset((void *) &pru_iopage_registers, 0, sizeof(pru_iopage_registers));
}
