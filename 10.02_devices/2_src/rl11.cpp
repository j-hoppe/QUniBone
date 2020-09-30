/* rl11.cpp: Implementation of the RL11 controller

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


 - implements a 4 QBUS/UNIBUS register interface, which are shared with PRU.
 - gets notified of QBUS/UNIBUS register access on_after_register_access()
 - starts 4 RL01/02 drives
 on_after_register_access() is a high priority RT thread.
 It may ONLY update the settings of QBUS/UNIBUS interface registers by swapping in
 several internal registers (status for each drive, MP multipuprpose for different
 Commands) to QBUS/UNIBUS registers.
 - execution of commands, access to drives etc is made in worker()
 worker() is waked by a signal from  on_after_register_access()

 Todo:
 - operation, when drive power  OFF? error DSE drive select?
 1) RL0 powered off: CS =200, nach NOP
 Get status; DA=013, write 00004   Read: CS 102204 (ERR,OPI, DRVready=0
 MP = 006050 (3x identisch)
 Seek: Write 0006, read: 102206 (Spnerror, coveropen,brush home)
 MP = 20210
 2) RL0 powered on, LOAD
 NOOP: CS write 0, read 200
 Get Status: DA=013,write 0004, read 204. MP = 20217 (spinn down), dann 20210 (LOAD)
 Seek: Write CS=0006 , read 102206. MP = unchanged
 3) RL02 on track
 NOOP: CS write 0,  read 140201 (Driverer, do Getstatus
 Get Status: DA=013, write 0004, read 205. MP = 020235
 Seek: DA=0377 (255)100, read = 207




 - Which errors raise "OPI" (operation incomplete)
 NXM?
 - Mismatch DMA wordcount and sector buffer
 word len != sector border ? => no problem?
 end of track before worldcount == 0 ?
 "DA register is not incrmeneted in multisector transfer"
 => OPI?

 - "Read header: ": select which sector to read?
 Simulate disk rotation???
 How to generate CRC? -> simh!

 - "read data without header"
 -> wait for sector pulse? disk rotation?


 Communication between on_after_register_access and worker():
 - use pthread condition variable pthrad_cond_*
 - normally a mutex show protect worker() against variable change
 by interrupting on_after_register_access()
 - the signal "controller_ready" is that mutex already:
 set by cmd-start in on_after_register_access(),
 released by worker() on completion
 - still a mutex needed, only for the thread condition variable as shown in
 - mutex in on_after_register_access() and worker()
 - all refgsier access are atomic 32bit anyhow
 http://openbook.rheinwerk-verlag.de/linux_unix_programmierung/Kap10-006.htm#RxxKap10006040003201F02818E
 https://docs.oracle.com/cd/E19455-01/806-5257/6je9h032r/index.html#sync-59145


 */

#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <string>

#include "logger.hpp"
#include "gpios.hpp"
#include "utils.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "panel.hpp"
#include "rl11.hpp"
#include "rl0102.hpp"

// function codes
#define CMD_NOOP 	0
#define CMD_WRITE_CHECK	1
#define CMD_GET_STATUS	2
#define CMD_SEEK	3
#define CMD_READ_HEADER	4
#define CMD_WRITE_DATA	5
#define CMD_READ_DATA	6
#define CMD_READ_DATA_WITHOUT_HEADER_CHECK 7

// state of controller
#define RL11_STATE_CONTROLLER_READY	0 // can accept new commands
#define RL11_STATE_CONTROLLER_BUSY 1 // unspecified operation, temporary
// #define RL11_STATE_CONTROLLER_DONE RL11_STATE_CONTROLLER_READY
// #define RL11_STATE_CONTROLLER_DONE 2 // not busy, but still not able to accept new command

// seek states
#define RL11_STATE_SEEK_MASK 0x100 // bit marks all seek states
#define RL11_STATE_SEEK_INIT 0x101

// "read/write" states
#define RL11_STATE_RW_MASK	0x0200 // bit marks all READ/WRITE states
#define RL11_STATE_RW_INIT	0x0201
#define RL11_STATE_RW_DISK	0x0202
//#define RL11_STATE_RW_WAIT_DMA 0x0203
//#define RL11_STATE_RW_DONE 0x0204

