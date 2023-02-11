/* m9312.hpp: Implementation of M9312 ROM bootstrap

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
 
 */
#ifndef _M9312_HPP_
#define _M9312_HPP_

#include "utils.hpp"
#include "qunibusdevice.hpp"
#include "memoryimage.hpp"
#include "rom.hpp"

class m9312_c: public qunibusdevice_c {
private:
	// content of unpopulated ROM sockets
	const uint16_t empty_socket_data_value = 0161777; // bits 12,11,10 are inverted

	void plug_rom(parameter_string_c *filepath, unsigned rom_idx,
			uint32_t rom_required_file_start_address);

	void resolve(void);

	unsigned bootaddress_timeout_ms = 300;
	timeout_c bootaddress_timeout; // vector max 300ms active
	int bootaddress_reg_trap_accesses; // count access to trap registers
	void bootaddress_set(void);
	void bootaddress_clear(void);

public:

	m9312_c();
	~m9312_c();

	bool on_param_changed(parameter_c *param) override;  // must implement

	// rom content.
	// if filename parameter empty: ROM "Unplugged", rom[i] = 0161777 fill pattern
	parameter_string_c consemurom_filepath = parameter_string_c(this, "consemu_file", "cer", /*readonly*/
	false, "ak6dn.com *.lst file for console emulator & diag ROM. \"-\" = no ROM in socket.");

	parameter_string_c bootrom1_filepath = parameter_string_c(this, "bootrom1_file", "br1", /*readonly*/
	false, "ak6dn.com *.lst file for BOOT ROM1");

	parameter_string_c bootrom2_filepath = parameter_string_c(this, "bootrom2_file", "br2", /*readonly*/
	false, "ak6dn.com *.lst file for BOOT ROM2");

	parameter_string_c bootrom3_filepath = parameter_string_c(this, "bootrom3_file", "br3", /*readonly*/
	false, "ak6dn.com *.lst file for BOOT ROM3");

	parameter_string_c bootrom4_filepath = parameter_string_c(this, "bootrom4_file", "br4", /*readonly*/
	false, "ak6dn.com *.lst file for BOOT ROM4");

	parameter_string_c bootaddress_label = parameter_string_c(this, "bootaddress_label", "bl", /*readonly*/
	false, "MACRO11 label from *.lst file to auto boot. Empty = no autoboot");

	// info: show boot address, or NO AUTO BOOT
	parameter_string_c bootaddress_info = parameter_string_c(this, "bootaddress_info", "bi",/*readonly*/
	true, "resolved bootaddress => power-on PC");

	uint32_t bootaddress;	// numerical value of boot PC

	// 5 roms: 0 = console emulator, 1..4 = BOOT ROMs
	rom_c *rom[5];

	// pseudo register interface: ROM cells with special funciton
	qunibusdevice_register_t *reg_trap_PC; // 773024, implemented by switches, 
	qunibusdevice_register_t *reg_trap_PSW; // 773026
	// both registers used to count DAT cycles after power up for boot logic

	// background worker function
	void worker(unsigned instance) override;

	bool on_before_install(void) override;
	void on_after_uninstall(void) override;

	// called by qunibusadapter on emulated register access
	void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control)
			override;

	void on_power_changed(signal_edge_enum aclo_edge,
			signal_edge_enum dclo_edge) override;
	void on_init_changed(void) override;
};

#endif
