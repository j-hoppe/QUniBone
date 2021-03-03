/* rx11.cpp: Implementation of the RX11 controller

 Copyright (c) 2021, Joerg Hoppe
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


 5-jan-2021	JH	cloned from RL11

Documented configurations:
controller	drive
RX11,RXV11 	RX01	single density, 256kb
RX11,RXV11 	RX02	single density
RX211,RXV21 RX02	single or double density 256kb,512kb

 The microCPU board contains all logic and state for the RX01/02 subsystem.
 It is connected on one side two to "dump" electro-mechanical drives,
 on the other side two a RX11/RXV11/RX211/RXV21 UNIBUS/QBUS interface.

 Interface RX11 controeller - uCPU:
 RX11 -> uCPU:
   RUN   (GO bit, start command)
   INIT
 uCPU -> RX11:
   DONE  (command compelted, uCPU idle)
   TRANSFER REQUEST
   SHIFT, OUT  serial buffer transfer
   ERROR  error summary
  Bidi:
 DATA serial buffer transfer

 RX11 functions
 1. uCPU.start_function(code).
 2. read or write data to uCPU buffer
	 RX11 accesses uCPU "Buffer" under TRDB access, RX211 does DMA
 3. uCPU executes function when buffer filled
    RX211: must call uCPU.buffer_complete() if last word transfered?
 4. uCPU signals on_uCPU_complete() or on_uCPU_error()
    generates interrupt.
   Logic for "Read Status" and 2REad Error register":
   transfer 1st buffer word (RXES or RXER) to RXDB



 */

#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <string>

#include "logger.hpp"
#include "gpios.hpp"
#include "utils.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "rx11211.hpp"
#include "rx0102ucpu.hpp"
#include "rx0102drive.hpp"


RX11_c::RX11_c(void) :    storagecontroller_c() {
    unsigned i;

    name.value = "rx"; // only one supported
    type_name.value = "RX11";
    log_label = "rx";

    // base addr, intr-vector, intr level
    set_default_bus_params(0777170, 16, 0264, 5);

    // Both drives are controlled by a single micro processor inside the double-drive box.
    uCPU = new RX0102uCPU_c(this);

    // add 2 RX disk drives
    drivecount = 2;
    for (i = 0; i < drivecount; i++) {
        RX0102drive_c *drive = new RX0102drive_c(uCPU, false);
        drive->unitno.value = i; // set the number plug
        drive->name.value = name.value + std::to_string(i);
        drive->log_label = drive->name.value;
        drive->parent = this; // link drive to controller
        storagedrives.push_back(drive);
        // also connect to microcontroller
        uCPU->drives.push_back(drive) ;
    }

    uCPU->set_RX02(false) ; // after drives instantiated

    // create QBUS/UNIBUS registers
    register_count = 2;

    // Control Status: reg no = 0, offset +0
    busreg_RXCS = &(this->registers[0]);
    strcpy_s(busreg_RXCS->name, sizeof(busreg_RXCS->name), "RXCS");
    busreg_RXCS->active_on_dati = false; // can be read fast without ARM code, no state change
    busreg_RXCS->active_on_dato = true; // writing changes controller state
    busreg_RXCS->reset_value = 0; // not even DONE, INITIALIZING
    busreg_RXCS->writable_bits = 0xffff;

    // Multipurpose Data Buffer register offset +2
    busreg_RXDB = &(this->registers[1]);
    strcpy_s(busreg_RXDB->name, sizeof(busreg_RXDB->name), "RXDB");
    busreg_RXDB->active_on_dati = true; // read moves next byte to RXDB
    busreg_RXDB->active_on_dato = true;
    busreg_RXDB->reset_value = 0; // read default
    busreg_RXDB->writable_bits = 0xffff;

    interrupt_enable = 0 ;

}

RX11_c::~RX11_c() {
    unsigned i;
    for (i = 0; i < drivecount; i++)
        delete storagedrives[i];
    delete uCPU ;
}

