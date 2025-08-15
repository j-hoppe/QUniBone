/* blinkenbone_panels.cpp: wrapper for Blinkenlight API panel

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


 Handels the control for a blinkenbone panel and the register mapping
 DB:
 Defines for each known panel type the order of controls.
 Needed, as for a certain panel (eg  PDP11/70) the order of
 controls as listed by the server varies among implementations.
 For PiDP11, Java panel and physical 11/70 only the naming of
 the controls is guranteed ("SR", "ADDRESS", ...) to be identical,
 not the order.
 The order of controls for a given panel is defined here.

 Implemented as hashmap of vectors
 hash key is the panel name as published by the server ("11/70")
 vector is the ordered list of control names.

*/
#include <assert.h>

#include "logger.hpp"
#include "bitcalc.h"

#include "qunibusdevice.hpp"
#include "blinkenbone.hpp"
#include "blinkenbone_panel.hpp"


blinkenbone_panel_c::blinkenbone_panel_c(blinkenbone_c *_device) {
    device = _device ;
    // panel diag same verbosity as master blinkenbone device
    log_level_ptr = device->log_level_ptr ;

    // fill database
    // key string is panel name, as given by server
    db["11/20"] =  {
        // see simh/.../src/REALCONS/realcons_console_pdp11_20.c
        // switches
        "POWER", "PANEL_LOCK", "SR", "LOAD_ADDR", "EXAM", "CONT", "HALT", "SCYCLE", "START", "DEPOSIT",
        // lamps
        "ADDRESS", "DATA", "RUN", "BUS", "FETCH", "EXEC", "SOURCE", "DESTINATION", "ADDRESS_CYCLE"
    } ;

    db["11/40"] =  {
        // see simh/.../src/REALCONS/realcons_console_pdp11_40.c
// switches
        "SR", "LOAD ADRS", "EXAM", "CONT", "HALT", "START", "DEPOSIT",
        // lamps
        "ADDRESS", "DATA", "RUN", "BUS", "USER", "PROCESSOR", "CONSOLE", "VIRTUAL"
    } ;

    db["11/70"] =  {
        // see simh/.../src/REALCONS/realcons_console_pdp11_70.c
        // switches = input controls
        "SR","LOAD_ADRS","EXAM", "DEPOSIT", "CONT", "HALT", "S_BUS_CYCLE", "START", //
        "ADDR_SELECT", "DATA_SELECT", "PANEL_LOCK",
        // lamps = output controls
        "ADDRESS", "DATA", "PARITY_HIGH", "PARITY_LOW",  //
        "PAR_ERR", "ADRS_ERR", "RUN", "PAUSE", "MASTER", "MMR0_MODE", "DATA_SPACE", "ADDRESSING_16", "ADDRESSING_18", "ADDRESSING_22"
    } ;

    db["PDP8I"] =  {
        // see simh/.../src/REALCONS/realcons_console_pdp8i.c
        // switches
        "POWER", "PANEL LOCK", "Start", "Load Add", "Dep", "Exam", "Cont", "Stop", "Sing_Step", "Sing_Inst", "SR", "DF", "IF",
        // lamps
        "Program_Counter", "Inst_Field", "Data_Field", "Memory_Address", "Memory_Buffer", //
        "Link", "Accumulator",  "Step_Counter", "Multiplier_Quotient",  //
        "And", "Tad", "Isz", "Dca", "Jms", "Jmp", "Iot", "Opr", //
        "Fetch", "Execute", "Defer", "Word_Count", "Current_Address", "Break", //
        "Ion", "Pause", "Run"
    } ;

    db["PDP15"] =  {
        // see simh/.../src/REALCONS/realcons_console_pdp15.c
// *** Switch Board ***
        "STOP", "RESET", "READ_IN", "START", "EXECUTE", "CONT", //
        "REG_GROUP", "CLOCK", "BANK_MODE", "REPT", "PROT", "SING_TIME", "SING_STEP", "SING_INST", //
        "DEPOSIT_THIS", "EXAMINE_THIS", "DEPOSIT_NEXT", "EXAMINE_NEXT", //
        "ADDRESS", "DATA", //
        "POWER", "REPEAT_RATE", "REGISTER_SELECT",
        // *** Indicator Board ***
        "DCH_ACTIVE", "API_STATES_ACTIVE", "API_ENABLE", "PI_ACTIVE", "PI_ENABLE", "MODE_INDEX", //
        "STATE_FETCH", "STATE_INC", "STATE_DEFER", "STATE_EAE", "STATE_EXEC", "TIME_STATES", //
        "EXTD", "CLOCK", "ERROR", "PROT", //
        "LINK", "REGISTER", //
        "POWER", "RUN", "INSTRUCTION", "INSTRUCTION_DEFER", "INSTRUCTION_INDEX", "MEMORY_BUFFER"
    } ;

    db["PDP10-KI10"] =  {
        // see simh/.../src/REALCONS/realcons_pdp10.c
        // more panel order, and button switches and button - lamps separated. may change.
        // ---------- outputs/switches upper panel --------------------------
        "FM_MANUAL_SW", "FM_BLOCK_SW", "SENSE_SW", //
        "MI_PROG_DIS_SW", "MEM_OVERLAP_DIS_SW", "SINGLE_PULSE_SW", "MARGIN_ENABLE_SW", "MANUAL_MARGIN_ADDRESS_SW", //
        "READ_IN_DEVICE_SW", //
        "MARGIN_VOLTAGE", "MARGIN_SELECT", //
        "LAMP_TEST_SW", "CONSOLE_LOCK_SW", "CONSOLE_DATALOCK_SW", "POWERBUTTON_SW", //
        "IND_SELECT", "SPEED_CONTROL_COARSE", "SPEED_CONTROL_FINE",
        // ---------- button feed back lamp/indicators on upper panel --------------------------
        "FM_MANUAL_FB", "FM_BLOCK_FB", "SENSE_FB", //
        "MI_PROG_DIS_FB", "MEM_OVERLAP_DIS_FB",	"SINGLE_PULSE_FB", "MARGIN_ENABLE_FB", "MANUAL_MARGIN_ADDRESS_FB", //
        "READ_IN_DEVICE_FB", //
        "OVERTEMP", "CKT_BRKR_TRIPPED", "DOORS_OPEN", "VOLTMETER", //
        "HOURMETER",

        // ----- buttons on lower panel --------------
        // "In the upper half of the operator panel are four rows of indicators, and below them are three
        // rows of two-position keys and switches. Physically both are pushbuttons, but the keys are
        // momentary contact whereas the switches are alternate action.""
        "PAGING_EXEC_SW", "PAGING_USER_SW", "ADDRESS_SW", "ADDRESS_CLEAR_SW", "ADDRESS_LOAD_SW", //
        "DATA_SW", "DATA_CLEAR_SW","DATA_LOAD_SW", //s
        "SINGLE_INST_SW", "SINGLE_PULSER_SW", "STOP_PAR_SW", "STOP_NXM_SW", "REPEAT_SW", //
        "FETCH_INST_SW", "FETCH_DATA_SW", "WRITE_SW", "ADDRESS_STOP_SW", "ADDRESS_BREAK_SW", //
        "READ_IN_SW", "START_SW", "CONT_SW", "STOP_SW", "RESET_SW", "XCT_SW", //
        "EXAMINE_THIS_SW", "EXAMINE_NEXT_SW", "DEPOSIT_THIS_SW", "DEPOSIT_NEXT_SW"

        // ---------- LEDs on lower panel --------------------------
        "PI_ACTIVE", "PI_IN_PROGRESS", "IOB_PI_REQUEST", "PI_REQUEST",  "PI_ON", "PI_OK_8", //
        "MODE",  "KEY_PG_FAIL", "KEY_MAINT", "STOP", "RUN", "POWER",
        "PROGRAM_COUNTER", "INSTRUCTION", "MEMORY_DATA", "PROGRAM_DATA",  "DATA", //
        // ---------- feed backbutton lamps on lower panel --------------------------
        "PAGING_EXEC_FB", "PAGING_USER_FB", "ADDRESS_FB", "ADDRESS_CLEAR_FB", "ADDRESS_LOAD_FB", //
        "DATA_FB", "DATA_CLEAR_FB",	"DATA_LOAD_FB", //
        "SINGLE_INST_FB", "SINGLE_PULSER_FB", "STOP_PAR_FB", "STOP_NXM_FB", "REPEAT_FB", //
        "FETCH_INST_FB", "FETCH_DATA_FB", "WRITE_FB", "ADDRESS_STOP_FB", "ADDRESS_BREAK_FB", //
        "READ_IN_FB", "START_FB", "CONT_FB", "STOP_FB", "RESET_FB", "XCT_FB", //
        "EXAMINE_THIS_FB", "EXAMINE_NEXT_FB", "DEPOSIT_THIS_FB", "DEPOSIT_NEXT_FB"
    } ;

    // mapped panel control registers are allocated in "connect()" when panel is known
    panel = nullptr ;
}


