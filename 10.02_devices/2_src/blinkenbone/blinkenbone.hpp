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

#include "qunibusdevice.hpp"

class blinkenbone_c: public qunibusdevice_c {
private:
    // global API client
    blinkenlight_api_client_t *blinkenlight_api_client = nullptr;

    blinkenlight_panel_t *panel = nullptr ; // active panel

    // static panel config registers
    // bits <15:14> selftest:  0="normal", 1="historic lamp test", 2="full test", 3="powerless"
    // bits <13:0>  panel_id. arbitrary value to communicate panel properties to PDP-11 software
    //			    Typical set by QUnibone config script before panel connection
    qunibusdevice_register_t *config_reg;
    uint16_t config_value_previous ; // to detect changing DATAos

    // control mapping registers start at base+2
    uint16_t fix_register_count ;

    // cross table entry between 16bit PDP-11 registers and 64bit blinkenlight controls
    // for each panel control the list of necessary 16bit PDP-11 registers is listed
    // calculated on "before_install()" when panel is known.
    class control_value_slice_register_c     {
    public:
        unsigned bit_index_from, bit_index_to ; // range of bits held here
        qunibusdevice_register_t *pdp11_reg ; // the PDP-11 register

        unsigned bit_len() { // # of valid bits
            assert(bit_index_to >= bit_index_from) ;
            int result = bit_index_to - bit_index_from + 1 ;
            assert(result > 0 && result <= 16) ; // must be 1..16 bits per pdp11-pdp11_reg
            return (unsigned)result ;
        }

        // print bit range like "<0>" or "<8:5>" or "<15:00>"
        char *bits_text() { // # of valid bits
            static char buffer[40] ;
            if (bit_len() == 1)
                sprintf(buffer, "<%d>", bit_index_from) ;
            else {
                unsigned digit_count = (bit_index_to > 9) ? 2 : 1 ;
                sprintf(buffer, "<%0*d:%0*d>", digit_count, bit_index_to, digit_count, bit_index_from) ;
            }
            return buffer ;
        }

    }  ;

    class control_register_set_c {
    public:
        blinkenlight_control_t *control ; // back link
        // control->panel and control->value_bitlen, value
        unsigned register_count ; // # of 16 bit registers to represent control bits
        // a control value len varies from 1 bit -> PDP-11 register to 36 bit (PDP-10): 3 PDP-11 registers
        control_value_slice_register_c control_value_slice_registers[4] ; // max 4*16=64 bit controls allowed
    } ;

    static const unsigned max_input_controls_count = MAX_IOPAGE_REGISTERS_PER_DEVICE/4 ;
    unsigned input_controls_count ;
    control_register_set_c input_control_register_sets[max_input_controls_count];

    static const unsigned max_output_controls_count = MAX_IOPAGE_REGISTERS_PER_DEVICE/4 ;
    unsigned output_controls_count ;
    control_register_set_c output_control_register_sets[max_output_controls_count] ;

    unsigned update_prescaler_ms ; // scale down worker() running with 1kHz
    unsigned poll_prescaler_ms ;

    void worker_config_changed();
    void worker_poll_inputs() ;
    void worker_update_outputs() ;

    void build_control_register_set(control_register_set_c *crs, blinkenlight_control_t *c) ;
    void print_server_info() ;
    void print_register_info() ;

    void set_output_update_need(bool forced_update_state) ;
    bool outputs_need_update() ;
    void input_controls_to_registers() ;
    void registers_to_output_controls() ;



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
                                      "", "%d", "Address of panel Blinkenlight server", 8, 10);
    parameter_unsigned_c panel_id = parameter_unsigned_c(this, "panel_id", "pid",/*readonly*/ false,
                                        "", "%o", "Custom ID for CONFIG register <13:0>", 14, 8);
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
