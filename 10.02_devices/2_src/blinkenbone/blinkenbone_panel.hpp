/* blinkenbone_panel.hpp: wrapper for Blinkenlight API panel

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

 4-aug-2025  JH      edit start
*/
#ifndef _BLINKENBONE_PANEL_HPP_
#define _BLINKENBONE_PANEL_HPP_

#include <map>
#include <vector>
#include <string>

#include "logsource.hpp"
#include "blinkenlight_api_client.h"

class blinkenbone_c ;

class blinkenbone_panel_c: public logsource_c {
public:
    // cross table entry between 16bit PDP-11 registers and 64bit blinkenlight controls
    // for each panel control the list of necessary 16bit PDP-11 registers is listed
    // calculated on "before_install()" when panel is known.
    class control_value_slice_register_c     {
    public:
        unsigned bit_index_from, bit_index_to ; // range of bits held here
        qunibusdevice_register_t *pdp11_reg ; // the PDP-11 register
        // control->value_previous doesn't identify the changed register,
        // so we need own change logic
        uint16_t value_previous ;

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

        bool is_input() const {
            return control->is_input ;
        }
    } ;

private:
    blinkenbone_c *device; // master qunibusdevice_c device
    // global API client
    blinkenlight_api_client_t *blinkenlight_api_client = nullptr;
    // database is map of vectors
    // map key is panel name, name vector is ordered list of control names
    std::map<std::string, std::vector<std::string>> db ;

    bool server_error ; // true, if no connection to server panel

public:
    std::string	hostname ;
    unsigned panel_addr ;
    blinkenlight_panel_t *panel = nullptr ;

    // static const unsigned max_input_controls_count = MAX_IOPAGE_REGISTERS_PER_DEVICE/4 ;
    // static const unsigned max_output_controls_count = MAX_IOPAGE_REGISTERS_PER_DEVICE/4 ;

    // order list of panel->controls
    std::vector<control_register_set_c> control_register_sets;
    int controls_count() const {
        return control_register_sets.size() ;
    }

    uint16_t testmode ; // as set

    blinkenbone_panel_c(		blinkenbone_c *_device) ;

    ~blinkenbone_panel_c() ;
    void connect(std::string _hostname, unsigned _panel_addr) ;
    bool connected() {
        return (!server_error && panel != nullptr) ;
    }
    void disconnect() ;

private:
    void build_control_register_set(control_register_set_c *crs, blinkenlight_control_t *c) ;
    void build_controls() ;

public:
    void print_server_info() ;
    void print_register_info() ;

    void set_testmode(uint16_t _testmode) ;

    void get_inputcontrols_values() ;
    void set_outputcontrols_values() ;

    uint32_t get_input_changed_and_clear() ;
    void set_output_changed(bool forced_update_state) ;
    bool has_output_changed() ;

    void input_panel_controls_to_registers() ;
    void registers_to_panel_output_controls() ;
} ;

#endif // _BLINKENBONE_PANEL_HPP_