blinkenbone_panel_c::~blinkenbone_panel_c() {
    if (blinkenlight_api_client != nullptr)
        blinkenlight_api_client_destructor(blinkenlight_api_client) ;
}


// connect to server,
// query panel[panel_addr],
// build ordered list of controls from panel and database
void blinkenbone_panel_c::connect(std::string _hostname, unsigned _panel_addr) {
    assert(!connected()) ;
    hostname = _hostname ;
    panel_addr = _panel_addr ;
    control_register_sets.clear();
    panel = nullptr ;
    server_error = false ;

    blinkenlight_api_client = blinkenlight_api_client_constructor();
    INFO("Trying to connect to the BlinkenBone server %s via RPC ... (fix timeout of 60! seconds)") ;
    if (blinkenlight_api_client_connect(blinkenlight_api_client, hostname.c_str()) != 0) {
        ERROR("Connecting to BlinkenBone server %s failed.", hostname.c_str());
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
        server_error = true  ;
    }

    if (!server_error && blinkenlight_api_client_get_panels_and_controls(blinkenlight_api_client)) {
        ERROR("Querying panels and controls from BlinkenBone server %s failed.", hostname.c_str());
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
        server_error = true  ;
    }

    if (!server_error && blinkenlight_api_client->panel_list->panels_count == 0) {
        ERROR("BlinkenBone server %s has no panels?", hostname.c_str());
        server_error = true  ;
    }

    // valid panel addressed ?
    if (!server_error && panel_addr >= blinkenlight_api_client->panel_list->panels_count) {
        ERROR("Invalid panel address %d, BlinkenBone server %s has only %d panels",
              panel_addr, hostname.c_str(), blinkenlight_api_client->panel_list->panels_count);
        server_error = true  ;
    }
    if (!server_error) {
        panel = &(blinkenlight_api_client->panel_list->panels[panel_addr]) ;

        // generate ordered control list and register mapping
        build_controls() ;
    }
}


