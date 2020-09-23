/* m9312.cpp: Implementation of M9312 ROM bootstrap

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

 05-feb-2020	JH      begin


 M9312 has several functions:
 ROM:
 1 ROM chip is mapped into 165000..165776 (LOW ROM 256 words)
 4 smaller ROMs with bootloaders are mapped into
 773000.. 773176 (BOOT ROM #1)
 773200.. 773376 (BOOT ROM #2)
 773400.. 773576 (BOOT ROM #3)
 773600.. 773776 (BOOT ROM #4)
 All 5 ROMs can be loaded from disk files.

 Boot logic:
 M9312 can redirect the CPU power-up fetch of vector 24/26 into
 own ROM at 773024/26.
 773024 is the start PC, it is implemented by as a variable address set by
 an "offset switch bank".
 (DEC doc lists for every ROM several entry addresses as switch settings.)
 11/60 traps to 224/226, this is also handled.
 Boot vector redirection is implemneted by ORing UNIBUS ADDR lines with
 773000 after ACLO gets negated. The vector remains on the BUS
 - for only 300ms (so manual EXAM/DEPOSITs with HALTed CPU are not disturbed)
 - for the first two DATI cycles (which are expected to be CPU PC/PSW fetch)

  Diagnostic:
  XXDP ZM9BE0

 Implementation   :

 fetching trap_PC and trap_PSW:
 M9312 gets two pseudo read-only registers at 773024/26
 *24 is the PC, value generated via address switch setings
 *26 is PSW
 read accesses on *24 and *26 are only used to count the power-up MSYn cycles for vector fetch.


 ROM:
 ROM cells in the IOpage are marked with "register handle" 0xff,
 meaning "that address is not a register, but to be read from DDR RAM".
 Amount of implementable device registers shrinks by one to 254.

 */

#include <string.h>

#include "mailbox.h"
#include "utils.hpp"
#include "logger.hpp"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"	// definition of class device_c
#include "m9312.hpp"

m9312_c::m9312_c() :
		qunibusdevice_c()  // super class constructor
{
	// static config
	name.value = "M9312";
	type_name.value = "m9312_c";
	log_label = "m9312";

	set_default_bus_params(0773024, 31, 0, 0); // base addr, intr-vector, intr level
	// 11/60 vector
	//set_default_bus_params(0773200, 31, 0, 0); // base addr, intr-vector, intr level

	// M9312 has only 2 register: PC and PSW access is monitored, to count power-up DATIs
	register_count = 2;

	reg_trap_PC = &(this->registers[0]); // @  base addr  773024
	strcpy(reg_trap_PC->name, "PC");
	reg_trap_PC->active_on_dati = true; // counter for ADDR boot vector redirection
	reg_trap_PC->active_on_dato = false;
	reg_trap_PC->writable_bits = 0x0000; // read only. Still accepts DATO .. todo!

	reg_trap_PC->reset_value = 0; // const register value set later in resolve()
	reg_trap_PSW = &(this->registers[1]); // @  base addr + 2 = 773026
	strcpy(reg_trap_PSW->name, "PSW");
	reg_trap_PSW->active_on_dati = true;
	reg_trap_PSW->active_on_dato = false;
	reg_trap_PSW->writable_bits = 0x0000; // read only. Still accepts DATO .. todo!
	reg_trap_PSW->reset_value = 0; // const register value set later in resolve()

	// all ROM sockets occupy space in IOpage, even if no ROM plugged in
	rom[0] = new rom_c("CONSEMU", 256, 0765000);
	rom[1] = new rom_c("BOOTROM1", 64, 0773000);
	rom[2] = new rom_c("BOOTROM2", 64, 0773200);
	rom[3] = new rom_c("BOOTROM3", 64, 0773400);
	rom[4] = new rom_c("BOOTROM4", 64, 0773600);
	// make empty 
	for (int i = 0; i < 5; i++)
		rom[i]->fill(empty_socket_data_value);

	bootaddress = MEMORY_ADDRESS_INVALID;
	bootaddress_info.value = "DISABLED";

}

m9312_c::~m9312_c() {
	// free loaded ROMs
	for (unsigned i = 0; i < 5; i++)
		if (rom[i] != NULL)
			delete rom[i];

}

