/* DL11W.cpp: sample QBUS/UNIBUS controller with SLU & LTC logic

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
 20/12/2018 djrm copied to make slu device
 14/01/2019 djrm adapted to use UART2 serial port

 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>

#include "utils.hpp"
#include "gpios.hpp"
#include "logger.hpp"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"	// definition of class device_c
#include "qunibus.h"
#include "dl11w.hpp"

#include "rs232.hpp"
#include "rs232adapter.hpp"

//-------------------------------------------------

slu_c::slu_c() :
		qunibusdevice_c() {
	set_workers_count(2); // receiver and transmitte have own threads

	//ip_host.value = IP_HOST; // not used
	//ip_port.value = IP_PORT; // not used

	// static config
	name.value = "DL11";
	type_name.value = "slu_c";
	log_label = "slu";

	break_enable.value = 1; // SW4-1 default ON
	error_bits_enable.value = 1; // SE4-7 default ON

	// SLU has 2 Interrupt vectors: base = RCV, base+= XMT
	// put in slot 1, closest to CPU
	set_default_bus_params(SLU_ADDR, SLU_SLOT, SLU_VECTOR, SLU_LEVEL); // base addr, intr-vector, intr level

	// controller has some register
	register_count = slu_idx_count;

	reg_rcsr = &(this->registers[slu_idx_rcsr]); // @  base addr
	strcpy(reg_rcsr->name, "RCSR"); // Receiver Status Register
	reg_rcsr->active_on_dati = false;
	reg_rcsr->active_on_dato = true;
	reg_rcsr->reset_value = 0;
	reg_rcsr->writable_bits = 0xff;

	reg_rbuf = &(this->registers[slu_idx_rbuf]); // @  base addr
	strcpy(reg_rbuf->name, "RBUF"); // Receiver Buffer Register
	reg_rbuf->active_on_dati = true;
	reg_rbuf->active_on_dato = true; // required for "active on dati""
	reg_rbuf->reset_value = 0;
	reg_rbuf->writable_bits = 0x00;

	reg_xcsr = &(this->registers[slu_idx_xcsr]); // @  base addr
	strcpy(reg_xcsr->name, "XCSR"); // Transmitter Status Register
	reg_xcsr->active_on_dati = false;
	reg_xcsr->active_on_dato = true;
	reg_xcsr->reset_value = XCSR_XMIT_RDY; // set
	reg_xcsr->writable_bits = 0xff;

	reg_xbuf = &(this->registers[slu_idx_xbuf]); // @  base addr
	strcpy(reg_xbuf->name, "XBUF"); //Transmitter Buffer Register
	reg_xbuf->active_on_dati = false; // no controller state change
	reg_xbuf->active_on_dato = true;
	reg_xbuf->reset_value = 0;
	reg_xbuf->writable_bits = 0xff;

	// initialize serial format
	serialport.value = "ttyS2"; // labeled "UART2" on PCB
	baudrate.value = 9600;
	mode.value = "8N1";

	rs232adapter.rs232 = &rs232;
}

slu_c::~slu_c() {
}

// called when "enabled" goes true, before registers plugged to QBUS/UNIBUS
// result false: configuration error, do not install
bool slu_c::on_before_install(void) {
	// enable SLU: setup COM serial port
	// setup for BREAK and parity evaluation
	rs232adapter.rcv_termios_error_encoding = true;
	if (rs232.OpenComport(serialport.value.c_str(), baudrate.value, mode.value.c_str(),
	true)) {
		ERROR("Can not open serial port %s", serialport.value.c_str());
		return false; // reject "enable"
	}

	// lock serial port and settings
	serialport.readonly = true;
	baudrate.readonly = true;
	mode.readonly = true;

	INFO("Serial port %s opened", serialport.value.c_str());
	char buff[256];
	sprintf(buff, "\n\rSerial port %s opened\n\r", serialport.value.c_str());
	rs232.cputs(buff);

	return true;
}

void slu_c::on_after_uninstall(void) {
	// disable SLU
	rs232.CloseComport();
	// unlock serial port and settings
	serialport.readonly = false;
	baudrate.readonly = false;
	mode.readonly = false;
	INFO("Serial port %s closed", serialport.value.c_str());
}

bool slu_c::on_param_changed(parameter_c *param) {
	if (param == &priority_slot) {
		rcvintr_request.set_priority_slot(priority_slot.new_value);
		// XMT INTR: lower priority => nxt slot, and next vector
		xmtintr_request.set_priority_slot(priority_slot.new_value + 1);
	} else if (param == &intr_vector) {
		rcvintr_request.set_vector(intr_vector.new_value);
		xmtintr_request.set_vector(intr_vector.new_value + 4);
	} else if (param == &intr_level) {
		rcvintr_request.set_level(intr_level.new_value);
		xmtintr_request.set_level(intr_level.new_value);
	}
	return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}

//--------------------------------------------

// calc static INTR condition level. 
// Change of that condition calculated by intr_request_c.is_condition_raised()
bool slu_c::get_rcv_intr_level(void) {
	return rcv_done && rcv_intr_enable;
}

// Update RCSR and optionally generate INTR
void slu_c::set_rcsr_dati_value_and_INTR(void) {
	uint16_t val = (rcv_active ? RCSR_RCVR_ACT : 0) | (rcv_done ? RCSR_RCVR_DONE : 0)
			| (rcv_intr_enable ? RCSR_RCVR_INT_ENB : 0);
	switch (rcvintr_request.edge_detect(get_rcv_intr_level())) {
	case intr_request_c::INTERRUPT_EDGE_RAISING:
		// set register atomically with INTR, if INTR not blocked
		qunibusadapter->INTR(rcvintr_request, reg_rcsr, val);
		break;
	case intr_request_c::INTERRUPT_EDGE_FALLING:
		// BR4 is tied to monitor and enable, so raised INTRs may get canceled
		qunibusadapter->cancel_INTR(rcvintr_request);
		set_register_dati_value(reg_rcsr, val, __func__);
		break;
	default:
		set_register_dati_value(reg_rcsr, val, __func__);
	}
}

// PDP-11 writes into RCSR
void slu_c::eval_rcsr_dato_value(void) {
	uint16_t val = get_register_dato_value(reg_rcsr);

	rcv_intr_enable = val & RCSR_RCVR_INT_ENB ? 1 : 0;
	rcv_rdr_enb = val & RCSR_RDR_ENB ? 1 : 0;
	if (rcv_rdr_enb)
		rcv_done = 0; // raising edge clears rcv_done
	// if rcvr_done and int enable goes high: INTR
	set_rcsr_dati_value_and_INTR();
}

// Update RBUF, readonly
void slu_c::set_rbuf_dati_value(void) {
	uint16_t val = 0;
	if (error_bits_enable.value) {
		val = (rcv_or_err ? RBUF_OR_ERR : 0) | (rcv_fr_err ? RBUF_FR_ERR : 0)
				| (rcv_p_err ? RBUF_P_ERR : 0);
		if (val) // set general error flag
			val |= RBUF_ERROR;
	}
	val |= rcv_buffer; // received char in bits 7..0
	set_register_dati_value(reg_rbuf, val, __func__);
}

bool slu_c::get_xmt_intr_level() {
	return xmt_ready && xmt_intr_enable;
}

// Update Transmit Status Register XCSR and optionally generate INTR
void slu_c::set_xcsr_dati_value_and_INTR(void) {
	uint16_t val = (xmt_ready ? XCSR_XMIT_RDY : 0) | (xmt_intr_enable ? XCSR_XMIT_INT_ENB : 0)
			| (xmt_maint ? XCSR_MAINT : 0) | (xmt_break ? XCSR_BREAK : 0);
	switch (xmtintr_request.edge_detect(get_xmt_intr_level())) {
	case intr_request_c::INTERRUPT_EDGE_RAISING:
		// set register atomically with INTR, if INTR not blocked
		qunibusadapter->INTR(xmtintr_request, reg_xcsr, val);
		break;
	case intr_request_c::INTERRUPT_EDGE_FALLING:
		// BR4 is tied to monitor and enable, so raised INTRs may get canceled
		qunibusadapter->cancel_INTR(xmtintr_request);
		set_register_dati_value(reg_xcsr, val, __func__);
		break;
	default:
		set_register_dati_value(reg_xcsr, val, __func__);
	}

}

void slu_c::eval_xcsr_dato_value(void) {
	uint16_t val = get_register_dato_value(reg_xcsr);
	bool old_break = xmt_break;
	xmt_intr_enable = val & XCSR_XMIT_INT_ENB ? 1 : 0;
	xmt_maint = val & XCSR_MAINT ? 1 : 0;
	xmt_break = val & XCSR_BREAK ? 1 : 0;
	// if xmt_ready and int enable goes high: INTR
	set_xcsr_dati_value_and_INTR();

	if (old_break != xmt_break) {
		// re-evaluate break state on bit change
		if (break_enable.value)
			rs232.SetBreak(xmt_break);
		else
			rs232.SetBreak(0);
	}
}

void slu_c::eval_xbuf_dato_value(void) {
	// transmit data buffer contains only the character in bits 7..0
	xmt_buffer = get_register_dato_value(reg_xbuf) & 0xff;
}

// process DATI/DATO access to one of my "active" registers
// !! called asynchronuously by PRU, with SSYN asserted and blocking QBUS/UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void slu_c::on_after_register_access(qunibusdevice_register_t *device_reg,
		uint8_t unibus_control) {

//	if (unibus_control == QUNIBUS_CYCLE_DATO) // bus write 
//		set_register_dati_value(device_reg, device_reg->active_dato_flipflops, __func__);

	if (qunibusadapter->line_INIT)
		return; // do nothing wile reset

	switch (device_reg->index) {

	case slu_idx_rcsr:
		if (unibus_control == QUNIBUS_CYCLE_DATO) { // bus write into RCSR
			pthread_mutex_lock(&on_after_rcv_register_access_mutex); // signal changes atomic against QBUS/UNIBUS accesses
			eval_rcsr_dato_value(); // may generate INTR
			set_rcsr_dati_value_and_INTR();
			// ignore reader enable
			pthread_mutex_unlock(&on_after_rcv_register_access_mutex);
		}
		break;
	case slu_idx_rbuf: { // DATI/DATO: is read only, but write also clears "rcvr_done"
		// signal data has been read from bus
		pthread_mutex_lock(&on_after_rcv_register_access_mutex);
		rcv_done = 0;
		set_rcsr_dati_value_and_INTR();
		pthread_mutex_unlock(&on_after_rcv_register_access_mutex);
	}
		break;
	case slu_idx_xcsr:
		if (unibus_control == QUNIBUS_CYCLE_DATO) { // bus write
			pthread_mutex_lock(&on_after_xmt_register_access_mutex);
			eval_xcsr_dato_value(); // may trigger INTR
			set_xcsr_dati_value_and_INTR();
			pthread_mutex_unlock(&on_after_xmt_register_access_mutex);
		}
		break;
	case slu_idx_xbuf:
		if (unibus_control == QUNIBUS_CYCLE_DATO) { // bus write into XBUF
			pthread_mutex_lock(&on_after_xmt_register_access_mutex);
			eval_xbuf_dato_value();
			xmt_ready = 0; // signal worker: xmt_data pending
			set_xcsr_dati_value_and_INTR();
			// on_after_register_access_cond used for xmt worker
			pthread_cond_signal(&on_after_xmt_register_access_cond);
			pthread_mutex_unlock(&on_after_xmt_register_access_mutex);
		}
		break;
	default:
		break;
	}

}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void slu_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	UNUSED(aclo_edge);
	UNUSED(dclo_edge);
}

// QBUS/UNIBUS INIT: clear all registers
void slu_c::on_init_changed(void) {
	// write all registers to "reset-values"
	if (init_asserted) {
		reset_unibus_registers();
		rcv_active = 0;
		rcv_done = 0;
		rcv_intr_enable = 0;
		rcv_or_err = 0;
		rcv_fr_err = 0;
		rcv_p_err = 0;
		rcv_buffer = 0;
		xmt_ready = 1;
		xmt_intr_enable = 0;
		xmt_maint = 0;
		xmt_break = 0;
		rcvintr_request.edge_detect_reset();
		xmtintr_request.edge_detect_reset();
		// INFO("slu_c::on_init()");
	}
}

// background worker.
void slu_c::worker_rcv(void) {
	flexi_timeout_c timeout; // if emulated CPU, use emulated timing
	// timeout_c timeout; 
	rs232byte_t rcv_byte;

	worker_init_realtime_priority(rt_device);

	while (!workers_terminate) {
		/* Receiver not time critical? UARTs are buffering
		 So if thread is swapped out and back a burst of characters appear.
		 -> Wait after each character for transfer time before polling
		 RS232 again.
		 */
		unsigned poll_periods_us = (rs232.CharTransmissionTime_us * 9) / 10;
		// poll a bit faster to be ahead of char stream. 
		// don't oversample: PDP-11 must process char in that time

		timeout.wait_us(poll_periods_us);
		if (qunibusadapter->line_INIT)
			continue; // do nothing while reset
		// "query
		// rcv_active: can only be set by polling the UART input GPIO pin?
		// at the moments, it is only sent on maintenance loopback xmt
		/* read serial data, if any */
		if (rs232adapter.rs232byte_rcv_poll(&rcv_byte)) {
			DEBUG("rcv_byte=0x%02x", (unsigned)rcv_byte.c);
			pthread_mutex_lock(&on_after_rcv_register_access_mutex); // signal changes atomic against QBUS/UNIBUS accesses
			rcv_or_err = rcv_fr_err = rcv_p_err = 0;
			if (rcv_done) { // not yet cleared? overrun!
				rcv_or_err = 1;
				DEBUG("RCV OVERRUN");
			}
			rcv_buffer = rcv_byte.c;
			if (rcv_byte.format_error)
				rcv_fr_err = rcv_p_err = 1;
			rcv_done = 1;
			rcv_active = 0;
			set_rbuf_dati_value();
			set_rcsr_dati_value_and_INTR(); // INTR!
			pthread_mutex_unlock(&on_after_rcv_register_access_mutex); // signal changes atomic against QBUS/UNIBUS accesses
		}
	}
}