RL11_c::RL11_c(void) :
		storagecontroller_c() {
	unsigned i;

	state = RL11_STATE_CONTROLLER_READY;
	name.value = "rl"; // only one supported
	type_name.value = "RL11";
	log_label = "rl";

	// base addr, intr-vector, intr level
	set_default_bus_params(0774400, 15, 0160, 5);

	// add 4 RL disk drives
	drivecount = 4;
	for (i = 0; i < drivecount; i++) {
		RL0102_c *drive = new RL0102_c(this);
		drive->unitno.value = i; // set the number plug
		drive->name.value = name.value + std::to_string(i);
		drive->log_label = drive->name.value;
		drive->parent = this; // link drive to controller
		storagedrives.push_back(drive);
	}

	// create QBUS/UNIBUS registers
	register_count = 4;

	// Control Status: reg no = 0, offset +0
	busreg_CS = &(this->registers[0]);
	strcpy_s(busreg_CS->name, sizeof(busreg_CS->name), "CS");
	busreg_CS->active_on_dati = false; // can be read fast without ARM code, no state change
	busreg_CS->active_on_dato = true; // writing changes controller state
	busreg_CS->reset_value = 0x80; // read default: bit 7 set
	busreg_CS->writable_bits = 0x3fe;  // bits 9..1 writable

	// Bus Address: offset +2
	busreg_BA = &(this->registers[1]);
	strcpy_s(busreg_BA->name, sizeof(busreg_BA->name), "BA");
	busreg_BA->active_on_dati = false; // pure storage
	busreg_BA->active_on_dato = false;
	busreg_BA->reset_value = 0; // read default: bit 7 set
	busreg_BA->writable_bits = 0xfffe;  // bits 15..1 writable

	// disk address: offset +4
	busreg_DA = &(this->registers[2]);
	strcpy_s(busreg_DA->name, sizeof(busreg_DA->name), "DA");
	busreg_DA->active_on_dati = false; // pure storage
	busreg_DA->active_on_dato = false;
	busreg_DA->reset_value = 0;
	busreg_DA->writable_bits = 0xffff;  // 16 bit read/write, format depnds on usage

	// Multi Purpose: offset +6
	busreg_MP = &(this->registers[3]);
	strcpy_s(busreg_MP->name, sizeof(busreg_MP->name), "MP");
	busreg_MP->active_on_dati = true; // read: needs logic for 3 word-sequence
	busreg_MP->active_on_dato = true; // write: just param word for cmds
	busreg_MP->reset_value = 0;
	busreg_MP->writable_bits = 0xffff;  // 16 bit read only

}

RL11_c::~RL11_c() {
	unsigned i;
	for (i = 0; i < drivecount; i++)
		delete storagedrives[i];
}

// called when "enabled" goes true, before registers plugged to QBUS/UNIBUS
// result false: configuration error, do not install
bool RL11_c::on_before_install() {
	connect_to_panel();
	return true ;
}

void RL11_c::on_after_uninstall() {
	disconnect_from_panel();
}


bool RL11_c::on_param_changed(parameter_c *param) {
	if (param == &priority_slot) {
		dma_request.set_priority_slot(priority_slot.new_value);
		intr_request.set_priority_slot(priority_slot.new_value);
	} else if (param == &intr_level) {
		intr_request.set_level(intr_level.new_value);
	} else if (param == &intr_vector) {
		intr_request.set_vector(intr_vector.new_value);
	}

	return storagecontroller_c::on_param_changed(param); // more actions (for enable)
}

/* connect parameters of drives to i2c paneldriver
 * Changes to parameter values after user panel operation
 * or refresh_params_from_panel()
 * TODO: virtual in base class
 */
void RL11_c::connect_to_panel() {
	if (drivecount != 4)
		FATAL("RL11 must control exactly 4 RL drives");
	/* Connection matrix: See construction of I2C paneldriver:
	 16 lamps and 8 buttons connected to 2 MC23017 GPIO extender

	 Chipaddr, reg_addr
	 */

	for (int drive_no = 0; drive_no < 4; drive_no++) {
		panelcontrol_c *pc;
		RL0102_c *drive = dynamic_cast<RL0102_c *>(storagedrives[drive_no]);
		// luckily paneldriver controls are named like drive->parameters ...
		pc = paneldriver->control_by_name(drive->name.value, drive->runstop_button.name);
		paneldriver->link_control_to_parameter(&drive->runstop_button, pc);
		pc = paneldriver->control_by_name(drive->name.value, drive->load_lamp.name);
		paneldriver->link_control_to_parameter(&drive->load_lamp, pc);
		pc = paneldriver->control_by_name(drive->name.value, drive->ready_lamp.name);
		paneldriver->link_control_to_parameter(&drive->ready_lamp, pc);
		pc = paneldriver->control_by_name(drive->name.value, drive->fault_lamp.name);
		paneldriver->link_control_to_parameter(&drive->fault_lamp, pc);
		pc = paneldriver->control_by_name(drive->name.value, drive->writeprotect_lamp.name);
		paneldriver->link_control_to_parameter(&drive->writeprotect_lamp, pc);
		pc = paneldriver->control_by_name(drive->name.value, drive->writeprotect_button.name);
		paneldriver->link_control_to_parameter(&drive->writeprotect_button, pc);
	}
}

