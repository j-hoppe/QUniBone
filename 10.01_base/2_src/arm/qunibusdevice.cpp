/* qunibusdevice.cpp: abstract device with interface to qunibusadapter

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


 aug-2020	JH		adapted to QBUS
 6-feb-2020	JH		added symbol table
 12-nov-2018  JH      entered beta phase

 abstract qunibus device
 maybe mass storage controller or other device implementing
 QBUS/UNIBUS IOpage registers.
 sets device register values depending on internal status,
 reacts on register read/write over QBUS/UNIBUS by evaluation of PRU events.
 */
//#include <string>
//using namespace std;
#include <vector>
#include <assert.h>
#include "logger.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"

qunibusdevice_c::qunibusdevice_c() :
		device_c() {
	handle = 0;
	register_count = 0;
	// device is not yet enabled, QBUS/UNIBUS properties can be set
	base_addr.readonly = false;
	priority_slot.readonly = false;
	intr_vector.readonly = false;
	intr_level.readonly = false;
	default_base_addr = 0;
	default_priority_slot = 0;
	default_intr_vector = 0;
	default_intr_level = 0;

	log_channelmask = 0; // no logging until set
}

qunibusdevice_c::~qunibusdevice_c() {
}

// implements params, so must handle "change"
bool qunibusdevice_c::on_param_changed(parameter_c *param) {
	if (param == &enabled) {
		// plug/unplug device into QUNIBUS:
		if (enabled.new_value) {
			if (!on_before_install()) // possible callback to device
				return false ; // device denied enable
			// enable: lock QBUS/UNIBUS config
			base_addr.readonly = true;
			priority_slot.readonly = true;
			intr_vector.readonly = true;
			intr_level.readonly = true;
			install(); // visible on QBUS/UNIBUS
			on_after_install() ;
		} else {
			// disable
			on_before_uninstall() ;
			uninstall();
			on_after_uninstall() ; // possible callback to device
			base_addr.readonly = false;
			priority_slot.readonly = false;
			intr_vector.readonly = false;
			intr_level.readonly = false;
		}
	}
	return device_c::on_param_changed(param); // more actions (for enable)
}

// define default values for device BASE address and INTR
void qunibusdevice_c::set_default_bus_params(uint32_t default_base_addr,
		unsigned default_priority_slot, unsigned default_intr_vector,
		unsigned default_intr_level) {
	assert(default_priority_slot <= PRIORITY_SLOT_COUNT); // bitmask!

	// make proper 16/18/22 bit IOpage address: start + addr<12:0>
	if (qunibus->addr_width == 0)
		FATAL("Address width of " QUNIBUS_NAME " not yet known!") ;
	default_base_addr = qunibus->iopage_start_addr + (default_base_addr & 0x1fff) ;
	this->default_base_addr = default_base_addr;
	this->default_priority_slot = default_priority_slot;
	this->default_intr_vector = this->intr_vector.new_value = default_intr_vector;
	this->default_intr_level = this->intr_level.new_value = default_intr_level;
	base_addr.set(default_base_addr);
	priority_slot.set(default_priority_slot);
	intr_vector.set(default_intr_vector);
	intr_level.set(default_intr_level);
}

void qunibusdevice_c::install(void) {
	qunibusadapter->register_device(*this); // -> device_c ?
	// now has handle

	// Reset device by generating DCLO power cycle.
	// Do not toggle ACLO, meant for run/stop conditions (CPU start, M9312 boot vector redirection)
	on_power_changed(SIGNAL_EDGE_NONE, SIGNAL_EDGE_RAISING);
	// ACLO active, DCLO inactive
	on_power_changed(SIGNAL_EDGE_NONE, SIGNAL_EDGE_FALLING);
}

void qunibusdevice_c::uninstall(void) {
	qunibusadapter->unregister_device(*this);
}

qunibusdevice_register_t *qunibusdevice_c::register_by_name(string name) {
	unsigned i;
	for (i = 0; i < register_count; i++) {
		qunibusdevice_register_t *reg = &(registers[i]);
		if (reg->name && !strcasecmp(name.c_str(), reg->name))
			return reg;
	}
	return NULL;
}

qunibusdevice_register_t *qunibusdevice_c::register_by_unibus_address(uint32_t addr) {
	unsigned i;
	for (i = 0; i < register_count; i++) {
		qunibusdevice_register_t *reg = &(registers[i]);
		if (reg->addr == addr)
			return reg;
	}
	return NULL;
}

// set value of QBUS/UNIBUS register which can be read by DATI
// passive register: simply set value in shared PRU RAM
// active: set additionally "read" flipflops
void qunibusdevice_c::set_register_dati_value(qunibusdevice_register_t *device_reg,
		uint16_t value, const char *debug_info) {
//	if (device_reg->active_on_dati)
	// always set dati_flipflops, needed to restore value written with DATO
	device_reg->active_dati_flipflops = value;
	device_reg->pru_iopage_register->value = value;

	// signal: changed by device logic
	log_register_event(debug_info, device_reg);
}
/*
 // same as if QBUS/UNIBUS DATO has written the register
 void qunibusdevice_c::set_register_dato_value(qunibusdevice_register_t *device_reg,
 uint16_t value) {
 if (device_reg->active_on_dato) {
 device_reg->active_dato_flipflops = value;
 // do not change value seen with QBUS/UNIBUS DATI
 } else
 device_reg->pru_iopage_register->value = value;
 }
 */
// get value of QBUS/UNIBUS register which has been written by DATO
uint16_t qunibusdevice_c::get_register_dato_value(qunibusdevice_register_t *device_reg) {
	if (device_reg->active_on_dato)
		return device_reg->active_dato_flipflops;
	else
		return device_reg->pru_iopage_register->value;
}