// RXV11  QBUS without DMA
RXV11_c::RXV11_c(): RX11_c() {
    type_name.value = "RXV11";
    // base addr, intr-vector, intr level
    // RXV11 has INTR level 4 instead of RX11s 5
   set_default_bus_params(0777170, 16, 0264, 4);
}


// called when "enabled" goes true, before registers plugged to QBUS/UNIBUS
// result false: configuration error, do not install
bool RX11_c::on_before_install() {
    return true ;
}

void RX11_c::on_after_install() {
    // poll signal wires from uCPU
    update_status("on_after_install() -> update_status") ;
}

void RX11_c::on_after_uninstall() {
}


bool RX11_c::on_param_changed(parameter_c *param) {
    if (param == &priority_slot) {
        intr_request.set_priority_slot(priority_slot.new_value);
    } else if (param == &intr_level) {
        intr_request.set_level(intr_level.new_value);
    } else if (param == &intr_vector) {
        intr_request.set_vector(intr_vector.new_value);
    }

    return storagecontroller_c::on_param_changed(param); // more actions (for enable)
}


// reset controller, after installation, on power and on INIT
void RX11_c::reset(void) {
    reset_unibus_registers();

    DEBUG("RX11_c::reset()");
    interrupt_enable = false ;
    interrupt_condition_prev = false ;
    intr_request.edge_detect_reset();
    interrupt_condition_prev = true ;

    uCPU->init() ; // home, read boot sector
    // generates done = 0,1 sequence
    update_status("reset() -> update_status");
}


// Access to QBUS/UNIBUS register interface
// called with 100% CPU highest RT priority.
// QBUS/UNIBUS is stopped by SSYN/RPLY while this is running.
// No loops! no drive, console, file or other operations!
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void RX11_c::on_after_register_access(qunibusdevice_register_t *device_reg,
                                      uint8_t qunibus_control) {
    // on drive select:
    // move  status of new drive to controller status register
    // on command: signal worker thread

    switch (device_reg->index) {
    case 0:  // RXCS
        if (qunibus_control == QUNIBUS_CYCLE_DATO) {
//GPIO_SETVAL(gpios.led[0], 1); // inverted, led OFF

            // Not clear, which bits may be written when DONE=0 (busy)
            // "go" disabled
            // RX11 DX.MAC: sets INT enable while INIT active
            // INIT interrupts GO ?
            // But: Changes of Unit select" while CPU active??"


            // write only allowed if RX02 is not busy
            // if (! uCPU->signal_done)
            //    break; // ignore write

            pthread_mutex_lock(&on_after_register_access_mutex);

            // CS<4> = drive select, write only, forwad to uCPU
            uCPU->signal_selected_drive_unitno = (busreg_RXCS->active_dato_flipflops >> 4) & 1;
            // CS<1:3> is cmd, write only
            uCPU->signal_function_code = (busreg_RXCS->active_dato_flipflops >> 1) & 0x07;
            // CS<6> is IE
            interrupt_enable = !!(busreg_RXCS->active_dato_flipflops & (1 << 6));

            uCPU->signal_function_density = (busreg_RXCS->active_dato_flipflops >> 8) & 1;

            // write to RXCS clears RXDB?
            // Only reference: AH-9341F-MC__RX11__RX11_INTFC__CZRXBF0__(C)74-79.pdf, line #916
            uCPU->rxdb = 0 ;
            //set_register_dati_value(busreg_RXDB, 0, __func__);


            if ((busreg_RXCS->active_dato_flipflops >> 14) & 1) { // INIT bit
                uCPU->init() ;
            } else if (uCPU->signal_done && busreg_RXCS->active_dato_flipflops & 1) { // GO bit
                uCPU->go() ; // execute uCPU.function_code
            } else
                // register status NOT updated via uCPU activity
                update_status("on_after_register_access() -> update_status") ; // may trigger interrupt

            pthread_mutex_unlock(&on_after_register_access_mutex);
        } else {
            // CS reg is not "active_on_dati"
            //  set value in code with "set_register_dati_value(reg_CS, CS_read) ;"
        }
        break;
    case 1:  // AFTER access to RXDB, multifunction
        // read from data port to RXDB
        if (qunibus_control == QUNIBUS_CYCLE_DATI) {
            // read moves next byte to rxdb
            uCPU->rxdb_after_read() ;
            // has read current value of rxdb, now deliver next via
            // on_uCPU_status_changed() to rxdb, calling
            //  set_register_dati_value(busreg_RXDB, w, __func__);
        } else if (qunibus_control == QUNIBUS_CYCLE_DATO) {
            // write RXDB to data port
            uint16_t w  = get_register_dato_value(busreg_RXDB);
            uCPU->rxdb_after_write(w) ;
        }
        break ;
    default: ; // ignore write
    }
}