/* unconnect drives from i2c paneldriver */
void RL11_c::disconnect_from_panel() {
	for (int drive_no = 0; drive_no < 4; drive_no++) {
		RL0102_c *drive = dynamic_cast<RL0102_c *>(storagedrives[drive_no]);
		paneldriver->unlink_controls_from_device(drive);
	}
}

// force param values of all drives as set by panel.
void RL11_c::refresh_params_from_panel() {
	for (int drive_no = 0; drive_no < 4; drive_no++) {
		RL0102_c *drive = dynamic_cast<RL0102_c *>(storagedrives[drive_no]);
		paneldriver->refresh_params(drive);
	}
}

// short alias
RL0102_c *RL11_c::selected_drive(void) {
	return dynamic_cast<RL0102_c *>(storagedrives[selected_drive_unitno]);
}

// reset controller, after installation, on power and on INIT
void RL11_c::reset(void) {
	// MPR = mpr_silo[0] is not reset
	busreg_MP->reset_value = busreg_MP->active_dati_flipflops;
	reset_unibus_registers();
	busreg_MP->reset_value = 0; // cleared on power cycle

	DEBUG("RL11_c::reset()");

	// reset internal state
	selected_drive_unitno = 0;
	function_code = 0;
	interrupt_enable = 0;
	unibus_address_msb = 0;
	clear_errors();
	intr_request.edge_detect_reset();
	change_state(RL11_STATE_CONTROLLER_READY);
	// or do_command_done() ?
	do_controller_status(false, __func__);
}

void RL11_c::clear_errors() {
	error_dma_timeout = false;
	error_operation_incomplete = false;
	error_writecheck = false;
	error_header_not_found = false;
}

// busaddress <17:16> = CS<4:5>
uint32_t RL11_c::get_unibus_address() {
	return (unibus_address_msb << 16) | get_register_dato_value(busreg_BA);
}

// set the changed current DMA qunibus address
void RL11_c::update_unibus_address(uint32_t addr) {
	unibus_address_msb = (addr >> 16);  // bit 17,16 used if CS is calculated
	set_register_dati_value(busreg_BA, addr & 0xfffe, __func__);
	// bits 17&16 in CS returned with do_controller_status()
}

// eval 2's complement value in MP register
// doc says: "bits13-15 must be ones": count value <= 0x2000
// but R11 v5.5 violates this rule.
uint16_t RL11_c::get_MP_wordcount() {
	uint16_t result = (0x10000 - get_register_dato_value(busreg_MP)) & 0xffff;
	// assert(result <= 0x2000); // RT11 v5.5 boot?
	return result;
}

void RL11_c::set_MP_wordcount(uint16_t wordcount) {
	// word count in 2's complement, bits 15:13 always set
	// assert(wordcount <= 0x1fff); // RT11 v5.5 boot?
	// do not change MP value visible with DATI
	busreg_MP->active_dato_flipflops = (0x10000 - wordcount) & 0xffff;
}

// data read from MP register comes from a 3 word silo.
// a  word must be put in the whole silo
void RL11_c::set_MP_dati_value(uint16_t w, const char *debug_info) {
	mpr_silo[0] = mpr_silo[1] = mpr_silo[2] = w;
	mpr_silo_idx = 0;
	set_register_dati_value(busreg_MP, w, debug_info);
}

// activate MP output sequence for filled silo
void RL11_c::set_MP_dati_silo(const char *debug_info) {
	mpr_silo_idx = 0;
	set_register_dati_value(busreg_MP, mpr_silo[0], debug_info);
}