void slu_c::worker_xmt(void) {
	timeout_c timeout;

	assert(!pthread_mutex_lock(&on_after_register_access_mutex));

	// Transmitter not time critical
	worker_init_realtime_priority(rt_device);

	while (!workers_terminate) {
		// 1. wait for xmt signal
		int res = pthread_cond_wait(&on_after_xmt_register_access_cond,
				&on_after_xmt_register_access_mutex);
		// on_after_xmt_register_access_mutex remains locked all the time
		if (res != 0) {
			ERROR("SLU::worker_xmt() pthread_cond_wait = %d = %s>", res, strerror(res));
			continue;
		}

		// 2. transmit
		rs232byte_t xmt_byte;
		xmt_byte.c = xmt_buffer;
		xmt_byte.format_error = false;
		rs232adapter.rs232byte_xmt_send(xmt_byte);
		xmt_ready = 0;
		set_xcsr_dati_value_and_INTR();
		if (xmt_maint) { // loop back: simulate data byte coming in
			pthread_mutex_lock(&on_after_rcv_register_access_mutex);
			rcv_active = 1;
			set_rcsr_dati_value_and_INTR();
			pthread_mutex_unlock(&on_after_rcv_register_access_mutex);
		}

		// 3. wait for data byte being shifted out
		pthread_mutex_unlock(&on_after_xmt_register_access_mutex);
		timeout.wait_us(rs232.CharTransmissionTime_us);
		pthread_mutex_lock(&on_after_xmt_register_access_mutex);
		if (xmt_maint)
			// put sent byte into rcv buffer, receiver will poll it
			rs232adapter.rs232byte_loopback(xmt_byte);
		xmt_ready = 1;
		set_xcsr_dati_value_and_INTR();

		// has rcv or xmt interrupt priority on maintennace loop back
	}
	assert(!pthread_mutex_unlock(&on_after_xmt_register_access_mutex));
}