// Load data from MACRO11 listing file, ore fill with "empty socket" pattern
// 'rom_required_file_start_address': code start address in file,
// is moved to rom[rom_idx]->baseaddress
void m9312_c::plug_rom(parameter_string_c *filepath, unsigned rom_idx,
		uint32_t rom_required_file_start_address) {
	assert(rom_idx < 5);

	rom_c *r = rom[rom_idx]; // alias
	bool empty_socket = false; // new state: ROM plugged into socket?

	if (filepath == NULL || filepath->new_value.empty())
		empty_socket = true;
	else if (filepath->new_value == "-") { // - also means NO ROM
		filepath->new_value = "";
		empty_socket = true;
	}
	if (!empty_socket) {
		// ROM file must contain addresses rom_required_file_start_address
		// so after loading, baseaddress is changed to file addresses.
		uint32_t baseaddress_saved = r->baseaddress;

		if (!r->load_macro11_listing(filepath->new_value.c_str())) {
			ERROR("Loading %s from file %s failed.", r->name.c_str(),
					filepath->new_value.c_str());
			filepath->new_value = "";
			empty_socket = true;
		} else if (r->baseaddress != rom_required_file_start_address) {
			ERROR("Content for %s in file %s not starting at %06o.", r->name.c_str(),
					filepath->new_value.c_str(), rom_required_file_start_address);
			filepath->new_value = "";
			empty_socket = true;
		} else {
			// move symbol lablesfrom 173000 data to 773200, for example.
			r->relocate(baseaddress_saved);
			if (verbosity.value == LL_DEBUG)
				r->dump(stdout);
		}
	}
	if (empty_socket)
		r->fill(empty_socket_data_value);
}

// Update dependencies from loaded ROMs and symbolic boot address.
// Search symbolic auto boot address in installed and relocated ROMs
void m9312_c::resolve() {

	/* 1. get boot address */
	bootaddress = MEMORY_ADDRESS_INVALID;

	// check: autoboot code label present in any ROM?
	if (!bootaddress_label.value.empty()) {
		// search for ROM label in code label table of all ROMs.
		// Upper/lower case matters!
		for (int i = 0; i < 5; i++)
			if (rom[i] != NULL) {
				codelabel_map_c codelabels = rom[i]->codelabels;
				if (codelabels.is_defined(bootaddress_label.value))
					bootaddress = codelabels.get_address(bootaddress_label.value);
			}
	}

	if (bootaddress == MEMORY_ADDRESS_INVALID)
		bootaddress_info.value = "DISABLED";
	else {
		char buff[10];
		sprintf(buff, "%06o", bootaddress);
		bootaddress_info.value = buff;
		INFO("Code label \"%s\" resolved, auto boot PC = %06o", bootaddress_label.value.c_str(),
				bootaddress);
	}

	/* 2. set value for overlayed registers */
	if (rom[1]) {
		// BOOTROM1 must be present.

		// Setup const values of BOOT vector locations.
		uint16_t data;
		// setup value of 773024, power trap PC
		// its either the power-on PC start given by "switches",
		// or
		if (bootaddress == MEMORY_ADDRESS_INVALID) {
			// just duplicate the content of BOOTROM1, 173024
			data = rom[1]->get_data(0773024); // always 173000 in DEC M9312 BOOTROMs
		} else
			// overlay ROM with start address
			data = bootaddress;
		reg_trap_PC->reset_value = data;
		//	set_register_dati_value(reg_trap_PC, data, __func__); not yet on UNIBUS

		// setup value of 773026, power trap PSW
		// just duplicate the content of BOOTROM1, 173026
		data = rom[1]->get_data(0773026); // always 340 in DEC M9312 BOOTROMs
		reg_trap_PSW->reset_value = data;
		//	set_register_dati_value(reg_trap_PSW, data, __func__); not yet on UNIBUS
	}
}

bool m9312_c::on_param_changed(parameter_c *param) {
	if (param == &consemurom_filepath) {
		// must be assembled to 165000
		// move to 18 bit IOpage address
		plug_rom(&consemurom_filepath, 0, 0165000);
		resolve();
		return true; // accept changed .new_value
	} else if (param == &bootrom1_filepath) {
		plug_rom(&bootrom1_filepath, 1, 0173000);
		resolve();
		return true;
	} else if (param == &bootrom2_filepath) {
		plug_rom(&bootrom2_filepath, 2, 0173000);
		resolve();
		return true;
	} else if (param == &bootrom3_filepath) {
		plug_rom(&bootrom3_filepath, 3, 0173000);
		resolve();
		return true;
	} else if (param == &bootrom4_filepath) {
		plug_rom(&bootrom4_filepath, 4, 0173000);
		resolve();
		return true;
	} else if (param == &bootaddress_label) {
		bootaddress_label.value = bootaddress_label.new_value; // resolve() works on .value
		resolve();
	}
	// no own parameter or "enable" logic
	// more actions (enable triggers unibus_device.install()/uninstall())
	return qunibusdevice_c::on_param_changed(param);
}