// Access to QBUS/UNIBUS register interface
// called with 100% CPU highest RT priority.
// QBUS/UNIBUS is stopped by SSYN/RPLY while this is running.
// No loops! no drive, console, file or other operations!
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void RL11_c::on_after_register_access(qunibusdevice_register_t *device_reg,
		uint8_t unibus_control) {
	// on drive select:
	// move  status of new drive to controller status register
	// on command: signal worker thread

	switch (device_reg->index) {
	case 0: { // CS
		if (unibus_control == QUNIBUS_CYCLE_DATO) {
//GPIO_SETVAL(gpios.led[0], 1); // inverted, led OFF

			// only write changes state
			// wait until worker() is ready to accept signal,
			// only allowed if RL11_STATE_CONTROLLER_READY
			// Write into CSR is seems blocked if not controller READY (GO H)
			// FPMS PS2: GO inhibits WRT CSR L over E37, E70
			// Regular PDP-11 software should only poll CS until ready, not write before.
			if (state != RL11_STATE_CONTROLLER_READY)
				break; // ignore write

			pthread_mutex_lock(&on_after_register_access_mutex);

			assert(state == RL11_STATE_CONTROLLER_READY); // else blocked by mutex

			// CS<8:9> = drive select
			selected_drive_unitno = (busreg_CS->active_dato_flipflops >> 8) & 0x03;
			// CS<1:3> is cmd
			function_code = (busreg_CS->active_dato_flipflops >> 1) & 0x07;
			// CS <4:5> is address<16:17>
			unibus_address_msb = (busreg_CS->active_dato_flipflops >> 4) & 0x03;
			// CS<6> is IE
			interrupt_enable = !!(busreg_CS->active_dato_flipflops & (1 << 6));
			// CRDY is CS<7>
			bool new_controller_ready = !!(busreg_CS->active_dato_flipflops & (1 << 7));
			// accept only command if controller ready
			if (new_controller_ready) {
				// GO not set
				do_controller_status(false, __func__); // QBUS/UNIBUS sees still "controller ready"
			} else {
				RL0102_c *drive; // some funct need the selected drive
				bool execute_function_delayed;

//				if (interrupt_enable && busreg_CS->active_dato_flipflops == 0100) // ZRLLG@21636
//					GPIO_SETVAL(gpios.led[0], 0); // inverted, led ON

// TODO: can these functions be executed when seek is pending?
				// GO !
				clear_errors();
				// some function cause an interrupt immediately (in this QBUS/UNIBUS cycle):
				change_state(RL11_STATE_CONTROLLER_BUSY); // force BUSY->READY INTR
				execute_function_delayed = false;
				switch (function_code) {
				case CMD_NOOP:
					DEBUG("cmd %d = Noop", function_code);
					do_command_done();
					break;
				case CMD_SEEK:
					drive = selected_drive();
					if ((drive->status_word & 0x07) == RL0102_STATE_seek)
						//	if waiting for end of seek: execute seek in worker()
						execute_function_delayed = true;
					else {
						DEBUG("cmd %d = Seek", function_code);
						state_seek();
					}
					break;
				case CMD_GET_STATUS:
					drive = selected_drive();
					// SIMH: OPI if DA code not 3? Real RL11: just NOOP
					DEBUG("cmd %d = Get Status. DA=%06o.", function_code,
							get_register_dato_value(busreg_DA));
					// doc says: bits 0,4:7 must be 0. SimH checks only bit 1 and 3 for "1"
					// XXDP boot: seen 001217 and 00646
					if ((get_register_dato_value(busreg_DA) & 0x02) != 0x02) { // bit<0:1> must be 1,
						//			if ((get_register_dato_value(busreg_DA) & 0xf7) != 0x03) { // bit<0:1> must be 1,
						do_operation_incomplete("DA bit 2 not set");
					} else {
						if (get_register_dato_value(busreg_DA) & 0x08) // bit 3: reset status?
							drive->clear_error_register();
						set_MP_dati_value(drive->status_word, __func__);
					}
					do_command_done();
					break;
				default:
					execute_function_delayed = true;
				}

				if (execute_function_delayed) {
					// long running command (or delayed seek), involving disk activity:
					// run at lower priority
					// signal worker() with pthread condition variable
					// transition from high priority "qunibusadapter thread" to
					// standard "device thread".
					do_controller_status(false, __func__); // QBUS/UNIBUS sees now "controller not ready"
					// wake up worker()
					pthread_cond_signal(&on_after_register_access_cond);
				}
			}

			pthread_mutex_unlock(&on_after_register_access_mutex);
		} else {
			// CS reg is not "active_on_dati"
			//  set value in code with "set_register_dati_value(reg_CS, CS_read) ;"
		}
		break;
	}
	case 3: // MP
		if (unibus_control == QUNIBUS_CYCLE_DATI) {
			// return data from 3 word silo
			// assume: silo[0] read from dati-flipflops, now post read increment dati flipflops
			// MP port read. update MP with next sequential value from SILO
			if (mpr_silo_idx < 2) { // read header: MP is port to 3 words
				// next DATI reads next value
				set_register_dati_value(device_reg, mpr_silo[++mpr_silo_idx], __func__);
			} else {
				// 3rd or later access: no further inc, return always SILO[2]
				// until MP set with one "set_MP_*()" function
				set_register_dati_value(device_reg, mpr_silo[2], __func__);
			}
		} else {
			// value written by DATO are parameters for cmds, they do not change state
			// but restore value readable with DATI
			assert(busreg_MP->shared_register->value == busreg_MP->active_dati_flipflops);
		}
		break;
	}
	// now SSYN goes inactive !
}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void RL11_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	// storagecontroller_c forwards to drives
	storagecontroller_c::on_power_changed(aclo_edge, dclo_edge);

	if (dclo_edge == SIGNAL_EDGE_RAISING) {
		// power-on - defaults
		reset();
		// but I need a valid state before that.
	}
}