void blinkenbone_panel_c::disconnect() {
    assert(connected()) ;
    control_register_sets.clear() ;
    panel = nullptr ; // re-select on next connect()

    if (blinkenlight_api_client == nullptr)
        return ;
    if (!blinkenlight_api_client->connected)
        return ;
    blinkenlight_api_client_disconnect( blinkenlight_api_client) ;
}

// return mapped register addr of last bit slice of last switch input which has changed
// resets change state after reading
// to be called after input_panel_controls_to_registers()
// in contrast to output change check, control->value previous is not used here,
// but an own "pdp11_reg->value_previous" to track changes on register level
uint32_t blinkenbone_panel_c::get_input_changed_and_clear() {
    // for all input controls: value_previous != value?
    uint32_t result = 0 ;

    assert(controls_count() == 0 || panel ) ; // connection to server?
    for (control_register_set_c &crs: control_register_sets)
        // iterate all, check only inputs
        if (crs.is_input()) {
            // which of the mapped registers?
            for (unsigned idx_cvsr = 0 ; idx_cvsr < crs.register_count ; idx_cvsr++) {
                control_value_slice_register_c *cvsr = &(crs.control_value_slice_registers[idx_cvsr]) ;
                uint16_t value = device->get_register_dato_value(cvsr->pdp11_reg);
                if (value != cvsr->value_previous) {
                    result = cvsr->pdp11_reg->addr ;
                    cvsr->value_previous = value ; // clear "changed" condition
                }
            }
        }
    return result ;
}


