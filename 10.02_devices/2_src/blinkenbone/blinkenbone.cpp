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
-  Memory map given by explicti control list in blinkenbone_panel_c
  Should mimic display of "blinkenlight-test" program:
  first block of PDP-11 registers accesses inputs (Switches)
  second register block accesses outputs (LED).
- each control (LED bank, switch row) has a value of n bits
   multiple PDP-11 registers are assign per control : 1 for n <= 16, 2 for n <=32, ...
   this is called a "control slice register set" for the control.
   registers are named by the control name
   if more than one register is needed, bits 0..15 get suffix "_A", 16..31 get "_B", so on
-  PDP-11 writes individual bit slices of a value (= registers in a register set)
	NON-ATOMIC,	when panel is updated in parallel, output control bit0..15 and 16..31 may
	be out of sync for one update period. Causes a 1milliscond visible glitch.
- input registers are read only
- only a single panel can be accessed.
- device parameters "panel_host" and "panel_addr" select statically the panel,
	must do before "enable".
    PDP-11 can not select different host or panel.

If connection to server not established: set error bit set in input_cs, output_cs
still programmable outpdate period and interrupt


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
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"	// definition of class device_c
#include "blinkenbone.hpp"

blinkenbone_c::blinkenbone_c() :
    qunibusdevice_c()  // super class constructor
{

    panel =	new blinkenbone_panel_c(this) ;

    // static config
    name.value = "BLINKENBONE";
    type_name.value = "blinkenbone_c";
    log_label = "bb";

    // Default input INT vector  = 310 (free space starting at 300)
    // output INT vector = input + 4 = 314
    // BR level = 6 (because periodic output interrupt ... handle it like the KW11 clock
    set_default_bus_params(0760200, 30, 0310, 6); // base addr, slot,(and slot+1), intr-vector, intr level

    // init parameters
    panel_host.value = "bigfoot" ;
    panel_addr.value = 0 ;
    poll_period_ms.value = 50 ; // poll 20 Hz
    update_period_ms.value = 10 ; // default slow update

    update_prescaler_ms = poll_prescaler_ms = 0;



    reg_input_cs = &(registers[0]); // @  base addr
    strcpy(reg_input_cs->name, "PANEL_ICS");
    reg_input_cs->active_on_dati = true; //  controller state change
    reg_input_cs->active_on_dato = true;
    reg_input_cs->reset_value = 0;
    reg_input_cs->writable_bits = BIT(6) ; // bit 6 IE

    reg_output_cs = &(registers[1]); // @  base addr+2
    strcpy(reg_output_cs->name, "PANEL_OCS");
    reg_output_cs->active_on_dati = true; //  controller state change
    reg_output_cs->active_on_dato = true;
    reg_output_cs->reset_value = 0;
    reg_output_cs->writable_bits = BIT(6) | 0x3; // bit 6 IE, testmode

    reg_input_period = &(registers[2]); // @  base addr+4
    strcpy(reg_input_period->name, "PANEL_IPERIOD");
    reg_input_period->active_on_dati = false; // passive
    reg_input_period->active_on_dato = false;
    reg_input_period->reset_value = 0; // set on enable
    reg_input_period->writable_bits = 0xffff; // r/w

    reg_output_period = &(registers[3]); // @  base addr+6
    strcpy(reg_output_period->name, "PANEL_OPERIOD");
    reg_output_period->active_on_dati = false; // passive
    reg_output_period->active_on_dato = false;
    reg_output_period->reset_value = 0; // set on enable
    reg_output_period->writable_bits = 0xffff; // r/w

    // if input switch change detected (and perhaps INTR),
    // store mapped register addr here
    reg_input_change_addr= &(registers[4]); // @  base addr+o10
    strcpy(reg_input_change_addr->name, "PANEL_ICHGREG");
    reg_input_change_addr->active_on_dati = false; // no controller state change
    reg_input_change_addr->active_on_dato = false;
    reg_input_change_addr->reset_value = 0;
    reg_input_change_addr->writable_bits = 0x0000; // read only

    reg_config = &(registers[5]); // @  base addr+o10
    strcpy(reg_config->name, "PANEL_CONFIG");
    reg_config->active_on_dati = false; // no controller state change
    reg_config->active_on_dato = false;
    reg_config->reset_value = 0;
    reg_config->writable_bits = 0x0000; // ROM

    register_count = fix_register_count = 6;

}