// QBUS/UNIBUS INIT: clear some registers, not all error conditions
void RL11_c::on_init_changed(void) {
	// storagecontroller_c forwards to drives
	storagecontroller_c::on_init_changed();

	// write all registers to "reset-values"
	if (!init_asserted) // falling edge of INIT
		reset();
}

// called by drive if ready or error
// must update CS then
void RL11_c::on_drive_status_changed(storagedrive_c *drive) {
	if (drive->unitno.value != selected_drive_unitno)
		return;
	// show status lines in CS for selected drive
	do_controller_status(false, __func__);
}

// issue interrupt.
// do not set CONTROLLER READY bit
void RL11_c::do_command_done(void) {
	// bool do_int = false;
	if (interrupt_enable && state != RL11_STATE_CONTROLLER_READY)
		change_state_INTR(RL11_STATE_CONTROLLER_READY);
	else
		// no intr
		change_state(RL11_STATE_CONTROLLER_READY);
#ifdef OLD	
	if (do_int) {

		// first set visible "controller ready"
		/* Change of CS->DATI visible value and raise of Interrupt must be atomic
		 * Else ZRLK: polls CS for ready, continues operation,
		 * RDY interrupt too late and at wrong program position
		 */
//		worker_boost_realtime_priority();
		change_state(RL11_STATE_CONTROLLER_READY);
		// scheduler may inject time here, if called from low prio worker() !
		// pending interrupt triggered
		//TODO: connect to interrupt register busreg_CS
		qunibusadapter->INTR(intr_request, NULL, 0);
		DEBUG("Interrupt!");
//		worker_restore_realtime_priority();
	} else
	// no intr
	change_state(RL11_STATE_CONTROLLER_READY);
	/*
	 {
	 // interrupt on leading edge of "controller-ready" signal
	 DEBUG("Interrupt!");
	 interrupt();
	 }
	 do_controller_status(__func__);
	 change_state(RL11_STATE_CONTROLLER_READY);
	 */
#endif	 
}

// CS read/Write access different registers.
// write current status into CS, for next read operation
// must be done after each DATO
void RL11_c::do_controller_status(bool do_intr, const char *debug_info) {
	RL0102_c *drive = selected_drive();
	uint16_t tmp = 0;
	bool drive_error_any = drive->drive_error_line; // save, may change
	bool controller_ready = (state == RL11_STATE_CONTROLLER_READY);
	//bit 0: drive ready
	if (drive->drive_ready_line)
		tmp |= BIT(0);
	// bits <1:3>: function code
	tmp |= (function_code << 1);
	// bits <4:5>: bus_address <17:16>
	tmp |= (unibus_address_msb & 3) << 4;
	// bit 6: IE
	if (interrupt_enable)
		tmp |= BIT(6);
	// bit 7: CRDY
	if (controller_ready)
		tmp |= BIT(7);
	// bits <8:9>: drive select
	tmp |= (selected_drive_unitno << 8);
	// bit <10:13>: error code. Only some possible errors
	if (error_operation_incomplete)
		tmp |= (0x01) << 10; // error code "OPI" = 0001
	if (error_writecheck)
		tmp |= (0x02) << 10;  // errror code "Read Data CRC" = 0010
	if (error_header_not_found)
		tmp |= (0x05) << 10; // error code "HNF" = 0101
	if (error_dma_timeout)
		tmp |= (0x08) << 10; // error code "NXM" = 1000
	// bit 14 is drive error
	if (drive_error_any)
		tmp |= BIT(14);
	// bit 15 is composite error
	if (error_dma_timeout || error_operation_incomplete || error_writecheck
			|| error_header_not_found || drive_error_any) {
		tmp |= BIT(15);
	}

	if (do_intr) {
		// set CSR atomically with INTR signal lines
		assert(interrupt_enable);
		assert(controller_ready);
		qunibusadapter->INTR(intr_request, busreg_CS, tmp);
	} else
		set_register_dati_value(busreg_CS, tmp, debug_info);
	// now visible on QBUS/UNIBUS

}

// if the drive is powered of, or not READY, it will not answer to RL11 requests
// then call do_operation_incomplete()
void RL11_c::do_operation_incomplete(const char *info) {
	DEBUG("do_operation_incomplete! %s", info);
	// drive does not respond after 200ms
	timeout.wait_ms(200 / emulation_speed.value);
	error_operation_incomplete = true;
	do_command_done();
}

// separate proc, to have a testpoint
void RL11_c::change_state(unsigned new_state) {
	if (state != new_state)
		DEBUG("Change RL11 state from 0x%x to 0x%x.", state, new_state);
	state = new_state;
	do_controller_status(false, __func__);
}

