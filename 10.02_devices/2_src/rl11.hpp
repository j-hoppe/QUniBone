/* rl11.hpp: Implementation of the RL11 controller

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
#ifndef _RL11_HPP_
#define _RL11_HPP_

#include "qunibusadapter.hpp"
#include "storagecontroller.hpp"

class RL0102_c;
class RL11_c: public storagecontroller_c {
private:
	/** everything shared between different threads must be "volatile" */
	volatile unsigned state; // one of RL11_STATE_*

	timeout_c timeout;

	// which drive will communicate with the controeller via the drive bus.
	volatile uint8_t selected_drive_unitno;

	/*** internal register values, to be mapped to UNIBUS registers ***/
	volatile uint8_t function_code;

	volatile bool interrupt_enable;
	volatile uint32_t unibus_address_msb; // bits<16:17> of BA register in CS

	volatile bool error_operation_incomplete; // OPI. operation aborted because of error
	volatile bool error_dma_timeout; // DMA operation addresses non existing memory
	volatile bool error_writecheck; // mismatch between memory and disk data
	volatile bool error_header_not_found; // sector address notfound on track

	// after "read header": MP register show different values on successive reads
	volatile uint16_t mpr_silo[3];  // max 3 word deep buffer
	volatile unsigned mpr_silo_idx;  // read index in MP register SILO

	// data buffer to/from drive
	uint16_t silo[128]; // buffer from/to drive
	uint16_t silo_compare[128]; // memory data to be compared with silo

	// RL11 has one INTR and DMA
	dma_request_c dma_request = dma_request_c(this); // operated by qunibusadapter
	intr_request_c intr_request = intr_request_c(this);

	// only 16*16 = 256 byte buffer from drive (SILO)
	// one DMA transaction per sector, must be complete within one sector time
	// (2400 rpm = 40 rounds per second -> 1/160 sec per sector  = 6,25 millisecs

	uint32_t get_unibus_address(void);
	void update_unibus_address(uint32_t addr);
	uint16_t get_MP_wordcount(void);
	void set_MP_wordcount(uint16_t wordcount);
	void set_MP_dati_value(uint16_t w, const char *debug_info);
	void set_MP_dati_silo(const char *debug_info);

	void clear_errors(void);

	// controller can accept commands again
	void do_command_done(void);
	// set readable value for busreg_CS
	void do_controller_status(bool do_intr, const char *debug_info);
	void do_operation_incomplete(const char *info);

	// state machines
	void change_state(unsigned new_state);
	void change_state_INTR(unsigned new_state);
	void state_seek(void);
	void state_readwrite(void);

	void connect_to_panel(void);
	void disconnect_from_panel(void);
	void refresh_params_from_panel(void);

public:

	// register interface to  RL11 controller
	qunibusdevice_register_t *busreg_CS; 	// Control Status: offset +0
	qunibusdevice_register_t *busreg_BA;		// Bus Address: offset +2
	qunibusdevice_register_t *busreg_DA;	// disk address: offset +4
	qunibusdevice_register_t *busreg_MP;	// Multi Purpose: offset +6

	RL11_c(void);
	~RL11_c(void);

	bool on_before_install(void) override ;
	void on_after_uninstall(void) override ;

	bool on_param_changed(parameter_c *param) override;

	void reset(void);

	// background worker function
	void worker(unsigned instance) override;

	RL0102_c *selected_drive(void);

	// called by qunibusadapter after DATI/DATO access to active emulated register
	// Runs at 100% RT priority, UNIBUS is stopped by SSYN while this is running.
	void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control)
			override;

	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
	void on_init_changed(void) override;
	void on_drive_status_changed(storagedrive_c *drive);
};

#endif
