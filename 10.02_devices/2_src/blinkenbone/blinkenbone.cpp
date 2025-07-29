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

 blinkenbone:
 A device to access remote BlinkenBone panels via PDP-11 address space
-  All control value are accessed as 32 bit (because of some 22 bit address/switches
    (so a PDP-11 will never drive the PDP-10 36bit panel)
-  Memory map mimics display of "blinkenlight-test" program:
  first block of PDP-11 registers accesses inputs (Switches)
  second register block accesses  outputs (LED).
- each control (LED bank, switch, roaw) has a value of n bits
   multiple PDP-11 registers are assign per control : 1 for n <0 16, 2 for n <=32, ...
   this is called a "control slice register set" for the control.
   registers are named by the control name
   if more than one register is needed, bits 0..15 get suffix "_A", 16..31 get "_B", so on
-  PDP-11 writes individual bit slices of a value (= registers in a register set)
	NON-ATOMIC,	when panel is updated in parallel, output control bit0..15 and 16..31 may
	be out of sync for one update period. Causes a 1milliscond visible glitch.
- input registers are read only
- only a single panel can be accessed.
- device parameters "panel_host" and "panel_add" select statically the panel,
	must do before "enable".
    PDP-11 can not select different host or panel.



 *** Controls of panel "11/70", screen #0 ***
* Inputs:
POWER       ( 1 bit SWITCH )=0o           ||  PANEL_LOCK  ( 1 bit SWITCH )=0o           ||  SR          (22 bit SWITCH )=00000000o
LOAD_ADRS   ( 1 bit SWITCH )=0o           ||  EXAM        ( 1 bit SWITCH )=0o           ||  DEPOSIT     ( 1 bit SWITCH )=0o
CONT        ( 1 bit SWITCH )=0o           ||  HALT        ( 1 bit SWITCH )=0o           ||  S_BUS_CYCLE ( 1 bit SWITCH )=0o
START       ( 1 bit SWITCH )=0o           ||  ADDR_SELECT ( 3 bit KNOB   )=7o !         ||  DATA_SELECT ( 2 bit KNOB   )=2o !
PANEL_LOCK  ( 1 bit KNOB   )=0o
* Outputs:
 0) ADDRESS       (22 bit LAMP   )=00000000o  ||   1) DATA          (16 bit LAMP   )=000000o
 2) PARITY_HIGH   ( 1 bit LAMP   )=0o         ||   3) PARITY_LOW    ( 1 bit LAMP   )=0o
 4) PAR_ERR       ( 1 bit LAMP   )=0o         ||   5) ADRS_ERR      ( 1 bit LAMP   )=0o
 6) RUN           ( 1 bit LAMP   )=0o         ||   7) PAUSE         ( 1 bit LAMP   )=0o
 8) MASTER        ( 1 bit LAMP   )=0o         ||   9) MMR0_MODE     ( 2 bit LAMP   )=0o
10) DATA_SPACE    ( 1 bit LAMP   )=0o         ||  11) ADDRESSING_16 ( 1 bit LAMP   )=0o
12) ADDRESSING_18 ( 1 bit LAMP   )=0o         ||  13) ADDRESSING_22 ( 1 bit LAMP   )=0o

 No active register callbacks, just polling in worker()
 */

#include <string.h>
#include <assert.h>

#include "logger.hpp"
#include "timeout.hpp"
#include "bitcalc.h"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"	// definition of class device_c
#include "blinkenbone.hpp"