void RL11_c::change_state_INTR(unsigned new_state) {
	if (state != new_state)
		DEBUG("Change RL11 state from 0x%x to 0x%x.", state, new_state);
	state = new_state;
	do_controller_status(true, __func__);
}

// start seek operation, then interrupt
// only one state, but complex operation
void RL11_c::state_seek() {
	RL0102_c *drive = selected_drive();

// eval difference word in DA to destination_cylinder
//  what, if drive not ready???
	if (!drive->drive_ready_line) {
		do_operation_incomplete("state_seek(): drive not ready"); // verified
		return;
	}
	// bit 0 must be 1, bit 3 must be 0
	if ((get_register_dato_value(busreg_DA) & 9) != 1) {
		// do nothing
		do_command_done();
		return;
	}

	// cylinder address difference is <7:15>
	unsigned cyl_diff = get_register_dato_value(busreg_DA) >> 7;
	// direction is bit 2
	unsigned direction_to_spindle = (get_register_dato_value(busreg_DA) >> 2) & 1;
	unsigned destination_cylinder, destination_head;

	// if destination cylinder out of range:
	// "stop at guard band and retreat to first even numbered track"
	// so cyl > 511 : cylinder := 510 !, head = unchanged . Verified.
	if (direction_to_spindle) { // to higher cylinder
		destination_cylinder = drive->cylinder + cyl_diff;
		if (destination_cylinder >= drive->cylinder_count)
			destination_cylinder = (drive->cylinder_count - 1) & 0xfffe;
	} else { // to lower cylinder
		if (cyl_diff > drive->cylinder)
			destination_cylinder = 0; // bound hit
		else
			destination_cylinder = drive->cylinder - cyl_diff;
	}
	// bit<4> is head
	destination_head = (get_register_dato_value(busreg_DA) >> 4) & 1;
	bool ok = drive->cmd_seek(destination_cylinder, destination_head);
	if (!ok) {
		// drive in wrong state??
	}
	do_command_done();
}

// data from drive is requested with cmd_read_next_sector()
// the drive simulates disk rotation by returning content of next sector header
// together with sector data into "cur_sector_hader and cur_sector_data
// (on the original this is a sequence: first header data into SILO, then sector data)
//

