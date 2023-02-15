/* DL11W.hpp: sample QBUS/UNIBUS controller with SLU & LTC logic

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
 20/12/2018 djrm copied to make DL11-W device
 */
#ifndef _DL11W_HPP_
#define _DL11W_HPP_

#include <fstream>

#include "utils.hpp"
#include "qunibusdevice.hpp"
#include "parameter.hpp"
#include "rs232.hpp"
#include "rs232adapter.hpp"

// socket console settings
//#define IP_PORT 5001
//#define IP_HOST "localhost"

// bus properties
#define DL11A 1
#if DL11A // console (teletype keyboard & printer)
#define SLU_ADDR	0777560
#define SLU_SLOT	1	// Close to CPU.  RCV, also SLOT+1 is used for XMT
#define SLU_LEVEL	04
#define SLU_VECTOR	060	// RCV +0, XMT +4
#elif DL11B // paper tape punch and reader
#define SLU_SLOT	1	// Close to CPU.  RCV, also SLOT+1 is used for XMT
#define SLU_ADDR	0777550
#define SLU_LEVEL	04
#define SLU_VECTOR	070
#else // other serial device
#define SLU_ADDR	0776500
#define SLU_SLOT	1	// Close to CPU.  RCV, also SLOT+1 is used for XMT
#define SLU_LEVEL	04
#define SLU_VECTOR	0300
#endif
#define LTC_ADDR	0777546
#define LTC_SLOT	(SLU_SLOT+2) 
#define LTC_LEVEL   06
#define LTC_VECTOR  0100

// global text buffer size for hostname etc
#define BUFLEN 32

// register bit definitions
#define RCSR_RCVR_ACT    	0004000
#define RCSR_RCVR_DONE		0000200
#define RCSR_RCVR_INT_ENB   0000100
#define RCSR_RDR_ENB		0000001

#define RBUF_ERROR			0100000
#define RBUF_OR_ERR			0040000
#define RBUF_FR_ERR			0020000
#define RBUF_P_ERR			0010000

#define XCSR_XMIT_RDY		0000200
#define XCSR_XMIT_INT_ENB   0000100
#define XCSR_MAINT			0000004
#define XCSR_BREAK          0000001

#define LKS_INT_ENB			0000100
#define LKS_INT_MON			0000200

// background task sleep times
#define SLU_MSRATE_MS  10
#define LTC_MSRATE_MS  50

// qunibus register indices
enum slu_reg_index {
	slu_idx_rcsr = 0, slu_idx_rbuf, slu_idx_xcsr, slu_idx_xbuf, slu_idx_count,
};

// ------------------------------------------ SLU -----------------------------
class slu_c: public qunibusdevice_c {
private:
	rs232_c rs232; /// COM port interface
public:
	rs232adapter_c rs232adapter; /// stream router

private:
	qunibusdevice_register_t *reg_rcsr;
	qunibusdevice_register_t *reg_rbuf;
	qunibusdevice_register_t *reg_xcsr;
	qunibusdevice_register_t *reg_xbuf;

	// two interrupts of same level, need slot and slot+1
	intr_request_c rcvintr_request = intr_request_c(this);
	intr_request_c xmtintr_request = intr_request_c(this);

	/*** SLU is infact 2 independend devices: RCV and XMT ***/
	pthread_cond_t on_after_rcv_register_access_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t on_after_rcv_register_access_mutex = PTHREAD_MUTEX_INITIALIZER;

	// bits in registers
	bool rcv_active; /// while a char is receive ... not available
	bool rcv_done; // char received. INTR. cleared by rdr_enable, access to rbuf, init
	bool rcv_overrun;bool rcv_intr_enable; // receiver interrupt enabled
	bool rcv_or_err; // receiver overrun: rcv_done 1 on receive
	bool rcv_fr_err; // framing error. high on received BREAK
	bool rcv_p_err; // parity error
	uint8_t rcv_buffer;bool rcv_rdr_enb; // reader enable. Cleared by receive or init