blinkenbone_c::blinkenbone_c() :
    qunibusdevice_c()  // super class constructor
{
    // static config
    name.value = "BLINKENBONE";
    type_name.value = "blinkenbone_c";
    log_label = "bb";

    set_default_bus_params(0760200, 31, 0, 0); // base addr, slot, intr-vector, intr level

    // init parameters
    panel_host.value = "bigfoot" ;
    panel_addr.value = 0 ;
    poll_period_ms.value = 50 ; // poll 20 Hz
    update_period_ms.value = 10 ; // default slow update

    update_prescaler_ms = poll_prescaler_ms = 0;

    // panel control registers are allocated in "install()" when panel is known
    register_count = fix_register_count = 1;
    config_reg = &(registers[0]); // @  base addr
    strcpy(config_reg->name, "PANEL_CONFIG");
    config_reg->active_on_dati = false; // no controller state change
    config_reg->active_on_dato = false;
    config_reg->reset_value = 0;
    config_reg->writable_bits = 0xc000; // only test mode writable
    config_value_previous = 0 ;


    input_controls_count = 0 ;
    output_controls_count = 0 ;
}

blinkenbone_c::~blinkenbone_c()
{
    if (blinkenlight_api_client == nullptr)
        return ;
    blinkenlight_api_client_destructor(blinkenlight_api_client) ;
}


// write current values of blinkenlight_api input controls to
// PDP-11 registers
void blinkenbone_c::input_controls_to_registers() {
    // find the "i_inputcontrol"th input control
    unsigned idx_icrs = 0 ; // iterates input_control_register_sets[]
    for (unsigned idx_control = 0; idx_control < panel->controls_count; idx_control++) {
        blinkenlight_control_t *c = &(panel->controls[idx_control]);
        if (c->is_input) { // idx_icrs indexes inputs only
            assert(idx_icrs < max_input_controls_count) ;
            control_register_set_c *icrs = &input_control_register_sets[idx_icrs] ;
            assert(icrs) ;

            // set all register slices values for this control
            for (unsigned idx_cvsr = 0 ; idx_cvsr < icrs->register_count ; idx_cvsr++) {
                // extract the bits for this register from the control value
                control_value_slice_register_c *cvsr = &(icrs->control_value_slice_registers[idx_cvsr]) ;
                uint16_t pdp11_reg_val = (c->value >> cvsr->bit_index_from) & BitmaskFromLen16[cvsr->bit_len()] ;
                set_register_dati_value(cvsr->pdp11_reg, pdp11_reg_val, __func__) ;
            }
            idx_icrs++ ;
        }
    }
}

// write current values of PDP-11 registers to blinkenlight_api output controls
void blinkenbone_c::registers_to_output_controls() {
    // find again the "i_outcontrol"th output control
    unsigned idx_ocrs = 0 ; //  // iterates input_control_register_sets[]
    for (unsigned idx_control = 0; idx_control < panel->controls_count; idx_control++) {
        blinkenlight_control_t *c = &(panel->controls[idx_control]);
        if (!c->is_input) { // idx_ocrs indexes outputs only
            assert(idx_ocrs < max_output_controls_count) ;
            control_register_set_c *ocrs = &output_control_register_sets[idx_ocrs] ;
            assert(ocrs) ;

            // mount control value from all register slices for this control
            c->value = 0 ;
            for (unsigned idx_cvsr = 0 ; idx_cvsr < ocrs->register_count ; idx_cvsr++) {
                // extract the bits for this register from the control value
                control_value_slice_register_c *cvsr = &(ocrs->control_value_slice_registers[idx_cvsr]) ;
                uint16_t pdp11_reg_val = get_register_dato_value(cvsr->pdp11_reg) ;
                c->value |= (uint64_t)pdp11_reg_val << cvsr->bit_index_from ;
            }
            idx_ocrs++ ;
        }
    }
}

// check wether PDP-11 registers for lamp controls have changed
// since last "update to server"
bool blinkenbone_c::outputs_need_update() {
// for all output controls: value_previous != value?
    bool result = false ;
    for (unsigned idx_control = 0; idx_control < panel->controls_count; idx_control++) {
        blinkenlight_control_t *c = &(panel->controls[idx_control]);
        if (!c->is_input && c->value != c->value_previous)
            // idx_control indexes outputs only
            result = true ;
    }
    return result ;
}