// check wether PDP-11 registers for lamp controls have changed
// since last "update to server"
bool blinkenbone_panel_c::has_output_changed() {
    // for all output controls: value_previous != value?
    bool result = false ;

    for (control_register_set_c &crs: control_register_sets) {
        blinkenlight_control_t *c = crs.control;
        // iterate all, check only outputs
        if (!crs.is_input() && c->value != c->value_previous)
            // idx_control indexes outputs only
            result = true ;
    }
    return result ;
}

// force update on next worker() run,
// or clear pending changes
void blinkenbone_panel_c::set_output_changed(bool forced_update_state) {
    // for all output controls: set value_previous to "same" or "different" value
    for (control_register_set_c &crs: control_register_sets) {
        blinkenlight_control_t *c = crs.control;
        // iterate all, check only outputs
        if (!crs.is_input())
            c->value_previous = forced_update_state
                                ? ~c->value  // force update: previous != value.
                                : c->value ; // clear update: previous = value.
    }
}


// define the registers needed to hold all bits for value of control c
// result in *crs
// example: c->value_bitlen = 36
// => register_count = 2, reg[0] = 0..15, reg[1] = 16..31, reg[2] = 32..35,  reg[3] = don't care
void blinkenbone_panel_c::build_control_register_set(control_register_set_c *crs, blinkenlight_control_t *c)  {
    assert(c->value_bitlen > 0) ;
    crs->control = c ;
    // how many 16 bit registers needed for the value bits? 1..16 -> 1, 17..32 -> 2
    crs->register_count = ((c->value_bitlen-1) / 16) + 1 ;
    assert(crs->register_count < 4) ; // max 64 bits

    control_value_slice_register_c *cvsr ;
    cvsr = &crs->control_value_slice_registers[0] ;
    cvsr->bit_index_from = 0 ;
    if (c->value_bitlen <= 15)
        cvsr->bit_index_to = c->value_bitlen-1 ;
    else
        cvsr->bit_index_to = 15 ;

    if (crs->register_count > 1) {
        cvsr = &crs->control_value_slice_registers[1] ;
        cvsr->bit_index_from = 16 ;
        if (c->value_bitlen <= 31)
            cvsr->bit_index_to = c->value_bitlen-1 ;
        else
            cvsr->bit_index_to = 31 ;
    }

    if (crs->register_count > 2) {
        cvsr = &crs->control_value_slice_registers[2] ;
        cvsr->bit_index_from = 32 ;
        if (c->value_bitlen <= 47)
            cvsr->bit_index_to = c->value_bitlen-1 ;
        else
            cvsr->bit_index_to = 47 ;
    }

    if (crs->register_count > 3) {
        cvsr = &crs->control_value_slice_registers[3] ;
        cvsr->bit_index_from = 48 ;
        cvsr->bit_index_to = c->value_bitlen-1 ;
    }

    // now append registers to qunibus device
    for (unsigned idx_cvsr = 0 ; idx_cvsr <  crs->register_count ; idx_cvsr++) {
        cvsr = &crs->control_value_slice_registers[idx_cvsr] ;
        assert(device->register_count < MAX_IOPAGE_REGISTERS_PER_DEVICE) ;
        qunibusdevice_register_t *pdp11_reg = &(device->registers[device->register_count++]);

        // naming:
        // if more than one register is needed, bits 0..15 get suffix "_A", 16..31 get "_B", so on
        if (crs->register_count == 1)
            sprintf(pdp11_reg->name, "%s", c->name) ; // only one register: no suffixes
        else
            sprintf(pdp11_reg->name, "%s_%c", c->name, 'A'+idx_cvsr) ; // suffixes
        pdp11_reg->active_on_dati = false; // can be read fast without ARM code, no state change
        pdp11_reg->active_on_dato = false; // no notification, no IRQ
        cvsr->value_previous = pdp11_reg->reset_value = 0; // init clear changed
        if (c->is_input)
            pdp11_reg->writable_bits = 0; // input controls are read only
        else
            pdp11_reg->writable_bits = BitmaskFromLen16[cvsr->bit_len()] ;
        cvsr->pdp11_reg = pdp11_reg ;
    }

}