void slu_c::worker(unsigned instance) {
	// 2 parallel worker() instances: 0 and 1 
	if (instance == 0)
		worker_rcv();
	else
		worker_xmt();

}

//--------------------------------------------------------------------------------------------------

ltc_c::ltc_c() :
		qunibusdevice_c()  // super class constructor
{

	// static config
	name.value = "KW11";
	type_name.value = "ltc_c";
	log_label = "ltc";
	// slot = 3:
	set_default_bus_params(LTC_ADDR, LTC_SLOT, LTC_VECTOR, LTC_LEVEL); // base addr, intr-vector, intr level

	// controller has only one register
	register_count = 1;
	reg_lks = &(this->registers[0]); // @  base addr
	strcpy(reg_lks->name, "LKS"); // Line Clock Status Register
	reg_lks->active_on_dati = false; // status polled by CPU, not active
	// reg_lks->active_on_dati = true; // debugging
	reg_lks->active_on_dato = true;
	reg_lks->reset_value = LKS_INT_MON;
	reg_lks->writable_bits = LKS_INT_ENB | LKS_INT_MON; // interrupt enable

	// init parameters
	frequency.value = 50;
	ltc_enable.value = true;

	// init controller state	
	intr_enable = 0;
	line_clock_monitor = 0;
}