// force update on next worker() run,
// or clear pending changes
void blinkenbone_c::set_output_update_need(bool forced_update_state) {
    // for all output controls: set value_previous to "same" or "different" value
    for (unsigned idx_control = 0; idx_control < panel->controls_count; idx_control++) {
        blinkenlight_control_t *c = &(panel->controls[idx_control]);
        if (!c->is_input) {
            // idx_control indexes outputs only
            c->value_previous = forced_update_state
                                ? ~c->value  // force update: previous != value.
                                : c->value ; // clear update: previous = value.
        }
    }
}


bool blinkenbone_c::on_param_changed(parameter_c *param)
{
    // no own parameter or "enable" logic
    return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}


// called on "enable"
// connects to the remote blinkenlight API server,
// may fail.
// define the registers needed to hold all bits for value of control c
// result in *crs
// example: c->value_bitlen = 36
// => register_count = 2, reg[0] = 0..15, reg[1] = 16..31, reg[2] = 32..35,  reg[3] = don't care
void blinkenbone_c::build_control_register_set(control_register_set_c *crs, blinkenlight_control_t *c)  {
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
    for (unsigned idx_cvsr = 0 ; idx_cvsr <  crs->register_count  ; idx_cvsr++) {
        cvsr = &crs->control_value_slice_registers[idx_cvsr] ;
        assert(register_count < MAX_IOPAGE_REGISTERS_PER_DEVICE) ;
        qunibusdevice_register_t *pdp11_reg = &(registers[register_count++]);

        // naming:
        // if more than one register is needed, bits 0..15 get suffix "_A", 16..31 get "_B", so on
        if (crs->register_count == 1)
            sprintf(pdp11_reg->name, "%s", c->name) ; // only one register: no suffixes
        else
            sprintf(pdp11_reg->name, "%s_%c", c->name, 'A'+idx_cvsr) ; // suffixes
        pdp11_reg->active_on_dati = false; // can be read fast without ARM code, no state change
        pdp11_reg->active_on_dato = false; // no notification, no IRQ
        pdp11_reg->reset_value = 0;
        if (c->is_input)
            pdp11_reg->writable_bits = 0; // input controls are read only
        else {
            pdp11_reg->writable_bits = BitmaskFromLen16[cvsr->bit_len()] ;
        }
        cvsr->pdp11_reg = pdp11_reg ;
    }

}


bool blinkenbone_c::on_before_install(void)
{
    panel = nullptr ;
    blinkenlight_api_client = blinkenlight_api_client_constructor();
    char *panel_hostname = (char *)panel_host.render()->c_str() ;
    if (blinkenlight_api_client_connect(blinkenlight_api_client, panel_hostname) != 0) {
        ERROR("Connecting to BlinkenBone server %s failed.", panel_hostname);
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
        return false; // error
    }

    if (blinkenlight_api_client_get_panels_and_controls(blinkenlight_api_client)) {
        ERROR("Querying panels and controls from BlinkenBone server %s failed.", panel_hostname);
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
        return false; // error
    }

    if (blinkenlight_api_client->panel_list->panels_count == 0) {
        ERROR("BlinkenBone server %s has no panels?", panel_hostname);
        return false ;
    }
    // valid panel addressed ?
    if (panel_addr.value >= blinkenlight_api_client->panel_list->panels_count) {
        ERROR("Invalid panel address %d, BlinkenBone server %s has only %d panels",
              panel_addr.value, panel_hostname, blinkenlight_api_client->panel_list->panels_count);
        return false ;
    }
    // now lock params against change
    panel_host.readonly = true ;
    panel_addr.readonly = true ;

    // Setup Config Register
    // get ID from parameter, set by script
    config_reg->reset_value = panel_id.value & 0x3fff ; // and "normal" test mode
    config_value_previous = ~config_reg->reset_value ; // force change

    assert(panel_addr.value < blinkenlight_api_client->panel_list->panels_count) ;
    panel = &(blinkenlight_api_client->panel_list->panels[panel_addr.value]) ;

    // Map input controls to 1st block of PDP-11 registers
    input_controls_count = 0 ;
    for (unsigned idx_control = 0; idx_control < panel->controls_count; idx_control++) {
        blinkenlight_control_t *c = &(panel->controls[idx_control]);
        if (c->is_input) {
            control_register_set_c *icrs = &(input_control_register_sets[input_controls_count++]) ;
            build_control_register_set(icrs, c) ;
        }
    }

    // Map output controls to 2nd block of PDP-11 registers
    output_controls_count = 0 ;
    for (unsigned idx_control = 0; idx_control < panel->controls_count; idx_control++) {
        blinkenlight_control_t *c = &(panel->controls[idx_control]);
        if (!c->is_input) {
            control_register_set_c *ocrs = &(output_control_register_sets[output_controls_count++]) ;
            build_control_register_set(ocrs, c) ;
        }
    }

    INFO("Connected to BlinkenBone server %s:", panel_hostname);

    return true ;
}