	pthread_cond_t on_after_xmt_register_access_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t on_after_xmt_register_access_mutex = PTHREAD_MUTEX_INITIALIZER;bool xmt_ready; // transmitter ready. INTR,  cleared on XBUF access
	bool xmt_intr_enable; // receiver interrupt enabled

	bool xmt_maint; // set 1 for local loop back
	bool xmt_break; // transmit continuous break
	uint8_t xmt_buffer;

	// convert between register ansd state variables	
	bool get_rcv_intr_level(void);bool get_xmt_intr_level(void);

	void set_rcsr_dati_value_and_INTR(void);
	void eval_rcsr_dato_value(void);
	void set_rbuf_dati_value(void);
	void set_xcsr_dati_value_and_INTR(void);
	void eval_xcsr_dato_value(void);
	void eval_xbuf_dato_value(void);

public:

	slu_c();
	~slu_c();

	bool on_before_install(void) override ;
	void on_after_uninstall(void) override ;

	//parameter_string_c   ip_host = parameter_string_c(  this, "SLU socket IP host", "host", /*readonly*/ false, "ip hostname");
	//parameter_unsigned_c ip_port = parameter_unsigned_c(this, "SLU socket IP serialport", "serialport", /*readonly*/ false, "", "%d", "ip serialport", 32, 10);
	parameter_string_c serialport = parameter_string_c(this, "serialport", "p", /*readonly*/
	false, "Linux serial port: \"ttyS1\" or \"ttyS2\"");

	parameter_unsigned_c baudrate = parameter_unsigned_c(this, "baudrate", "b", /*readonly*/
	false, "", "%d", "Baudrate: 110, 300, ... 38400", 16, 10);
	// 40kbaud -> 25us bit polling period needed

	parameter_string_c mode = parameter_string_c(this, "mode", "m", /*readonly*/false,
			"Mode: 8N1, 7E1, ... ");

	parameter_bool_c error_bits_enable = parameter_bool_c(this, "errorbits", "eb", /*readonly*/
	false, "Enable error bits (M7856 SW4-7)");

	parameter_bool_c break_enable = parameter_bool_c(this, "break", "b", /*readonly*/false,
			"Enable BREAK transmission (M7856 SW4-1)");

	// 		

	// background worker function
	void worker(unsigned instance) override;
	void worker_rcv(void);
	void worker_xmt(void);

	// called by qunibusadapter on emulated register access
	void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control, DATO_ACCESS access)
			override;

	bool on_param_changed(parameter_c *param) override;  // must implement
	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
	void on_init_changed(void) override;
};

//-------------------------------------------- LTC -------------------------------------
class ltc_c: public qunibusdevice_c {
private:

	qunibusdevice_register_t *reg_lks;

	// KW11 has one interrupt
	intr_request_c intr_request = intr_request_c(this);

	bool intr_enable; // interrupt enable, LKS bit 6
	bool line_clock_monitor; // LKS bit 7

	bool get_intr_signal_level(void);
	void set_lks_dati_value_and_INTR(bool do_intr);

	// Adaptive clock ticks	:
	// track world time since last INIT
	timeout_c	world_time_since_init ;
	// # of power supply square wave edges emulated so far
	// overflow: 2^32 @ 120 Hz -> 414 Tage
	uint32_t clock_ticks_produced_since_init ;
	
public:

	ltc_c();
	~ltc_c();

	parameter_unsigned_c frequency = parameter_unsigned_c(this, "Line clock frequency", "freq", /*readonly*/
	false, "", "%d", "50/60 Hz", 32, 10);
	parameter_bool_c ltc_enable = parameter_bool_c(this, "LTC input enable", "ltc",/*readonly*/
	false, "1 = enable update of LKS by LTC Input");

	// background worker function
	void worker(unsigned instance) override;

	// called by qunibusadapter on emulated register access
	void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control, DATO_ACCESS access)
			override;

	bool on_param_changed(parameter_c *param) override;  // must implement
	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
	void on_init_changed(void) override;
};

#endif