ltc_c::~ltc_c() {
}

bool ltc_c::on_param_changed(parameter_c *param) {
	// no own parameter or "enable" logic here
	if (param == &frequency) {
		// allow all values, but complain
		if (frequency.new_value != 50 && frequency.new_value != 60)
			WARNING("KW11 non-standard clock value %d, regular 50 or 60", frequency.new_value);
		// return (frequency.new_value == 50 || frequency.new_value == 60);
	} else if (param == &priority_slot) {
		intr_request.set_priority_slot(priority_slot.new_value);
	} else if (param == &intr_level) {
		intr_request.set_level(intr_level.new_value);
	} else if (param == &intr_vector) {
		intr_request.set_vector(intr_vector.new_value);
	}

	return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}

// set status register, and optionally generate INTR
// intr_raise: if inactive->active transition of interrupt condition detected.
void ltc_c::set_lks_dati_value_and_INTR(bool do_intr) {
	uint16_t val = (line_clock_monitor ? LKS_INT_MON : 0) | (intr_enable ? LKS_INT_ENB : 0);
	if (do_intr)
		// set register atomically with INTR, if INTR not blocked
		qunibusadapter->INTR(intr_request, reg_lks, val);
	else
		// set unrelated to INTR condition
		set_register_dati_value(reg_lks, val, __func__);
}