// Generate ordered control list and register mapping
// - connects to the remote blinkenlight API server,
//   may fail.
// - iterates the control name list in the database,
//	builds inputcontrols[] and outputcontrols[] from that
void blinkenbone_panel_c::build_controls() {

    // known name of server panel?
    std::vector<std::string> *control_names = nullptr ;
    try {
        control_names = &(db.at(panel->name)) ;
    } catch (const std::out_of_range& e) {
        ERROR("Blinkenlight API panel \"%s\" not known!", panel->name);
        server_error = true ;
        return ;
    }
    // now have list of control names, iterate
    control_register_sets.clear() ;

    for (const std::string &control_name : *control_names)	 {
        // find control, name case insensitive, and may be abbreviated
        // build-in name search request
        blinkenlight_control_t *c = blinkenlight_panels_get_control_by_name(blinkenlight_api_client->panel_list,
                                    panel, control_name.c_str(), /*is_input*/0);
        if (c == nullptr)
            c = blinkenlight_panels_get_control_by_name(blinkenlight_api_client->panel_list,
                    panel, control_name.c_str(), /*is_input*/1);
        if ( c == nullptr) {
            ERROR("Blinkenlight API panel \"%s\", control \"%s\" not published by server!", panel->name, control_name.c_str());
            continue ;
        }
        // add control to list, and add the PDP-11 registers to the blinkenbone device
        control_register_set_c crs ;
        build_control_register_set(&crs, c) ;
        control_register_sets.push_back(crs) ;
    }
// Map input controls to 1st block of PDP-11 registers
}


// diag output of all panels connected to server
void blinkenbone_panel_c::print_server_info() {
    if (blinkenlight_api_client == nullptr) {
        INFO("blinkenlight_api_client not instantiated.");
        return ;
    }
    if (!blinkenlight_api_client->connected) {
        INFO("blinkenlight_api_client not connected to any server.");
        return ;
    }

    char	buffer[1024] = "blinkenlight_api_client_get_serverinfo() error";
    blinkenlight_api_client_get_serverinfo(blinkenlight_api_client, buffer, sizeof(buffer));
    INFO("	%s.", buffer);

    // server list of panels, for each a list of controls
#ifdef SHOW_ALL_BLINKENBONE_SERVER_CONTROLS
    INFO("All Panels and their controls provided by server %s:\n", blinkenlight_api_client->rpc_server_hostname);
    for (unsigned i_panel = 0; i_panel < blinkenlight_api_client->panel_list->panels_count; i_panel++) {
        blinkenlight_panel_t *p = &(blinkenlight_api_client->panel_list->panels[i_panel]);
        INFO("Panel %d \"%s\" \n", i_panel, p->name);
        // iterate switches and lamp separated
        // global index is the PDP-11 "control address" register value
        unsigned i_inputcontrol = 0 ;
        for (unsigned i_control = 0; i_control < p->controls_count; i_control++) {
            blinkenlight_control_t *c = &(p->controls[i_control]);
            if (c->is_input) {
                INFO("    Control %d: Input \"%s\" \n", i_control, c->name);
                i_inputcontrol++ ;
            }
        }
        unsigned i_outputcontrol = 0 ;
        for (unsigned i_control = 0; i_control < p->controls_count; i_control++) {
            blinkenlight_control_t *c = &(p->controls[i_control]);
            if (!c->is_input) {
                INFO("    Control addr %d: Output \"%s\" \n", i_control, c->name);
                i_outputcontrol++ ;
            }
        }
    }
#endif

}


