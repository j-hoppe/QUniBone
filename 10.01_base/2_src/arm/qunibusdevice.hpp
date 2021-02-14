/* qqunibusdevice.hpp: abstract device with interface to qunibusadapter

 Copyright (c) 2018-2020, Joerg Hoppe
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

#ifndef _QUNIBUSDEVICE_HPP_
#define _QUNIBUSDEVICE_HPP_

#include <stdint.h>
#include <pthread.h>

#include "device.hpp"

#include "iopageregister.h"
#include "priorityrequest.hpp"

// forwards
class qunibusdevice_c;

typedef struct qunibusdevice_register_struct {
	// backlink
	qunibusdevice_c *device;
	char name[40]; // for display
	unsigned index; // # of register in device register list
	uint32_t addr; // qunibus address
	// so addr = device_base_addr + 2 * index

	/*** link into PRU shared area ***/
	// only for access by qunibusadapter
	// Restauration of shared_register->value IS NOT ATOMIC against device logic threads.
	// Devices must use only reg->active_dati_flipflops !
	volatile iopageregister_t *shared_register;
	uint8_t shared_register_handle;

	/*** static setup information ***/
	// "Active=true": PRU generates an event after DATI and/or DATO on that register
	//	  (context switch to ARM: slow!)
	// "Active=false": PRU handles register access with simple data value like a memory cell.
	//	  (fast)
	// "active_on_dato" is true for "command" registers, which
	//   start controller actions (like "CMD+GO" in RL11 and similar disk controllers
	// "active_on_dati" is true, if read with DATI clears a bit,
	//   or if register is port to a value sequence.
	//	  Status register should be not "active", as they are polled in loops
	// 	  But sometimes reading status clears an INTR flag.
	// controllers  "on_after_register_access" is called then after
	//   UNIBUS access to the register
	bool active_on_dati; // call on_after_register_access() on DATI
	bool active_on_dato; // call on_after_register_access() on DATO
	uint16_t reset_value;
	uint16_t writable_bits;

	/*** dynamic state  ***/
	// if register is "active": flipflops behind address
	// active registers have different latches for DATI and DATO cycles,
	// and represent status/command information.
	volatile uint16_t active_dati_flipflops; // latch acessed by DATI
	volatile uint16_t active_dato_flipflops;
} qunibusdevice_register_t;

class qunibusdevice_c: public device_c {
public:
	static qunibusdevice_c *find_by_request_slot(uint8_t priority_slot);

private:
	// setup address tables, also in shared memory
	// start both threads
	void install(void);

	void uninstall(void);bool is_installed() {
		return (handle > 0);
	}

public:
	uint8_t handle; // assigned by "qunibus.adapter.register

	// !!! slot, vector, level READONLY. If user should change,
	// !! add logic to update dma_request_c and intr_request_c

	// 0 = not "Plugged" in to UNIBUS
	parameter_unsigned_c base_addr = parameter_unsigned_c(this, "base_addr", "addr", true, "",
			"%06o", "controller base address in IO page", 18, 8);
	parameter_unsigned_c priority_slot = parameter_unsigned_c(this, "slot", "sl", true, "",
			"%d", "backplane slot #, interrupt priority within one level, 0 = next to CPU", 16,
			10);
	// dump devices without buffers toward CPU, smart buffering devices other end

	parameter_unsigned_c intr_vector = parameter_unsigned_c(this, "intr_vector", "iv", true, "",
			"%03o", "interrupt vector address", 9, 8);
	parameter_unsigned_c intr_level = parameter_unsigned_c(this, "intr_level", "il", true, "",
			"%o", "interrupt bus request level: 4,5,6,7", 3, 8);

	// DEC defaults as defined by device type
	uint32_t default_base_addr;
	uint8_t default_priority_slot;
	uint8_t default_intr_level;
	uint16_t default_intr_vector;

	// requests in use
	std::vector<dma_request_c *> dma_requests;
	std::vector<intr_request_c *> intr_requests;

	void set_default_bus_params(uint32_t default_base_addr, unsigned default_priority_slot,
			unsigned default_intr_vector, unsigned default_intr_level);

	// controller register data as pointer to registers in shared PRU RAM
	// UNIBUS addr of register[i] = base_addr + 2*i
	// !! devices have always sequential register address range !!
	unsigned register_count;
	qunibusdevice_register_t registers[MAX_IOPAGE_REGISTERS_PER_DEVICE];

	unsigned log_channelmask; // channelmask for DEBUG logging
	//  device is the log channel. one of logger::LC_*

	qunibusdevice_c();
	virtual ~qunibusdevice_c();	// class with virtual functions should have virtual destructors

	bool on_param_changed(parameter_c *param) override;

	// reset device
	// virtual void init() override ;

	// access the value of a register in shared UNIBUS PRU space
	void set_register_dati_value(qunibusdevice_register_t *device_reg, uint16_t value,
			const char *debug_info);
	uint16_t get_register_dato_value(qunibusdevice_register_t *device_reg);
	void reset_unibus_registers();

	qunibusdevice_register_t *register_by_name(string name);
	qunibusdevice_register_t *register_by_unibus_address(uint32_t addr);


	// callbacks to let device do something on UNIBUS plugin/plugout
	virtual bool on_before_install(void) { return true ;} 
	virtual void on_after_install(void) {} 
	virtual void on_before_uninstall(void) {} 
	virtual void on_after_uninstall(void) {} 

	
	// callback to be called on controller register DATI/DATO events.
	// must ACK mailbox.event.signal. Asynchron!
	// May not generate direct INTR or DMA.
	// INTR/DMA as task to separate INTR/DMA scheduler
	// -> orders INTR/DMA of different devices, wait for UNIBUS idle.
	virtual void on_after_register_access(qunibusdevice_register_t *device_reg,
			uint8_t unibus_control) = 0;
	// communication between on_after_register_access() and device_c::worker()
	// see pthread_cond_wait()* examples
	pthread_cond_t on_after_register_access_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t on_after_register_access_mutex = PTHREAD_MUTEX_INITIALIZER;

	void log_register_event(const char *change_info, qunibusdevice_register_t *changed_reg);

	char *get_qunibus_resource_info(void);

};

#endif