// process DATI/DATO access to one of my "active" registers
void ltc_c::on_after_register_access(qunibusdevice_register_t *device_reg,
		uint8_t unibus_control) {
	pthread_mutex_lock(&on_after_register_access_mutex);
// not necessary, not harmful?
	if (unibus_control == QUNIBUS_CYCLE_DATO) // bus write
		set_register_dati_value(device_reg, device_reg->active_dato_flipflops, __func__);

	switch (device_reg->index) {

	case 0: // LKS
		if (unibus_control == QUNIBUS_CYCLE_DATO) { // bus write
//DEBUG("LKS wrDATO, val = %06o", reg_lks->active_dato_flipflops) ;
			intr_enable = !!(reg_lks->active_dato_flipflops & LKS_INT_ENB);
			// schematic: LINE CLOCK MONITOR can only be cleared
			if ((reg_lks->active_dato_flipflops & LKS_INT_MON) == 0)
				line_clock_monitor = 0;
			if (!intr_enable || !line_clock_monitor) {
				// BR6 is tied to monitor and enable, so raised INTRs may get canceled
				qunibusadapter->cancel_INTR(intr_request);
			}
			set_lks_dati_value_and_INTR(false); // INTR only by clock, not by LKs access
		} else
//DEBUG("LKS DATI, control=%d, val = %06o = %06o", (int)unibus_control, reg_lks->active_dati_flipflops, device_reg->pru_iopage_register->value ) ;
			break;

	default:
		break;
	}
	pthread_mutex_unlock(&on_after_register_access_mutex);

}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void ltc_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	UNUSED(aclo_edge);
	UNUSED(dclo_edge);
}

// QBUS/UNIBUS INIT: clear all registers
void ltc_c::on_init_changed(void) {
	// write all registers to "reset-values"
	if (init_asserted) {
		reset_unibus_registers();
		intr_enable = 0;
		line_clock_monitor = 1;
		intr_request.edge_detect_reset(); // but edge_detect() not used
		// initial condition is "not signaled"
		// INFO("ltc_c::on_init()");
		world_time_since_init.start_ns(0);
		clock_ticks_produced_since_init = 0;
	}
}

/*
 Adpative clock period.
 Problem: execution of an emulated CPU is
 1) slower than a real CPU
 2) may be stopped by Linux scheduling.
 PDP-11 Code may assume silently that clock INTR occurs about every 50.000 CPU cycles
 (and ticks every 25.000 cycles)
 So
 - for an emulated CPU clock ticks must be produced in "cpu time", not in "world time".
 - clock ticks are used to drive real time clocks for PDP11 OSses, so msut run in "world time""
 These colliding requirements can be solved by
 - keeping track how many "world time" ticks should have generated
 - issuing emulated ticks faster then 50.000 CPU cycles, so the emulated system
 runs with "faster ticks" after beeing stopped until it catches uop with world time.
 */

/* background worker.
  Frequency of clock signal edges is tied to absolute system time, not to "wait" periods
  This worker may get delayed arbitray amount of time (as every thread), 
  lost edges are compensated.
 */
