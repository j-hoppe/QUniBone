/* blinkenbone.cpp: map BlinkenBone panel into PDP-11 I/O page

 Copyright (c) 2025, Joerg Hoppe
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


 15-jul-2025  JH      edit start
 */
#ifndef _BLINKENBONE_HPP_
#define _BLINKENBONE_HPP_

#include <assert.h>
#include "utils.hpp"
#include "blinkenlight_api_client.h"

#include "blinkenbone_panel.hpp"

#include "qunibusdevice.hpp"

class blinkenbone_c: public qunibusdevice_c {
    friend class blinkenbone_panel_c ; // allow panel to work with registers
private:

    blinkenbone_panel_c *panel = nullptr ; // the blinkenlight API panel

    // static registers, modelled after DL11, KW11

    // +0  PANEL_ICS, input side Control and Status
    // bit 7: flag: any input changed, readonly, clered by read
    // bit 6: input interrupt enable
    qunibusdevice_register_t *reg_input_cs;

    //+2  PANEL_OCS, output side CS
    // bit 7: flag: output perdioc event passed, clear on read
    // bit 6: output interrupt enable
    // 1:0 testmode
    qunibusdevice_register_t *reg_output_cs;

    // +4 PANEL_IPERIOD, input polling period in ms
    // 0...1000 set polling period in ms (read only)
    qunibusdevice_register_t *reg_input_period;

    // +6 PANEL_OPERIOD, output update period in ms
    // 0...1000 set update frequency period in ms (read only)
    qunibusdevice_register_t *reg_output_period;

    // +010 PANEL_ICHGREG
    // if polling detected changed in put switch, this is the address
    // of the mapped register
    qunibusdevice_register_t *reg_input_change_addr ;

    // + 012 PANEL_CONFIG,  value from panel_config parameter
    qunibusdevice_register_t *reg_config;

    // two interrupts of same level, need slot and slot+1
    intr_request_c intr_request_input_change = intr_request_c(this);
    intr_request_c intr_request_output_period = intr_request_c(this);


    /*** SLU is infact 2 independend devices: RCV and XMT ***/
    // pthread_cond_t on_after_input_register_access_cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t on_after_input_register_access_mutex = PTHREAD_MUTEX_INITIALIZER;
    //pthread_cond_t on_after_output_register_access_cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t on_after_output_register_access_mutex = PTHREAD_MUTEX_INITIALIZER;

    // control mapping registers start at base+2
    uint16_t fix_register_count ;

    // state signals, visible in CS regs
    bool state_input_event ; // 0->1 = iNT
    bool state_input_interrupt_enable ;
    bool state_output_event ; // 0->1 = iNT
    bool state_output_interrupt_enable ;
    uint16_t state_testmode ;

    unsigned update_prescaler_ms ; // scale down worker() running with 1kHz
    unsigned poll_prescaler_ms ;

    void print_register_info() ;

    void worker_testmode_changed();
    void worker_input_poll() ;
    void worker_output_update() ;

    bool get_input_intr_level() ;
    void set_input_csr_dati_value_and_INTR();
    bool get_output_intr_level() ;
    void set_output_csr_dati_value_and_INTR() ;

public:
    blinkenbone_c();
    ~blinkenbone_c();

    bool on_param_changed(parameter_c *param) override;  // must implement

    bool on_before_install(void) override ;
    void on_after_install(void) override ;
    void on_after_uninstall(void) override ;

    parameter_string_c panel_host = parameter_string_c(this, "panel_host", "ph",/*readonly*/
                                    false, "hostname of Blinkenlight server, the computer running the panel .. physical, Java or PiDP11");
    parameter_unsigned_c panel_addr = parameter_unsigned_c(this, "panel_addr", "pa",/*readonly*/ false,
                                      "", "%d", "Address of panel in the Blinkenlight server", 8, 10);
    parameter_unsigned_c panel_config = parameter_unsigned_c(this, "panel_config", "pc",/*readonly*/ false,
                                        "", "%o", "Custom CONFIG register", 16, 8);
    parameter_unsigned_c poll_period_ms = parameter_unsigned_c(this, "poll_period", "pp",/*readonly*/ false,
                                          "", "%d", "Panel switches are polled every so many milliseconds. 0=disable.", 10, 10);
    parameter_unsigned_c update_period_ms = parameter_unsigned_c(this, "update_period", "up",/*readonly*/ false,
                                            "", "%d", "Panel lamps are updated every so many milliseconds. 0=disable.", 10, 10);
    // background worker function
    void worker(unsigned instance) override;

    // called by qunibusadapter on emulated register access
    void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control, DATO_ACCESS access)
    override;

    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;
};

#endif