blinkenbone_c::~blinkenbone_c()
{
    delete panel ;
}




// print list of all fixed registers.
// same layout as blineknbone_panel_c::print_register_info()
void blinkenbone_c::print_register_info() {
    const char *regFmt = "  %06o     %-16s  %s" ; // addr, name, info
    const char *regBitFmt = "    %7s  %-16s  %s" ; // bitrange, mnemonic info
    // mapping into PDP-11 address space
    // server list of panels, for each a list of controls
    INFO("Fixed BlinkenBone device registers in PDP-11 address space:\n");
    INFO("  %9s  %-16s  %s\n", "Addr/Bits", "Reg name", "Info");
    INFO("  %9s  %-16s  %s\n", "---------", "--------", "----");

    INFO(regFmt, reg_input_cs->addr, reg_input_cs->name,
         "Command and Status Register for panel Inputs") ;
    INFO(regBitFmt, "<15>", "ERR", "Panel not connected, server error");
    INFO(regBitFmt, "<7>", "IEVNT", "Change Event on some input switches, may trigger INT");
    INFO(regBitFmt, "<6>", "IIE", "Interrupt Enable for Input");

    INFO(regFmt, reg_output_cs->addr, reg_output_cs->name,
         "Command and Status Register for panel Outputs") ;
    INFO(regBitFmt, "<15>", "ERR", "Panel not connected, server error");
    INFO(regBitFmt, "<7>", "OEVNT", "Periodic panel Output (lamps) update occured, may trigger INT");
    INFO(regBitFmt, "<6>", "OIE", "Interrupt Enable for Output");
    INFO(regBitFmt, "<1:0>", "OTSTMODE",
         "panel test mode: 0=normal,1=lamp test,2=full test,3=powerless");

    INFO(regFmt, reg_input_period->addr, reg_input_period->name,
         "Interval for periodic panel input polling") ;
    INFO(regBitFmt, "<9:0>", "IPERIOD", "1..1000 millisecs, 0=off, inits to parameter \"poll_period_ms\"");

    INFO(regFmt, reg_output_period->addr, reg_output_period->name,
         "Interval for periodic panel output update") ;
    INFO(regBitFmt, "<9:0>", "OPERIOD", "1..1000 millisecs, 0=off, inits to parameter \"update_period_ms\"");

    INFO(regFmt, reg_input_change_addr->addr, reg_input_change_addr->name,
         "Addr of last changed mapped input switch register") ;

    INFO(regFmt, reg_config->addr, reg_config->name,
         "User defined bitpattern to tell PDP-11 panel config");
}

// calc static INTR condition level.
// Change of that condition calculated by intr_request_c.is_condition_raised()
bool blinkenbone_c::get_input_intr_level(void)
{
    return state_input_event && state_input_interrupt_enable;
}

// Update RCSR and optionally generate INTR
void blinkenbone_c::set_input_csr_dati_value_and_INTR(void)
{
    uint16_t val =
        (!panel->connected() ? BIT(15) : 0)
        | (state_input_event ? BIT(7) : 0)
        | (state_input_interrupt_enable ? BIT(6) : 0) ;
    switch (intr_request_input_change.edge_detect(get_input_intr_level())) {
    case intr_request_c::INTERRUPT_EDGE_RAISING:
        // set register atomically with INTR, if INTR not blocked
        qunibusadapter->INTR(intr_request_input_change, reg_input_cs, val);
        // if(val) DEBUG_FAST("set_input_csr_dati_value_and_INTR() @A val=o%o", val);
        break;
    case intr_request_c::INTERRUPT_EDGE_FALLING:
        // raised INTRs may get canceled if DATI
        qunibusadapter->cancel_INTR(intr_request_input_change);
        // if(val) DEBUG_FAST("set_input_csr_dati_value_and_INTR() @B val=o%o", val);
        set_register_dati_value(reg_input_cs, val, __func__);
        break;
    default:
        // if(val) DEBUG_FAST("set_input_csr_dati_value_and_INTR() @C val=o%o", val);
        set_register_dati_value(reg_input_cs, val, __func__);
    }
}