void ltc_c::worker(unsigned instance) {
	UNUSED(instance); // only one
	int64_t global_edge_count = 0;
	flexi_timeout_c timeout; // world time or driven by CPU cycles
	int64_t world_next_intr_ns;

// set prio to RT, but less than unibus_adapter
	worker_init_realtime_priority(rt_device);

	INFO("KW11 time resolution is < %u us", (unsigned )(timeout.get_resolution_ns() / 1000));

	world_next_intr_ns = the_flexi_timeout_controller->world_now_ns();
	while (!workers_terminate) {
		// 1. Generate INTR
		if (ltc_enable.value) {
			global_edge_count++; // debugging
			
			line_clock_monitor = 1;
			pthread_mutex_lock(&on_after_register_access_mutex);
			set_lks_dati_value_and_INTR(intr_enable);
			pthread_mutex_unlock(&on_after_register_access_mutex);
		}

		// 2. Calculate next INTR time
		// signal period as setup by LTC param. may be changed by user, so recalc every loop.
		int64_t intr_period_ns = (int) (BILLION / frequency.value);
		
		int64_t now_ns = the_flexi_timeout_controller->world_now_ns();

		// due to worker() scheduling, INTR signal generated is normally delayed.
		int64_t missing_ns = now_ns - world_next_intr_ns; // now_ns always > desired INTR time
		// missing_ns may grow infinitely, if worker() is permanentely too slow
		
		// wait shorter than intr_period_ns to catch up with worker() delays
		// may even be negative, if worker() delayed too long!
		int64_t wait_time_ns = intr_period_ns - missing_ns;
		// however, wait always a minimum (half period) of ns.
		if (wait_time_ns < intr_period_ns / 2)
			wait_time_ns = intr_period_ns / 2;
		// next INTR should occur at this time
		world_next_intr_ns += intr_period_ns;

		// toggle each iNTR, so output half frequency
		// ARM_DEBUG_PIN0(global_edge_count & 1) ;

		// Test average frequency
		if (global_edge_count && (global_edge_count %  frequency.value) == 0)
			DEBUG("LTC: %u secs by INTR", (unsigned)( global_edge_count/ frequency.value) ) ;
		
		// wait for next clock event
		timeout.wait_ns(wait_time_ns);

#ifdef ORG		
		// signal egde period may change if 50/60 Hz is changed
		uint64_t edge_period_ns = BILLION / (2 * frequency.value);
		// overdue_ns: time which signal edge is too late
		int64_t overdue_ns = (int64_t) global_time.elapsed_ns() - world_next_intr_ns;
		// INFO does not work on 64 ints
		// printf("elapsed [ms] =%u, overdue [us] =%u\n", (unsigned) global_time.elapsed_ms(), (unsigned) overdue_ns/1000) ;
		// if overdue_ns positive, next signal edge should have occured
		if (overdue_ns < 0) {
			wait_ns = -overdue_ns; // wait until next edge time reached
		} else {
			// time for next signal edge reached
			if (ltc_enable.value) {
				global_edge_count++;
				clock_signal = !clock_signal; // square wave
				if (clock_signal) {
					line_clock_monitor = 1;
					pthread_mutex_lock(&on_after_register_access_mutex);
					set_lks_dati_value_and_INTR(intr_enable);
					pthread_mutex_unlock(&on_after_register_access_mutex);
				}
			} else
			// clock disconnected
			clock_signal = 0;

			// time of next signal edge
			world_next_intr_ns += edge_period_ns;
			// overdue_ns now time which next signal edge is too late
			overdue_ns -= edge_period_ns;

			if (overdue_ns < 0)
			// next edge now in future: wait exact
			wait_ns = -overdue_ns;
			else
			// next edge still in past:
			// wait shorter than signal edge period to keep up slowly
			wait_ns = edge_period_ns / 2;
			//if ((global_edge_count % 100) == 0)
			//	INFO("LTC: %u secs by edges", (unsigned)(global_edge_count/100) ) ;
		}
		timeout.wait_ns(wait_ns);
#endif		
	}
}

/* Test plan (for 50Hz / 20ms)
 1. When flexi_timeout is driveen by real world clock
 1.1. INTR period must not be shorter than 10ms
 1.2. average INTR period must be 20ms
 
 2. When flexi_timeout is driveen by simuleted CPU MSYN
 2.1. INTR period must not be shorter than "10ms" = 10.000 cycles
 2.2. average INTR period must be "20ms" = 20.000 cycles

 */