// update RXCS & RXDB state for next DATI
// RXCS / DB read/Write access different registers.
// write current status into CS, for next read operation
// must be done after each DATO
// generates INTR too: on change on DONE or on change of INTENABLE
void RX11_c::update_status(const char *debug_info) {
    // update_status() *NOT* called both by DATI/DATO on_after_register_access() and uCPU worker thread
    //	pthread_mutex_lock(&status_mutex);

    // RXDB
    set_register_dati_value(busreg_RXDB, uCPU->rxdb, debug_info);

    bool interrupt_condition = uCPU->signal_done && interrupt_enable ;

    uint16_t tmp = 0;

    if (uCPU->signal_error)
        tmp |= BIT(15);
    if (uCPU->signal_transfer_request)
        tmp |= BIT(7);
    if (interrupt_enable)
        tmp |= BIT(6);
    if (uCPU->signal_done)
        tmp |= BIT(5);

    if (!interrupt_condition_prev && interrupt_condition) {
        // set CSR atomically with INTR signal lines
        DEBUG("%s: ERROR=%d, TR=%d, INTENB=%d, DONE=%d, interrupt!", debug_info,
              uCPU->signal_error, uCPU->signal_transfer_request, interrupt_enable, uCPU->signal_done) ;
        qunibusadapter->INTR(intr_request, busreg_RXCS, tmp);
    } else {
        if (!interrupt_condition) // revoke INTR, if raised
            qunibusadapter->cancel_INTR(intr_request);
        set_register_dati_value(busreg_RXCS, tmp, debug_info);
        DEBUG("%s: ERROR=%d, TR=%d, INTENB=%d, DONE=%d, no interrupt", debug_info,
              uCPU->signal_error, uCPU->signal_transfer_request, interrupt_enable, uCPU->signal_done) ;
    }

    interrupt_condition_prev = interrupt_condition ;

    //	pthread_mutex_unlock(&status_mutex);

}



// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void RX11_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
    // storagecontroller_c forwards to drives
    storagecontroller_c::on_power_changed(aclo_edge, dclo_edge);

    if (dclo_edge == SIGNAL_EDGE_RAISING) {
        // power-on - defaults
        reset();
        // but I need a valid state before that.
    }
}

// QBUS/UNIBUS INIT: clear some registers, not all error conditions
void RX11_c::on_init_changed(void) {

    // storagecontroller_c forwards to drives
    storagecontroller_c::on_init_changed();

    // write all registers to "reset-values"
    if (!init_asserted) // falling edge of INIT
        reset(); // triggers uCPU init()
}

// called by drive if ready or error
// handled by uCPU
void RX11_c::on_drive_status_changed(storagedrive_c *drive) {
    UNUSED(drive) ;
}


// thread
// no background activity for bus interface
void RX11_c::worker(unsigned instance) {
    UNUSED(instance); // only one
}

