/* iopageregister.h: Tables of implemented devices and their registers.
 Datastructures common to ARM and PRU.

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


 12-nov-2018  JH      entered Beta phase

 Implementation of QBUS/UNIBUS devices:

 A device is a piece of hardware with internal logic and a set of QBUS/UNIBUS registers.
 Device logic is implemented by ARM code as Linux user process.

 Device registers are dual ported:
 - used by ARM device logic.
 - accessed over QBUS/UNIBUS with DATI/DATO cycles.

 Two types of registers:
 - Some "passive" registers are simply memory cells, DATI/DATO does not change device logic state.
 - "active" registers trigger logic state changes (Example "GO" and CMD bit in TM11).

 Problem: if QBUS/UNIBUS accesses a  "hot" register
 - value must be read/written within 10us (QBUS/UNIBUS timeout)
 - a signal to ARM logic is generated. This causes Linux process context switch and further processing
 This must be completed before next QBUS/UNIBUS access to same device, but can take
 infinite time.
 Solution: MSYN-assert to SSYN-assert time must be fast, else timeout.
 At end of cycle MSYN-negate to SSYN-negate can be slow.
 -> keep SSYN asserted until ARM logic complete

 Events:
 PRU generated for certain register acccess an  event "afterread" or "afterwrite".

 In detail: if a iopage register is "hot":
 QBUS/UNIBUS - READ (PRU)
 on SSYN assert: return "value" field, (fast and uncoditionally)
 raise "AfterDATI"
 wait until ARM clears event (ACK)
 wait until MSYN is negate /much much faster than ARM completes
 negate SSYN

 Logic-READ (ARM)
 - wait in eventhtread until event is raised
 - get register, controller and DATI flag
 - route event to controller
 - change controller state, write new values into register set.
 - perhaps schedule DMA and INTR

 QBUS/UNIBUS-WRITE (PRU)
 MSYN asserted
 immediately copy value into register.value, assert SSYN
 raise "AFTER-DATO" event
 wait until ARM clears event (ACK)
 wait until MSYN is negated /much much faster than ARM completes
 negate SSYN


 Logic-WRITE
 wait in event thread until event is raised
 get register, controller and DATI/O flag
 route event to controller
 change controller state, write new values into register set.
 perhaps do DMA and INTR
 */

#ifndef _DEVICES_H_
#define _DEVICES_H_

#include <stdint.h>

/***** start of shared structs *****/
// on PRU. all struct are byte-packed, no "#pragma pack" there
// (support answer 20.5.2018,  issue CODEGEN-4832)
#ifdef ARM
#pragma pack(push,1)
#endif

#ifdef OLD
// range in which to respond to non-register DATI/DATO accesses
// entries in page table
#define PAGE_IGNORE	0	// UniBone does not respond to addresses in this page
#define PAGE_MEMORY	1	// UniBone emulates memory for this page
#define PAGE_IO		2	// addr in IO page, further check requires

#define PAGE_SIZE	8192	// 1 page = 8KB. (size of uiopage)
#define PAGE_COUNT	32	// 256KB/8 = 32 entries

#define PAGE_TABLE_ENTRY(pru_iopage_registers,addr)	( (pru_iopage_registers).pagetable[(addr) >> 13] )
#endif

// convert a full 16/18/22bit byte address to index# of word adress in the IO page
// examples: 760000 -> 0, 760002 -> 1, ... 777776 -> 4095
#define IOPAGE_REGISTER_ENTRY(pru_iopage_registers,addr)	\
	(pru_iopage_registers).register_handles[((addr)& 017777)/ 2]

// # of device registers supported. Limited by PRU RAM size and handle type = uint8_t
#define MAX_IOPAGE_REGISTER_COUNT	0xff	// valid handles 1..0xfe
// this IOpage address is implemented as ROM, backed by DDR RAM
#define IOPAGE_REGISTER_HANDLE_ROM	0xff

#define MAX_IOPAGE_REGISTERS_PER_DEVICE MAX_IOPAGE_REGISTER_COUNT
// #define MAX_IOPAGE_REGISTERS_PER_DEVICE 32 // RK611 has the most?


#define MAX_DEVICE_HANDLE	255 // 0 not used, must fit in 8bits

// Bitmask: Create event for iopageregister DATI/DATO access ?
#define IOPAGEREGISTER_EVENT_FLAG_DATI	0x01
#define IOPAGEREGISTER_EVENT_FLAG_DATO	0x02

// register descriptor used by PRU for direct high-speed QBUS/UNIBUS DATI/DATO access
typedef struct {
	uint16_t value; // QBUS/UNIBUS-visible register content
	// if "active" register with cmd/status bits (instead of pure memory);
	// it must *always* contain the content for the next DATI without
	// further state processing.

	uint16_t reset_value; // PRU sets "value := reset_value" on INIT

	// QBUS/UNIBUS DATO can be restricted to certain bits.
	// special cases:
	// 0x0000: register is ROM/read only
	// 0xffff: register is 16bits read/write
	uint16_t writable_bits; // 1 = bit can be written, 0 = remains as preset



	// EVENT to PRU, data to route event to
	// static set up by controller logic on "install()"
	uint8_t event_register_handle; // accesses device register
	uint8_t event_flags; // Bit-OR of IOPAGEREGISTER_EVEMT_FLAG_*

	// Important: record struct must have size of "power of 2"
	// Else indexing the record array requires multiplication,
	// which can last 4,6 us !!!
	// uint8_t dummy[2]; // fill up to 2+2+1+1+2 = 8 byte record size
} pru_iopage_register_t;

typedef struct {
	/* The whole memory range is segmented into 
	 * a single contniuous range of emulated memory (maybe 0)
	 * and the IOpage (8K)
	 */
	uint32_t	memory_start_addr ; // start of emulateded memory
	uint32_t	memory_limit_addr ; // first address after emulated memory, 0 = disable emulation

	uint32_t	iopage_start_addr ; // 0160000, 0760000, 017760000
	
	// iopage address table:
	// for every even address in the IOPage:
	// handle of a device register descriptor,
	// handle == 0 if address not used
	// handle == 0xff if address is ROM
	uint8_t register_handles[0x1000]; // 4k entries for 8k bytes

	// array of 254 register, indexed by register handles
	// handle 0 not used, 0xff = ROM=>DDR RAM
 	pru_iopage_register_t registers[MAX_IOPAGE_REGISTER_COUNT];
	// sizeof(pru_iopage_register_t) == "power of 2", index calculation!

} pru_iopage_registers_t;
// must fit in 8K PRU0 RAM

#ifdef ARM
#pragma pack(pop)
#endif
/***** end of shared structs *****/

#ifdef ARM
/********** included by ARM code *****************/

#ifndef _IOPAGEREGISTER_CPP_
extern volatile pru_iopage_registers_t *pru_iopage_registers;
#endif

int iopageregisters_connect(void);
void iopageregisters_init(void);
void iopageregisters_print_tables(void);

#else
/********** included by PRU code *****************/

#ifndef _IOPAGEREGISTER_C_
// not volatile
extern pru_iopage_registers_t pru_iopage_registers;
#endif

uint8_t emulated_addr_read(uint32_t addr, uint16_t *w);
uint8_t emulated_addr_write_w(uint32_t addr, uint16_t b);
uint8_t emulated_addr_write_b(uint32_t addr, uint8_t w);
void iopageregisters_reset_values(void) ;
void iopageregisters_init(void);

#endif

#endif // _IOPAGEREGISTER_H_