// called when parameter "enabled" goes true
// registers not yet linked to UNIBUS map.
// result false: configuration error, not pluggable
bool m9312_c::on_before_install() {

	// Check ROM config
	// console emulator is optional.
	// BOOTROM1 is mandatory.
	// ROM2,3,4 are optional, but must be present in asceding order
	if (rom[1] == NULL) {
		ERROR("BOOTROM1 must be plugged in");
		return false;
	}
	for (int i = 2; i < 5; i++)
		if (rom[i] != NULL && rom[i - 1] == NULL) {
			WARNING("BOOTROM sockets not populated in ascending order: %u missing", i - 1);
			return false;
		}

	if (!bootaddress_label.value.empty() && bootaddress == MEMORY_ADDRESS_INVALID) {
		WARNING("Code label \"%s\", not found in any ROM. no auto boot",
				bootaddress_label.value.c_str());
	}

	// install ROMs to UNIBUS
	for (int i = 0; i < 5; i++)
		if (rom[i] != NULL)
			rom[i]->install();

	// lock ROMs against "unplugging"
	consemurom_filepath.readonly = true;
	bootrom1_filepath.readonly = true;
	bootrom2_filepath.readonly = true;
	bootrom3_filepath.readonly = true;
	bootrom4_filepath.readonly = true;
	bootaddress_label.readonly = true;
	
	return true ;
}

// called when parameter "enabled" goes false
void m9312_c::on_after_uninstall() {
	// disabled

	// deinstall ROMs from UNIBUS
	for (int i = 0; i < 5; i++)
		if (rom[i] != NULL)
			rom[i]->uninstall();

	// allow "ROM chip change"
	consemurom_filepath.readonly = false;
	bootrom1_filepath.readonly = false;
	bootrom2_filepath.readonly = false;
	bootrom3_filepath.readonly = false;
	bootrom4_filepath.readonly = false;
	bootaddress_label.readonly = false;
}

// background worker.
// udpate LEDS, poll switches direct to register flipflops
void m9312_c::worker(unsigned instance) {
	UNUSED(instance); // only one
	timeout_c timeout;
	while (!workers_terminate) {
		// poll BOOT vector ADDR timout
		timeout.wait_ms(50);
		if (qunibus->is_address_overlay_active()) {
			// start timeout
			if (qunibusadapter->line_ACLO) {
				// timer starts running when ACLO goes inactive
				bootaddress_timeout.start_ms(bootaddress_timeout_ms);
			}
			if (bootaddress_timeout.reached()) {
				DEBUG("bootaddress_timeout.reached()");
				bootaddress_clear();
			}
		}
	}
}

// set UNIBUS ADDR lines to boot vector address overlay
void m9312_c::bootaddress_set() {
	if (bootaddress != MEMORY_ADDRESS_INVALID) {
		qunibus->set_address_overlay(0773000) ;
		ddrmem->set_pmi_address_overlay(0773000) ; // emulated CPU booting from DDRRAM via PMI?
		DEBUG("bootaddress_set");
		// remove vector after 300ms, if no  access to PC/PSW at 773024/26
		bootaddress_timeout.start_ms(bootaddress_timeout_ms);
		bootaddress_reg_trap_accesses = 0;
	}
}

// remove boot vector address overlay from UNIBUS ADDR lines
void m9312_c::bootaddress_clear() {
	if (qunibus->is_address_overlay_active()) {
		qunibus->set_address_overlay(0) ;
		ddrmem->set_pmi_address_overlay(0) ;
		DEBUG("bootaddress_clr_event");
	}
}

// process DATI/DATO access to one of my "active" registers
// !! called asynchronuously by PRU, with SSYN asserted and blocking UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void m9312_c::on_after_register_access(qunibusdevice_register_t *device_reg,
		uint8_t unibus_control) {
	UNUSED(device_reg);
	UNUSED(unibus_control);
	// the value of reg_trap_PC and reg_trap_PSW never change at runtime.
	// just count MSYNs

	if (!qunibus->is_address_overlay_active())
		return;

	// bootring CPU accesses PC and PSW, then remove the boot vector
	bootaddress_reg_trap_accesses++;
	if (bootaddress_reg_trap_accesses == 2) {
		DEBUG("2nd MSYN");
		bootaddress_clear();
	}
}

// after UNIBUS install, device is reset by DCLO cycle
void m9312_c::on_power_changed(signal_edge_enum aclo_edge,
		signal_edge_enum dclo_edge) {
	UNUSED(dclo_edge);
	// !!! Detection of ACLO edges appears delayed against MSYN/SSYN activity,
	// so don't use "ACLO edge falling"
	if (aclo_edge == SIGNAL_EDGE_RAISING) { // ACLO leading edge: set BOOT vector ADDR
		DEBUG("ACLO asserted");
		bootaddress_set();
	}
}

// UNIBUS INIT: clear all registers
void m9312_c::on_init_changed(void) {
//	// write all registers to "reset-values"
//	if (init_asserted) {
//		reset_unibus_registers();
//		INFO("m9312_c::on_init()");
//	}
}