// read data sector by sector from drive into SILO
// after each sector
// -> DMA transaction for sector
// perhaps a DATA LATE if previous DMA not ready
// increment diskaddress, read next sector.
// disk drive is guaranteed to need time_per_sector_us
void RL11_c::state_readwrite() {
	RL0102_c *drive = selected_drive();
	uint16_t disk_address = get_register_dato_value(busreg_DA);
	uint32_t unibus_address = get_unibus_address(); // device register to local var
	unsigned sector_wordcount = drive->sector_size_bytes / 2; // size of sector
	unsigned cmd_wordcount = get_MP_wordcount(); // wordcount in hidden MP register
	unsigned dma_wordcount; // len of current DMA transaction

	assert(sizeof(silo) / 2 >= sector_wordcount);
	assert(
			function_code == CMD_READ_DATA_WITHOUT_HEADER_CHECK || function_code == CMD_READ_DATA || function_code == CMD_WRITE_DATA || function_code == CMD_WRITE_CHECK);

	if (!drive->drive_ready_line) {
		do_operation_incomplete("state_readwrite(): drive not ready"); // verified
		return;
	}

	switch (state) {
	case RL11_STATE_RW_INIT:
		// Entry condition: cmd_wordcount > 0
		// diskaddress DA valid.

		// setup controller at start of read operation
		clear_errors();
		if (cmd_wordcount == 0)
			do_command_done();
		else
			change_state(RL11_STATE_RW_DISK);
		break;
	case RL11_STATE_RW_DISK:
		// start next sector read, or terminate
		assert(cmd_wordcount > 0);

		if (function_code == CMD_READ_DATA_WITHOUT_HEADER_CHECK) {
			// just read next sector data block from disk, disk address ignored
		} else {
			// Disk address valid?
			if (!drive->header_on_track(disk_address)) {
				// - sector not on current drive track: search header forever => OPI
				// - No spiral read/write: if reading past end of track: sector number is incremented to 40 = 050.
				//   no track change, no head switch, instead OPI error.
				// - advance past last sector on track: error OPI
				error_header_not_found = true;
				do_operation_incomplete("RL11_STATE_RW_DISK: !drive->header_on_track()");
				break;
			}

			// wait for right sector header
			drive->cmd_read_next_sector_header((uint16_t *) mpr_silo, 3);
			if (mpr_silo[0] != get_register_dato_value(busreg_DA))
				break; // wrong sector
			// DEBUG(LC_RL, "Found sector header DA=%06o.", silo[0]);
		}

		// # of words to read/write from/to memory
		if (cmd_wordcount > sector_wordcount)
			dma_wordcount = sector_wordcount;
		else
			dma_wordcount = cmd_wordcount; // transfer all remaining words

		memset((uint8_t *) silo, 0, sizeof(silo));
		memset((uint8_t *) silo_compare, 0, sizeof(silo_compare));

		if (function_code == CMD_READ_DATA
				|| function_code == CMD_READ_DATA_WITHOUT_HEADER_CHECK) {
			// the requested sector passes the head: read it into the SILO
			drive->cmd_read_next_sector_data(silo, 128);
			//logger.debug_hexdump(LC_RL, "Read data between disk access and DMA",
			//		(uint8_t *) silo, sizeof(silo), NULL);
			// start DMA transmission of SILO into memory
			qunibusadapter->DMA(dma_request, true, QUNIBUS_CYCLE_DATO, unibus_address, silo,
					dma_wordcount);
			error_dma_timeout = !dma_request.success;
			unibus_address = dma_request.unibus_end_addr;
		} else if (function_code == CMD_WRITE_CHECK) {
			// read sector data to compare with sector data
			drive->cmd_read_next_sector_data(silo, 128);
			// logger.debug_hexdump(LC_RL, "Read data between disk access and DMA",
			//		(uint8_t *) silo, sizeof(silo), NULL);
			// start DMA transmission of memory to compare with SILO
			qunibusadapter->DMA(dma_request, true, QUNIBUS_CYCLE_DATI, unibus_address,
					silo_compare, dma_wordcount);
			error_dma_timeout = !dma_request.success;
			unibus_address = dma_request.unibus_end_addr;
		} else if (function_code == CMD_WRITE_DATA) {
			// start DMA transmission of memory into SILO
			qunibusadapter->DMA(dma_request, true, QUNIBUS_CYCLE_DATI, unibus_address, silo,
					dma_wordcount);
			error_dma_timeout = !dma_request.success;
			unibus_address = dma_request.unibus_end_addr;
		}

		// request_client_DMA() was blocking, DMA processed now.
		// unibus_address updated to last accesses address
		unibus_address += 2; // was last address, is now next to fill
		// if timeout: yes, current addr is addr AFTER illegal address (verified)
		update_unibus_address(unibus_address); // set addr msb to cs

		if (error_dma_timeout) {
			// NXM condition
			do_operation_incomplete("RL11_STATE_RW_WAIT_DMA: dma timeout");
			break;
		}
		if (function_code == CMD_WRITE_DATA) { // data from SILO to disk
			// write whole silo. if less data read from memory, 00s are filled in.
			drive->cmd_write_next_sector_data(silo, 128);
			//logger.debug_hexdump(LC_RL, "Write data between DMA and disk access",
			//		(uint8_t *) silo, sizeof(silo),	NULL);
		} else if (function_code == CMD_WRITE_CHECK) {
			// compare data from disk in silo[] with data from memory in silo_compare[]
			unsigned i;
			error_writecheck = false;
			// compare only full sectors, even if not all words of sector read:
			// silo and silo_compare partly filled but 00 initialized
			for (i = 0; i < sector_wordcount; i++)
				if (silo[i] != silo_compare[i])
					error_writecheck = true;
		}

		if (function_code == CMD_READ_DATA_WITHOUT_HEADER_CHECK) {
			// "The DA register is not incremented"
			// But ZRLHB0, test 44: DA should increment by 1
			disk_address++;
		} else {
			// increment "disk address" to next sector on track.
			// may be invalid now, then "OPI" in next loop.
			disk_address++;
		}
		set_register_dati_value(busreg_DA, disk_address, __func__);

		// sector read & transfer OK. next?
		if (cmd_wordcount >= sector_wordcount)  // start DMA transmission of SILO into memory
			cmd_wordcount -= sector_wordcount;
		else
			cmd_wordcount = 0;
		set_MP_wordcount(cmd_wordcount);

		if (cmd_wordcount == 0) {
			// last sector transfered
			do_command_done();
			// READY/INTR delayed against end of DMA: nanosleep() in worker()
			break;
		}
		change_state(RL11_STATE_RW_DISK);
		break;
	default:
		ERROR("RL11:state_readwrite(): illegal state %d.", state);
	}
	// update visible state: local var to QBUS/UNIBUS register
//	update_unibus_address(unibus_address);
}