// print list of all PDP-11 registers with assigned controls
void blinkenbone_panel_c::print_register_info() {
    if (blinkenlight_api_client == nullptr) {
        ERROR("blinkenlight_api_client not instantiated.");
        return ;
    }
    if (!blinkenlight_api_client->connected) {
        ERROR("blinkenlight_api_client not connected to any server.");
        return ;
    }

    // mapping into PDP-11 address space
    // server list of panels, for each a list of controls
    INFO("Controls of panel %d \"%s\" on server \"%s\" mapped into PDP-11 address space:",
         panel_addr, panel->name, blinkenlight_api_client->rpc_server_hostname);
    INFO("  Addr    In/out  Reg name                  Bits     Panel control idx") ;
    INFO("  ----    ------  --------                  ----     -----------------") ;

    for (control_register_set_c &crs: control_register_sets) {
        for (unsigned idx_cvsr = 0 ; idx_cvsr < crs.register_count ; idx_cvsr++) {
            control_value_slice_register_c *cvsr = &(crs.control_value_slice_registers[idx_cvsr]) ;
            INFO("  %06o  %-6s   %-24s  %-7s  %d", cvsr->pdp11_reg->addr,
                 crs.is_input() ? "input":"output",
                 cvsr->pdp11_reg->name, cvsr->bits_text(), crs.control->index);
        }
    }
}


void blinkenbone_panel_c::set_testmode(uint16_t _testmode) {
    assert(panel) ; // connection to server?
    // write
    blinkenlight_api_client_set_object_param(blinkenlight_api_client,
            RPC_PARAM_CLASS_PANEL, panel->index, RPC_PARAM_HANDLE_PANEL_MODE, _testmode);
    testmode = _testmode ;
}



// write current values of blinkenlight_api input controls to
// PDP-11 registers
void blinkenbone_panel_c::input_panel_controls_to_registers() {
    assert(panel) ; // connection to server?
    for (control_register_set_c &crs: control_register_sets)
        // iterate all, check only inputs
        if (crs.is_input()) {
            blinkenlight_control_t *c = crs.control;
            // set all register slices values for this control
            for (unsigned idx_cvsr = 0 ; idx_cvsr < crs.register_count ; idx_cvsr++) {
                // extract the bits for this register from the control value
                control_value_slice_register_c *cvsr = &(crs.control_value_slice_registers[idx_cvsr]) ;
                uint16_t pdp11_reg_val = (c->value >> cvsr->bit_index_from) & BitmaskFromLen16[cvsr->bit_len()] ;
                device->set_register_dati_value(cvsr->pdp11_reg, pdp11_reg_val, __func__) ;
            }
        }
}

// write current values of PDP-11 registers to blinkenlight_api output controls
void blinkenbone_panel_c::registers_to_panel_output_controls() {
    assert(panel) ; // connection to server?
    for (control_register_set_c &crs: control_register_sets)
        // iterate all, check only outputs
        if (!crs.is_input()) {
            blinkenlight_control_t *c = crs.control;
            // mount control value from all register slices for this control
            c->value = 0 ;
            for (unsigned idx_cvsr = 0 ; idx_cvsr < crs.register_count ; idx_cvsr++) {
                // extract the bits for this register from the control value
                control_value_slice_register_c *cvsr = &(crs.control_value_slice_registers[idx_cvsr]) ;
                uint16_t pdp11_reg_val = device->get_register_dato_value(cvsr->pdp11_reg) ;
                c->value |= (uint64_t)pdp11_reg_val << cvsr->bit_index_from ;
            }
        }
}


// query server
void blinkenbone_panel_c::get_inputcontrols_values() {
    assert(panel) ;
// if active control is an input control, update the PDP-11 registers with its value
    blinkenlight_api_status_t status = blinkenlight_api_client_get_inputcontrols_values(
                                           blinkenlight_api_client, panel) ;
    if (status != 0) {
        server_error = true ;
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
    }
}


// update server
void blinkenbone_panel_c::set_outputcontrols_values() {
    assert(panel) ; // connection to server?
    blinkenlight_api_status_t status = blinkenlight_api_client_set_outputcontrols_values(blinkenlight_api_client, panel) ;
    if (status != 0) {
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
        exit(1); // error
    }
    set_output_changed(false) ; // clear "changed" condition
}