// now the PDP-11 registers are assigned to PDP-11 adresses
void blinkenbone_c::on_after_install(void) {
    print_register_info();

    // force server update on next worker()
    set_output_update_need(true) ;
}


// disconnect panel, rmeove mapping regsiters for controls
// static regsiters remain
void blinkenbone_c::on_after_uninstall(void)
{
    panel_host.readonly = false ;
    panel_addr.readonly = false ;
    panel = nullptr ; // re-select on next install()
    register_count = fix_register_count ; // panel_config register remains
    input_controls_count = 0 ;
    output_controls_count = 0 ;

    if (blinkenlight_api_client == nullptr)
        return ;
    if (!blinkenlight_api_client->connected)
        return ;
    blinkenlight_api_client_disconnect(	blinkenlight_api_client) ;
}


// diag output of all panels connected to server
void blinkenbone_c::print_server_info() {
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
void blinkenbone_c::print_register_info() {
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

    // mapping into PDP-11 address space
    // server list of panels, for each a list of controls
    INFO("Controls of panel %d on server \"%s\" mapped into PDP-11 address space:\n", panel_addr.value,
         blinkenlight_api_client->rpc_server_hostname);
    INFO("  Addr    In/out  Reg name                  Bits     Panel control idx\n") ;


    INFO("  %06o  config  %-24s  %-7s  %s", config_reg->addr, config_reg->name, "<15:14>",
         "selftest: 0=normal,1=lamp test,2=full test,3=powerless");
    INFO("  %06o  config  %-24s  %-7s  %s", config_reg->addr, config_reg->name, "<13:00>", "access to parameter panel_id");


    for (unsigned idx_inputcontrol = 0 ; idx_inputcontrol < input_controls_count ; idx_inputcontrol++) {
        control_register_set_c *crs = &(input_control_register_sets[idx_inputcontrol]) ;
        assert(crs->control->is_input) ;
        for (unsigned idx_cvsr = 0 ; idx_cvsr < crs->register_count ; idx_cvsr++) {
            control_value_slice_register_c *cvsr = &(crs->control_value_slice_registers[idx_cvsr]) ;
            INFO("  %06o  input   %-24s  %-7s  %d", cvsr->pdp11_reg->addr, cvsr->pdp11_reg->name, cvsr->bits_text(), crs->control->index);
        }
    }
    for (unsigned idx_outputcontrol = 0 ; idx_outputcontrol < output_controls_count ; idx_outputcontrol++) {
        control_register_set_c *crs = &(output_control_register_sets[idx_outputcontrol]) ;
        assert(!crs->control->is_input) ;
        for (unsigned idx_cvsr = 0 ; idx_cvsr < crs->register_count ; idx_cvsr++) {
            control_value_slice_register_c *cvsr = &(crs->control_value_slice_registers[idx_cvsr]) ;
            INFO("  %06o  output  %-24s  %-7s  %d", cvsr->pdp11_reg->addr, cvsr->pdp11_reg->name, cvsr->bits_text(), crs->control->index);
        }
    }
}


void blinkenbone_c::worker_config_changed() {
    // change in one of the status bits <15:14>
    uint16_t cur_value = get_register_dato_value(config_reg) ;
    uint16_t changed_bits =  (cur_value ^ config_value_previous) & 0xc000 ;
    config_value_previous = cur_value ; // changes processed

    if ( changed_bits == 0)
        return ;

    // bits <15:14>
    unsigned testmode = (cur_value >> 14) & 3 ;

    // write
    blinkenlight_api_client_set_object_param(blinkenlight_api_client,
            RPC_PARAM_CLASS_PANEL, panel->index, RPC_PARAM_HANDLE_PANEL_MODE, testmode);
}


// period polling of panel input
void blinkenbone_c::worker_poll_inputs() {
    if (poll_period_ms.value == 0)
        return ; // period == 0: disable

    if (poll_prescaler_ms > 0) {
        poll_prescaler_ms-- ;
        return ;
    }

    poll_prescaler_ms = poll_period_ms.value-1 ; // reload

    if (panel == nullptr || input_controls_count == 0)
        return ; // nothing to do

    // if active control is an input control, update the PDP-11 registers with its value
    blinkenlight_api_status_t status = blinkenlight_api_client_get_inputcontrols_values(
                                           blinkenlight_api_client, panel) ;
    if (status != 0) {
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
        exit(1); // error
    }
    input_controls_to_registers() ;
}


// update lamps
void blinkenbone_c::worker_update_outputs() {
    if (update_period_ms.value == 0)
        return ; // period == 0: disable

    if (update_prescaler_ms > 0) {
        update_prescaler_ms-- ;
        return ;
    }
    update_prescaler_ms = update_period_ms.value-1 ; // reload

    if (panel == nullptr || output_controls_count == 0)
        return ; // nothing to do

    // I think TCP/IP with 1 kHz is less a system load than
    // high frequency interrupts needed to register DATO changes
    // via on_after_register_access()
    registers_to_output_controls() ;

    if (!outputs_need_update())
        return ;

    blinkenlight_api_status_t status = blinkenlight_api_client_set_outputcontrols_values(blinkenlight_api_client, panel) ;
    if (status != 0) {
        ERROR(blinkenlight_api_client_get_error_text(blinkenlight_api_client));
        exit(1); // error
    }

    set_output_update_need(false) ; // clear "changed" condition
}


// background worker.
// poll input controls  (= panel switches) direct to register flipflops
void blinkenbone_c::worker(unsigned instance)
{
    UNUSED(instance); // only one
    timeout_c timeout;
    while (!workers_terminate) {
        timeout.wait_ms(1);

        worker_config_changed() ;
        worker_poll_inputs() ;
        worker_update_outputs() ;
    }
}

// process DATI/DATO access to one of my "active" registers
// !! called asynchronuously by PRU, with SSYN asserted and blocking QBUS/UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void blinkenbone_c::on_after_register_access(qunibusdevice_register_t *device_reg,
        uint8_t unibus_control, DATO_ACCESS access)
{
    // nothing todo
    // DATI/DATO to PDP-11 registers does not initiate any action,
    // polling and sync with blinkenbone server in worker()
    UNUSED(device_reg);
    UNUSED(unibus_control);
    UNUSED(access);
}

// after QBUS/UNIBUS install, device is reset by DCLO cycle
void blinkenbone_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge)
{
    UNUSED(aclo_edge) ;
    UNUSED(dclo_edge) ;
}

// QBUS/UNIBUS INIT: clear all registers
void blinkenbone_c::on_init_changed(void)
{
    // write all registers to "reset-values"
    if (init_asserted) {
        reset_unibus_registers();
        INFO("blinkenbone_c::on_init()");
    }
}