// thread
// excutes commands
void RL11_c::worker(unsigned instance) {
	UNUSED(instance); // only one
	assert(!pthread_mutex_lock(&on_after_register_access_mutex));

	// set prio to RT, but less than unibus_adapter
	worker_init_realtime_priority(rt_device);

	while (!workers_terminate) {
		/* process command state machine in parallel with
		 "active register" state changes
		 */

		// wait for "cmd" signal of on_after_register_access()
		int res;

		// CRDY in busreg_CS->active_dati_flipflops & 0x80)) 
		// may still be inactive, when PRU updatesit with iNTR delayed.
		// enable operation of pending on_after_register_access()
		/*
		 if (!(busreg_CS->active_dati_flipflops & 0x80)) { // CRDY must be set
		 ERROR("CRDY not set, CS=%06o", busreg_CS->active_dati_flipflops);
		 logger->dump(logger->default_filepath);
		 }
		 */
		res = pthread_cond_wait(&on_after_register_access_cond,
				&on_after_register_access_mutex);
		if (res != 0) {
			ERROR("RL11::worker() pthread_cond_wait = %d = %s>", res, strerror(res));
			continue;
		}
		// res==0: triggered by signal, execute next cmd
		RL0102_c *drive = selected_drive();
		if (init_asserted) {
			DEBUG("cmd %d ignored because of INIT.", function_code);
			continue;
		}
		if ((busreg_CS->active_dati_flipflops & 0x80)) // CRDY must be cleared
			ERROR("CRDY set, CS=%06o", busreg_CS->active_dati_flipflops);

		// all commands: OPI, if drive powered off
		clear_errors();

		if (drive->state.value == RL0102_STATE_power_off) {
			DEBUG("cmd %d ignored, drive powered off.", function_code);
			do_operation_incomplete("worker: drive power off");
			continue;
		}

		// inhibit command execution until previous seek complete (CRDY remains false)
		bool seek_wait = false;
		// DEBUG("AAA: drive->status_word = %06o", drive->status_word) ;
		while ((drive->status_word & 0x07) == RL0102_STATE_seek) {
			timeout.wait_ms(1);
			if (!seek_wait) // suppress to much output
				DEBUG("Start drive_busy_seeking. drive->status_word = %06o",
						drive->status_word);
			seek_wait = true;
		}
		if (seek_wait) {
			// wait for "DRIVE ready" after seek: race condition between RL0102 and RL11
			while ((busreg_CS->shared_register->value & 1) == 0)
				;

//			do_controller_status("seek busy ended") ;
			DEBUG("End drive_busy_seeking: drive->status_word = %06o", drive->status_word);
		}

		// start execution of new command
		// produce an interrupt on any transition to ready
		switch (function_code) {
		/* NOP, GETSTATUS have immediate INTR:
		 * handled fast in on_after_register_access()
		 */

		case CMD_WRITE_CHECK: // Write Check
			DEBUG("cmd %d = Write Check", function_code);
			change_state(RL11_STATE_RW_INIT);
			// reads 1 sector into a separate buffer and compares data
			break;
		case CMD_SEEK:
			// SEEK has immediate INTR: handled fast in on_after_register_access()
			// but can be delayed if drive already seeking and cmd execution blocked
			DEBUG("cmd %d = Seek (delayed)", function_code);
			change_state(RL11_STATE_SEEK_INIT);
			break;
		case CMD_READ_HEADER: // Read sector header from disk
			DEBUG("cmd %d = Read Header", function_code);
			// read header if not locked-on track?
			if (!drive->cmd_read_next_sector_header((uint16_t *) mpr_silo, 3)) {
			}
			set_MP_dati_silo(__func__);
			do_command_done();
			break;
		case CMD_WRITE_DATA: // Write sector data from memory to disk
			DEBUG("cmd %d = Write Data", function_code);
			change_state(RL11_STATE_RW_INIT);
			break;
		case CMD_READ_DATA: // Read disk datao into memory
			DEBUG("cmd %d = Read Data", function_code);
			// sector address from DA, before seek must moved head to same track
			change_state(RL11_STATE_RW_INIT);
			break;
		case CMD_READ_DATA_WITHOUT_HEADER_CHECK:
			DEBUG("cmd %d = Read Data Without Check", function_code);
			change_state(RL11_STATE_RW_INIT);
			break;
		default:
			ERROR("RL11: invalid function code %u", function_code);
		}

		// execute command. CRDY is false, no new cmds are accepted
		while (state != RL11_STATE_CONTROLLER_READY) {
			// ZRLHB0 requires CSREADY within 400*20 usec (addr 015716)
//			timeout.wait_ns(50000); // 50us
// log running operation need a timeout.wait_ns(). Internal in some states

			// process current command states machines
			// noop if machine terminates and state == RL11_STATE_CONTROLLER_READY
			if (state & RL11_STATE_SEEK_MASK)
				state_seek();
			else if (state & RL11_STATE_RW_MASK)
				state_readwrite();
		}
	}
	assert(!pthread_mutex_unlock(&on_after_register_access_mutex));
}

