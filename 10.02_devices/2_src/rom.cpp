/* rom.cpp - emulate a ROM in QBUS/UNIBUS IO page

 Copyright (c) 2020, Joerg Hoppe, j_hoppe@t-online.de, www.retrocmp.com

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

 7-feb-2020   JH      begin

 A ROM is implemented by DDR memory, which is accessed by PRU
 if pru_iopage_registers_t.register_handles[addr] == 0xff

 State:
 Code in a ROM can be loaded from a disk file (MACRO11 listing),
 a map with code labels is managed.
 A loaded ROM can be relocated (neeed for M9312 boot ROMS, which are all 173000)
 A loaded ROM can be installed to QBUS/UNIBUS (by setting PRU maps)
 */

#include "logger.hpp"
#include "qunibusadapter.hpp"
#include "ddrmem.h"
#include "rom.hpp"

// fill from file
// uses global memory buffer "membuffer"

// size if ROM in 16bit words
rom_c::rom_c(string _name, unsigned _wordcount, uint32_t _baseaddress) {
	log_label = "ROM";

	name = _name;
	wordcount = _wordcount;
	baseaddress = _baseaddress;
	cells.reserve(_wordcount);
}

// fill ROM with pattern
void rom_c::fill(uint16_t value) {
	for (unsigned i = 0; i < wordcount; i++)
		cells[i] = value;
	codelabels.clear() ; // no code in ROM now
}

bool rom_c::load_macro11_listing(const char *fname) {
	bool load_ok;
	uint32_t lastaddr;

	fill(0xffff); // "empty" pattern

	membuffer->init();
	load_ok = membuffer->load_macro11_listing(fname, &codelabels);
	if (load_ok) {
		// move data to intern array. 
		// fill undefined data with 0xff.
		// error, if file defines more data then base_addr+size
		membuffer->get_addr_range(&baseaddress, &lastaddr);
		unsigned loaded_words = (lastaddr - baseaddress) / 2 + 1;

		if (loaded_words > wordcount) {
			ERROR("Data overflow: file \"%s\" contains %u words, ROM size is %u.", fname,
					loaded_words, wordcount);
			load_ok = false;
		}
	}

	if (load_ok) {
		// copy loaded words into ROM cells[]
		for (unsigned i = 0; i < wordcount; i++) {
			uint32_t addr = baseaddress + 2 * i;
			if (membuffer->is_valid(addr))
				cells[i] = membuffer->get_word(addr);
		}
	}

	return load_ok;
}

void rom_c::dump(FILE *f) {
	unsigned addr;
	fprintf(f, "Data of ROM \"%s\", baseaddress=%s, wordcount=%u:\n", name.c_str(),
			qunibus->addr2text(baseaddress), wordcount);
	for (addr = baseaddress; addr < baseaddress + 2 * wordcount; addr += 2)
		fprintf(f, "%06o %06o\n", addr, get_data(addr));
	codelabels.print(stderr);
}

// get a cell, or exception
uint16_t rom_c::get_data(uint32_t addr) {
	if (addr < baseaddress || (addr >= baseaddress + 2 * wordcount)) {
		ERROR("get_data(): illegal address %s", qunibus->addr2text(addr));
		return 0;
	}
	return cells[(addr - baseaddress) / 2];
}

// set a cell
void rom_c::set_data(uint32_t addr, uint16_t value) {
	if (addr < baseaddress || (addr >= baseaddress + 2 * wordcount)) {
		ERROR("set_data(): illegal address %s", qunibus->addr2text(addr));
		return;
	}
	cells[(addr - baseaddress) / 2] = value;
}

// move base address, correct code label map
// must not be installed on QBUS/UNIBUS !
void rom_c::relocate(uint32_t new_base_addr) {
	if (new_base_addr + 2 * wordcount > qunibus->addr_space_byte_count) {
		ERROR("Relocation to %s would exceed addressrange at %06o", qunibus->addr2text(new_base_addr), qunibus->addr_space_byte_count);
		return;
	}
	int delta = new_base_addr - baseaddress;
	baseaddress = new_base_addr;

	codelabels.relocate(delta);
}

// implemement on QBUS/UNIBUS
void rom_c::install(void) {
	// check whether all cells are in IOpage

	// register onto QBUS/UNIBUS IOpage
	// if a IOpage address is already marked as "device register"
	// that address is silently NOT defined to be a ROM
	// so ROM ranges may be superseded with registers 
	// (M9312, 773024/26)
	for (unsigned i = 0; i < wordcount; i++) {
		uint32_t addr = baseaddress + 2 * i;
		// set value of ROM cell in background DDR RAM
		if (!ddrmem->iopage_deposit(addr, cells[i]))
			FATAL("setting IOpage background DDR RAM failed");
		// set QBUS/UNIBUS address as "ROM", to access DDR mem cell
		if (!qunibusadapter->register_rom(addr))
			break; // error
	}
}

// remove from QBUS/UNIBUS
void rom_c::uninstall(void) {
	for (unsigned i = 0; i < wordcount; i++) {
		uint32_t addr = baseaddress + 2 * i;
		qunibusadapter->unregister_rom(addr);
	}
}