// calc static INTR condition level.
// Change of that condition calculated by intr_request_c.is_condition_raised()
bool blinkenbone_c::get_output_intr_level(void)
{
    return state_output_event && state_output_interrupt_enable;
}

// Update RCSR and optionally generate INTR
void blinkenbone_c::set_output_csr_dati_value_and_INTR(void)
{
    uint16_t val =
        (!panel->connected() ? BIT(15) : 0)
        | (state_output_event ? BIT(7) : 0)
        | (state_output_interrupt_enable ? BIT(6) : 0)
        | (state_testmode & 3);

    switch (intr_request_output_period.edge_detect(get_output_intr_level())) {
    case intr_request_c::INTERRUPT_EDGE_RAISING:
        // set register atomically with INTR, if INTR not blocked
        qunibusadapter->INTR(intr_request_output_period, reg_output_cs, val);
        break;
    case intr_request_c::INTERRUPT_EDGE_FALLING:
        // raised INTRs may get canceled if DATI
        qunibusadapter->cancel_INTR(intr_request_output_period);
        set_register_dati_value(reg_output_cs, val, __func__);
        break;
    default:
        set_register_dati_value(reg_output_cs, val, __func__);
    }
}


bool blinkenbone_c::on_param_changed(parameter_c *param)
{
    if (param == &priority_slot) {
        intr_request_input_change.set_priority_slot(priority_slot.new_value);
        // XMT INTR: lower priority => nxt slot, and next vector
        intr_request_output_period.set_priority_slot(priority_slot.new_value + 1);
    } else if (param == &intr_vector) {
        intr_request_input_change.set_vector(intr_vector.new_value);
        intr_request_output_period.set_vector(intr_vector.new_value + 4);
    } else if (param == &intr_level) {
        intr_request_input_change.set_level(intr_level.new_value);
        intr_request_output_period.set_level(intr_level.new_value);
    }
    return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}



// always install device
// in case of connection errors, see 'panel->connected()' and proceed
bool blinkenbone_c::on_before_install(void)
{
    // now lock params against change
    panel_host.readonly = true ;
    panel_addr.readonly = true ;

    // setup register content
    reg_input_period->reset_value = poll_period_ms.value ;
//    set_register_dati_value(reg_input_period, poll_period_ms.value, __func__) ;
    reg_output_period->reset_value = update_period_ms.value ;
//    set_register_dati_value(reg_output_period, update_period_ms.value, __func__) ;
    reg_config->reset_value = panel_config.value ;
//    set_register_dati_value(reg_config, panel_config.value, __func__) ;

    // force testmode update on worker start
    panel->testmode = ~state_testmode ;

    panel->connect(/*hostname*/*panel_host.render(), panel_addr.value) ;

    if (!panel->connected()) {
        INFO("NO Connection to BlinkenBone server!");
    } else {
        INFO("Connected to BlinkenBone server %s:", panel->hostname.c_str());
    }

    // install device on every case, even if no connection to panel server
    return true ;
}


// now the PDP-11 registers are assigned to PDP-11 adresses
void blinkenbone_c::on_after_install(void) {

    panel->print_server_info() ;
    print_register_info() ;
    panel->print_register_info();

    // reset "change" conditions
    panel->get_input_changed_and_clear() ;
    panel->set_output_changed(true) ;
}