// write the reset value into all registers (helper for QBUS/UNIBUS INIT)
void qunibusdevice_c::reset_unibus_registers() {
	unsigned i;
	for (i = 0; i < register_count; i++) {
		qunibusdevice_register_t *reg = &(registers[i]);
		set_register_dati_value(reg, reg->reset_value, __func__);
		// log_register_event("RESET", reg) ;
	}
}

// log register state changes:
//  print event info
// - print full register content to logger
// "active" registers are printed as <datiflipflop></datoflipflop>

void qunibusdevice_c::log_register_event(const char *change_info,
		qunibusdevice_register_t *changed_reg) {
	// do not use std:: string .. hand coded because of performance
	char buffer[1024];
	unsigned i;

	if (logger->ignored(this, LL_DEBUG))
		return; // channel not enabled: quick return

	buffer[0] = 0;

	// event info could be like "DATI CS" or "CHNG DA"
	if (change_info)
		strcat(buffer, change_info);
	if (changed_reg) {
		strcat(buffer, " ");
		strcat(buffer, changed_reg->name);
	}
	if (change_info || changed_reg)
		strcat(buffer, ":");

	// dump all registers.
	// if too much (examples: memory emulator): only the changed register
	if (register_count <= 8) {
		for (i = 0; i < register_count; i++) {
			char buff1[80];
			qunibusdevice_register_t *reg = &(registers[i]);
			if (reg->active_on_dati || reg->active_on_dato)
				sprintf(buff1, " %s=%06o/%06o", reg->name, reg->pru_iopage_register->value,
						reg->active_dato_flipflops);
			else
				sprintf(buff1, " %s=%06o", reg->name, reg->pru_iopage_register->value);
			strcat(buffer, buff1);
		}
	} else {
		// only the changed register
		sprintf(buffer, "%s=%06o", changed_reg->name, changed_reg->pru_iopage_register->value);
	}
	DEBUG(buffer);
}

// search device in global list mydevices[]				
qunibusdevice_c *qunibusdevice_c::find_by_request_slot(uint8_t priority_slot) {
	list<device_c *>::iterator devit;
	for (devit = device_c::mydevices.begin(); devit != device_c::mydevices.end(); ++devit) {
		qunibusdevice_c *ubdevice = dynamic_cast<qunibusdevice_c *>(*devit);
		if (ubdevice) {
			// all dma and intr requests
			for (vector<dma_request_c *>::iterator reqit = ubdevice->dma_requests.begin();
					reqit < ubdevice->dma_requests.end(); reqit++)
				if ((*reqit)->get_priority_slot() == priority_slot)
					return ubdevice;
			for (vector<intr_request_c *>::iterator reqit = ubdevice->intr_requests.begin();
					reqit < ubdevice->intr_requests.end(); reqit++)
				if ((*reqit)->get_priority_slot() == priority_slot)
					return ubdevice;
		}
	}
	return NULL;
}

// returns a string of form
// reg_first-reg_last, slots from-to, DMA, INTR level1/vec1,level2/vec2,...
char *qunibusdevice_c::get_qunibus_resource_info(void) {
	static char buffer[1024];
	char tmpbuff[256];
	buffer[0] = 0;

	// get register address range
	// use parameter "base_addr", register struct only valid after qunibusadapter.install()
	if (register_count == 0)  // cpu is a device without register interface
		strcpy(tmpbuff, "");
	else if (register_count == 1)
		sprintf(tmpbuff, "addr %s", qunibus->addr2text(base_addr.value));
	else
		sprintf(tmpbuff, "addr %s-%s (%d regs)", qunibus->addr2text(base_addr.value),
				qunibus->addr2text(base_addr.value + 2 * (register_count - 1)), register_count);
	strcat(buffer, tmpbuff);

	// get priority slot range from DMA request and intr_requests
	uint8_t slot_from = 0xff, slot_to = 0;
	for (vector<dma_request_c *>::iterator it = dma_requests.begin(); it < dma_requests.end();
			it++) {
		slot_from = std::min(slot_from, (*it)->get_priority_slot());
		slot_to = std::max(slot_to, (*it)->get_priority_slot());
	}
	for (vector<intr_request_c *>::iterator it = intr_requests.begin();
			it < intr_requests.end(); it++) {
		slot_from = std::min(slot_from, (*it)->get_priority_slot());
		slot_to = std::max(slot_to, (*it)->get_priority_slot());
	}

	if (slot_from > slot_to) // no requests: use device parameter
		slot_from = slot_to = priority_slot.value;
	if (slot_from == slot_to)
		sprintf(tmpbuff, ", slot %u", (unsigned) slot_from);
	else
		sprintf(tmpbuff, ", slots %u-%u", (unsigned) slot_from, (unsigned) slot_to);
	strcat(buffer, tmpbuff);

	//  DMA channels
	if (dma_requests.size() > 0) {
		if (dma_requests.size() == 1)
			sprintf(tmpbuff, ", DMA");
		else
			sprintf(tmpbuff, ", %uxDMA", dma_requests.size());
		strcat(buffer, tmpbuff);
	}
	//  Interrupts
	if (intr_requests.size() > 4) {
		// that crazy testcontroller has 31*4 !
		sprintf(tmpbuff, "%d INTRs", intr_requests.size());
		strcat(buffer, tmpbuff);
	} else if (intr_requests.size() > 0) {
		const char *sep = ":";
		strcat(buffer, ", INTRs");
		for (vector<intr_request_c *>::iterator it = intr_requests.begin();
				it < intr_requests.end(); it++) {
			sprintf(tmpbuff, "%s%d/%03o", sep, (*it)->get_level(), (*it)->get_vector());
			strcat(buffer, tmpbuff);
			sep = ",";
		}
	}

	return buffer;
}