// disconnect panel, remove mapped control registers
// static registers remain
void blinkenbone_c::on_after_uninstall(void)
{
    panel->disconnect() ;
    panel_host.readonly = false ;
    panel_addr.readonly = false ;
    register_count = fix_register_count ; // panel_config register remains
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
    UNUSED(access);
    // nothing todo
    // DATI/DATO to PDP-11 registers does not initiate any action,
    // polling and sync with blinkenbone server in worker()
    switch (device_reg->index) {
    case 0: { // ICS
        pthread_mutex_lock(&on_after_input_register_access_mutex) ;
        if (unibus_control == QUNIBUS_CYCLE_DATO) {
            uint16_t csr_value = get_register_dato_value(device_reg) ;
            state_input_interrupt_enable = !!(csr_value & BIT(6)) ;
        }
        if (unibus_control == QUNIBUS_CYCLE_DATI) {
            // DATI read state_input_event, now clear
            state_input_event = false ;
            set_input_csr_dati_value_and_INTR();
        }
        pthread_mutex_unlock(&on_after_input_register_access_mutex) ;
    }
    case 1: { // OCS
        pthread_mutex_lock(&on_after_output_register_access_mutex);
        if (unibus_control == QUNIBUS_CYCLE_DATO) {
            uint16_t csr_value = get_register_dato_value(device_reg) ;
            state_output_interrupt_enable = !!(csr_value & BIT(6)) ;
            state_testmode = csr_value & 3 ;
        }
        if (unibus_control == QUNIBUS_CYCLE_DATI) {
            // DATI clears state_output_event
            state_output_event = false ;
            set_output_csr_dati_value_and_INTR();
        }
        pthread_mutex_unlock(&on_after_output_register_access_mutex) ;
    }
    break ;
    }
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


// update testmode
void blinkenbone_c::worker_testmode_changed() {
    if (!panel->connected())
        return ; // no panel
    if (panel->testmode == state_testmode)
        return ; // already in sync

    panel->set_testmode(state_testmode) ;
}


// period polling of panel input
void blinkenbone_c::worker_input_poll() {
    // prescaler counts upwards to adapt to dynamic changed reg_input_period
    uint16_t effective_period = get_register_dato_value(reg_input_period) ;
    // map to 1..1000milli seconds, 0=off
    effective_period = (uint16_t)rangeToMinMax(effective_period, 0, 1000) ;

    if (effective_period == 0)
        return ; // period == 0: disable

    if (poll_prescaler_ms < effective_period) {
        poll_prescaler_ms++ ;
        return ;
    }

    poll_prescaler_ms = 0 ; // reload

    if (!panel->connected())
        return ;

    panel->get_inputcontrols_values() ;
    panel->input_panel_controls_to_registers() ;

    // which input register changed, if any? 0 =  none
    uint16_t input_changed_addr = panel->get_input_changed_and_clear() ;
    if (input_changed_addr > 0) {
        // INTR, and store mapped register addr of changed control bits
        // lock against parallel CSR DATI/DATO
        pthread_mutex_lock(&on_after_input_register_access_mutex) ;
        // DEBUG_FAST("INPUT mapped reg o%o changed", input_changed_addr);
        state_input_event = true ; //  set flag, put to csr, perhaps interrupt
        set_register_dati_value(reg_input_change_addr, input_changed_addr, __func__) ;
        set_input_csr_dati_value_and_INTR() ;
        pthread_mutex_unlock(&on_after_input_register_access_mutex) ;
    }
}


// update lamps
void blinkenbone_c::worker_output_update() {
    // prescaler counts upwards to adapt to dynamic changed reg_input_period
    uint16_t effective_period = get_register_dato_value(reg_output_period) ;
    // map to 1..1000milli seconds, 0=off
    effective_period = (uint16_t)rangeToMinMax(effective_period, 0, 1000) ;

    if (effective_period == 0)
        return ; // period == 0: disable

    if (update_prescaler_ms < effective_period) {
        update_prescaler_ms++ ;
        return ;
    }
    update_prescaler_ms = 0 ; // reload

    if (!panel->connected())
        return ;

    // I think TCP/IP with 1 kHz is less a system load than
    // high frequency interrupts needed to register DATO changes
    // via on_after_register_access()
    panel->registers_to_panel_output_controls() ;
    if (panel->has_output_changed()) {
        panel->set_output_changed(false) ;
        panel->set_outputcontrols_values() ;
    }

    // in any case issue the periodic interrupt
    pthread_mutex_lock(&on_after_output_register_access_mutex) ;
    state_output_event = true ; //  set flag, put to csr, perhaps interrupt
    set_output_csr_dati_value_and_INTR() ;
    pthread_mutex_unlock(&on_after_output_register_access_mutex) ;
}


// background worker.
// poll input controls  (= panel switches) direct to register flipflops
void blinkenbone_c::worker(unsigned instance)
{
    UNUSED(instance); // only one
    timeout_c timeout;
    while (!workers_terminate) {
        timeout.wait_ms(1);

        worker_testmode_changed() ;
        worker_input_poll() ;
        worker_output_update() ;
    }
}

