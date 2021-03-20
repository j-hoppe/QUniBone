/* rx211.cpp: Implementation of the RX211 controller

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

//RX211_c::RX211_c(): RX11_c() {

RX211_c::RX211_c(void) : storagecontroller_c()
{
    unsigned i;
    is_RXV21 = false ;


    name.value = "ry"; // only one supported
    type_name.value = "RY211";
    log_label = "ry";

    // base addr, intr-vector, intr level
    set_default_bus_params(0777170, 16, 0264, 5);

    // Both drives are controlled by a single micro processor inside the double-drive box.
    uCPU = new RX0102uCPU_c(this);

    // add 2 RY disk drives
    drivecount = 2;
    for (i = 0; i < drivecount; i++) {
        RX0102drive_c *drive = new RX0102drive_c(uCPU,true);
        drive->unitno.value = i; // set the number plug
        drive->activity_led.value = i ; // default: LED = unitno
        drive->name.value = name.value + std::to_string(i);
        drive->log_label = drive->name.value;
        drive->parent = this; // link drive to controller
        storagedrives.push_back(drive);
        // also connect to microcontroller
        uCPU->drives.push_back(drive) ;
    }

    uCPU->set_RX02(true) ; // after drives instantiated

    // create QBUS/UNIBUS registers
    register_count = 2;

    // Control Status: reg no = 0, offset +0
    busreg_RX2CS = &(this->registers[0]);
    strcpy_s(busreg_RX2CS->name, sizeof(busreg_RX2CS->name), "RX2CS");
    busreg_RX2CS->active_on_dati = false; // can be read fast without ARM code, no state change
    busreg_RX2CS->active_on_dato = true; // writing changes controller state
    busreg_RX2CS->reset_value = 0; // not even DONE, INITIALIZING
    busreg_RX2CS->writable_bits = 0xffff;

    // Multipurpose Data Buffer register offset +2
    busreg_RX2DB = &(this->registers[1]);
    strcpy_s(busreg_RX2DB->name, sizeof(busreg_RX2DB->name), "RX2DB");
    busreg_RX2DB->active_on_dati = true; // read moves next byte to RXDB
    busreg_RX2DB->active_on_dato = true;
    busreg_RX2DB->reset_value = 0; // read default
    busreg_RX2DB->writable_bits = 0xffff;

    interrupt_enable = 0 ;

}

RX211_c::~RX211_c() {
    unsigned i;
    for (i = 0; i < drivecount; i++)
        delete storagedrives[i];
    delete uCPU ;
}


// RXV21 has is QBUS + DMA
RXV21_c::RXV21_c(): RX211_c() {
    is_RXV21 = true ;

    type_name.value = "RXV12";
    // INTR level 4 instead of RX11s 5
    set_default_bus_params(0777170, 16, 0264, 4);
}


bool RX211_c::on_param_changed(parameter_c *param) {
    if (param == &priority_slot) {
        dma_request.set_priority_slot(priority_slot.new_value);
        intr_request.set_priority_slot(priority_slot.new_value);
    } else if (param == &intr_level) {
        intr_request.set_level(intr_level.new_value);
    } else if (param == &intr_vector) {
        intr_request.set_vector(intr_vector.new_value);
    }

    return storagecontroller_c::on_param_changed(param); // more actions (for enable)
}


// reset controller, after installation, on power and on INIT
void RX211_c::reset(void) {
    reset_unibus_registers();

    DEBUG("RX211_c::reset()");
    interrupt_enable = false ;
    interrupt_condition_prev = false ;
    intr_request.edge_detect_reset();
    interrupt_condition_prev = true ;
    state = state_base ;
    done = true ;
    extended_address = 0 ;
    rx2ba = 0 ;
    uCPU->extended_status[1] = rx2wc = 0;
    error_dma_nxm = false ;
//    error_dma_word_count_overflow = false ;

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
void RX211_c::on_after_register_access(qunibusdevice_register_t *device_reg,
                                       uint8_t qunibus_control) {
    // on drive select:
    // move  status of new drive to controller status register
    // on command: signal worker thread

    switch (device_reg->index) {
    case 0:  // RXCS
        if (qunibus_control == QUNIBUS_CYCLE_DATO) {
//GPIO_SETVAL(gpios.led[0], 1); // inverted, led OFF
            uint16_t w = busreg_RX2CS->active_dato_flipflops ;

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
            uCPU->signal_selected_drive_unitno = !!(w & BIT(4));
            // CS<1:3> is cmd, write only
            function_select = uCPU->signal_function_code = (w >> 1) & 0x07;
            // CS<8> determines the desnity of the function to be executed
            function_density = uCPU->signal_function_density = !!(w & BIT(8));

            // CS<6> is IE
            interrupt_enable = !!(w & BIT(6));

            // CS<13:12> is bus address <17:16>
            extended_address = (w >> 12) & 3;

            // "future use", but must be read back
            csr09_10 = (w >> 10) & 3;

            // ZRXFB0, test13: write to lower byte of RXCS maps to RXDB?
            uCPU->rxdb = w & 0366; // only some bits map ?
            // visible on update_status()

            if (w & BIT(14)) { // INIT bit
                uCPU->init() ;
            } else if (uCPU->signal_done && (w & BIT(0))) { // GO bit
                if (function_select == RX11_CMD_EMPTY_BUFFER
                        || function_select == RX11_CMD_FILL_BUFFER
                        || function_select == RX11_CMD_READ_ERROR_CODE) {
                    // multistep Buffer DMA: RX211 operates uCPU via RXDB and does QUNIBUS DMA
                    // first BA and WC are received via RXDB, then uCPU&DMA started in background worker().
                    done = false ; // inhibit interrupts
                    error_dma_nxm = false ;
                    if (function_select == RX11_CMD_READ_ERROR_CODE) {
                        // very special: not touched and not transferred before BA
                        // previous WC value should be returned by extended status
                        dma_function_word_count = 4 ; // DO NOT TOUCH wc, only address to transfer
                        state = state_wait_rx2ba ; //
                    } else
                        state = state_wait_rx2wc ;
                    update_status("on_after_register_access() -> update_status") ; // may trigger interrupt
                } else
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
        } else if (qunibus_control == QUNIBUS_CYCLE_DATO) {
            // write RXDB to data port
            uint16_t w  = get_register_dato_value(busreg_RX2DB);
            switch(state) {
            case state_base:
                uCPU->rxdb_after_write(w) ; // forward to uCPU, alls update_status()
                break ;
            case state_wait_rx2wc:
                dma_function_word_count = uCPU->extended_status[1] = rx2wc = w & 0xff ; // save word count
                // in worker() test against uCPU transfer size
                state = state_wait_rx2ba ;
				update_status("on_after_register_access() state_wait_rx2ba -> update_status") ; // may trigger interrupt
                break ;
            case state_wait_rx2ba:
                rx2ba = w ; // save bus address
                state = state_dma_busy ;
				update_status("on_after_register_access() state_dma_busy -> update_status") ; // may trigger interrupt
                // wake up worker()
                // do DMA! with dma_buffer and dma_cycle DATI/DATO
                pthread_cond_signal(&on_after_register_access_cond);
                break ;
            case state_dma_busy:
            default:
                assert(false); // Show me stray write to RX2DB
            }
        }
    default: ; // ignore write
    }
}


// update RXCS & RXDB state for next DATI
// RXCS / DB read/Write access different registers.
// write current status into CS, for next read operation
// must be done after each DATO
// generates INTR too: on change on DONE or on change of INTENABLE
void RX211_c::update_status(const char *debug_info) {
    // update_status() *NOT* called both by DATI/DATO on_after_register_access() and uCPU worker thread
    //	pthread_mutex_lock(&status_mutex);

    // RXDB: in all cases set to RXES by uCPU
    uint16_t rxdb = uCPU->rxdb ;

    // Add controller errors to uCPU status
    if (error_dma_nxm)
        rxdb |= BIT(11) ;
    if (uCPU->signal_error_word_count_overflow)
        rxdb |= BIT(10) ;

    set_register_dati_value(busreg_RX2DB, uCPU->rxdb, debug_info);

    // both RX controller and the uCPU must be ready (DMA+interface operation)
    bool interrupt_condition = done && uCPU->signal_done && interrupt_enable ;

    uint16_t tmp = 0 ;

    // several bits are documented as "write-only" but ZRXFB0 test 11 reads them back
    tmp |= (uint16_t)extended_address << 12 ;
    tmp |= (uint16_t)csr09_10 << 9 ; // "future use"
    tmp |= (uint16_t)function_density << 8 ;
    tmp |= (uint16_t)uCPU->signal_selected_drive_unitno << 4 ;
    tmp |= (uint16_t)function_select << 1 ;
    tmp &= ~ BIT(3) ; // ZRXF test 11: function_select msb always 0 ???

    if (is_RXV21)
        tmp &= 05560 ; // RXV21: only 11,9,8,6,5,4 are read/write, almost as documented.

    tmp |= BIT(11); // we are RX02

    // RX2DB can accept data
    // - when controller waits for DMA wc,ba
    // - when the uCPU waits for some function parameter
    bool tr ;
    switch(state) {
    case state_base:
        tr = uCPU->signal_transfer_request ;
        break ;
    case state_wait_rx2wc:
    case state_wait_rx2ba:
        tr = true ;
        break ;
    case state_dma_busy:
    default:
        tr = false ;
    }
    if (tr)
        tmp |= BIT(7);


    if (uCPU->signal_error)
        tmp |= BIT(15);
    if (interrupt_enable)
        tmp |= BIT(6);
    if (done && uCPU->signal_done)
        tmp |= BIT(5);




    if (!interrupt_condition_prev && interrupt_condition) {
        // set CSR atomically with INTR signal lines
        DEBUG("%s: ERROR=%d, TR=%d, INTENB=%d, DONE=%d, interrupt!", debug_info,
              uCPU->signal_error, uCPU->signal_transfer_request, interrupt_enable, uCPU->signal_done) ;
        qunibusadapter->INTR(intr_request, busreg_RX2CS, tmp);
    } else {
        if (!interrupt_condition) // revoke INTR, if raised
            qunibusadapter->cancel_INTR(intr_request);
        set_register_dati_value(busreg_RX2CS, tmp, debug_info);
        DEBUG("%s: ERROR=%d, TR=%d, INTENB=%d, DONE=%d, no interrupt", debug_info,
              uCPU->signal_error, uCPU->signal_transfer_request, interrupt_enable, uCPU->signal_done) ;
    }

    interrupt_condition_prev = interrupt_condition ;

    //	pthread_mutex_unlock(&status_mutex);

}



// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void RX211_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
    // storagecontroller_c forwards to drives
    storagecontroller_c::on_power_changed(aclo_edge, dclo_edge);

    if (dclo_edge == SIGNAL_EDGE_RAISING) {
        // power-on - defaults
        reset();
        // but I need a valid state before that.
    }
}

// QBUS/UNIBUS INIT: clear some registers, not all error conditions
void RX211_c::on_init_changed(void) {

    // storagecontroller_c forwards to drives
    storagecontroller_c::on_init_changed();

    // write all registers to "reset-values"
    if (!init_asserted) // falling edge of INIT
        reset(); // triggers uCPU init()
}

// called by drive if ready or error
// handled by uCPU
void RX211_c::on_drive_status_changed(storagedrive_c *drive) {
    UNUSED(drive) ;
}

// Transfer uCPU -> buffer -> DMA DATO
// RX2WC and RX2BA already set via RXDB access
void RX211_c::worker_transfer_uCPU2DMA(void)
{
    uint16_t dma_buffer[256] ;
    uint16_t *writeptr = dma_buffer ;
    // limit DMA transfer to uCPU limit
    assert(state == state_dma_busy) ; // CSR control
    assert(sizeof(uint16_t)*dma_function_word_count <= sizeof(dma_buffer)) ;
    unsigned bus_addr = ((unsigned) extended_address << 16) | rx2ba ;
    done = false ;
    uCPU->signal_function_code = function_select ;
    uCPU->signal_function_density = function_density ;
    uCPU->go() ; // delay "DONE" until DMA ready.  needs rx2wc for count

    // !! in original hardware DMA cycles and access to RXDB are synchronous.
    // !! here, first all DMA, then all RXDB is done => rx2wc different while busy
    // uCPU waits now for rxdb data.  abort clears _transfer_request
    for (unsigned i=0 ; uCPU->signal_transfer_request && i < dma_function_word_count ; i++) {
        // byte-word conversion
        assert(uCPU->signal_transfer_request) ; // do not show these TR in CSR
        uint16_t w = uCPU->rxdb ; // LSB
        uCPU->rxdb_after_read() ; // triggers update_status()
        assert(uCPU->signal_transfer_request) ;
        w |= ((uint16_t)uCPU->rxdb) << 8 ; // MSB
        uCPU->rxdb_after_read() ;// triggers update_status()
        *writeptr++ = w ;
    }
    // TR signal_transfer_request may stay active, if less bytes are transfered then in sector buffer.
    // assert(!uCPU->signal_transfer_request) ;
    qunibusadapter->DMA(dma_request, true, QUNIBUS_CYCLE_DATO, bus_addr, dma_buffer, dma_function_word_count);
    if (function_select != RX11_CMD_READ_ERROR_CODE) {
        // RX11_CMD_READ_ERROR_CODE does not change "rx2wc" register
        unsigned new_rx2wc = rx2wc - (dma_request.qunibus_end_addr-bus_addr)/2 - 1;
        DEBUG("worker_transfer_uCPU2DMA.DMA() complete: bus_addr = 0%06o, end_addr=0%06o, nxm=%d, dmawc=%d, rx2wc=%d, new_rx2wc=%d",
              bus_addr, dma_request.qunibus_end_addr, (unsigned)error_dma_nxm, dma_function_word_count, rx2wc, new_rx2wc) ;
        rx2wc = uCPU->extended_status[1] = new_rx2wc ;
    }
    done = true ; // controller ready, uCPU may remain busy
    error_dma_nxm = !dma_request.success; // NXM
    // signal error if transfer had to be limited
//    error_dma_word_count_overflow = dma_effective_word_count > dma_function_word_count ;
//    if (error_dma_word_count_overflow)
//        uCPU->extended_status[0] = 0230 ;
}


// Transfer DMA -> buffer ->uCPU
// RX2WC and RX2BA already set via RXDB access
void RX211_c::worker_transfer_DMA2uCPU(void)
{
    uint16_t dma_buffer[256] ;
    uint16_t *readptr = dma_buffer ;
    assert(state == state_dma_busy) ; // CSR control
    assert(sizeof(uint16_t)*dma_function_word_count <= sizeof(dma_buffer)) ;

    unsigned bus_addr = ((unsigned) extended_address << 16) | rx2ba ;
    readptr = dma_buffer ;
    done = false ;
    // if DMA wordcount to small, remaining words are 0
    // !! in original hardware DMA cycles and access to RXDB are synchronous.
    // !! here, first all DMA, then all RXDB is done => rx2wc different while busy
    memset(dma_buffer, 0, sizeof(dma_buffer)) ;
    qunibusadapter->DMA(dma_request, true, QUNIBUS_CYCLE_DATI, bus_addr, dma_buffer,dma_function_word_count);
    error_dma_nxm = !dma_request.success; // NXM
    unsigned new_rx2wc = rx2wc - (dma_request.qunibus_end_addr-bus_addr)/2 - 1;
    DEBUG("worker_transfer_DMA2uCPU.DMA() complete: bus_addr = 0%06o, end_addr=0%06o, nxm=%d, dmawc=%d, rx2wc=%d, new_rx2wc=%d",
          bus_addr, dma_request.qunibus_end_addr, (unsigned)error_dma_nxm, dma_function_word_count, rx2wc, new_rx2wc) ;
    uCPU->signal_function_code = RX11_CMD_FILL_BUFFER ;
    uCPU->signal_function_density = function_density ;
    uCPU->go() ; // delay "DONE" until DMA ready. needs rx2wc for count
    // fill in all words, maybe with trailing 0s
    for (unsigned i=0 ; uCPU->signal_transfer_request &&  i < dma_function_word_count ; i++) {
        // byte-word conversion
        uint16_t w = *readptr++ ;
        assert(uCPU->signal_transfer_request) ; // do not show these TR in CSR
        uCPU->rxdb_after_write(w & 0xff) ;// LSB, triggers update_status()
        assert(uCPU->signal_transfer_request) ;
        uCPU->rxdb_after_write(w >> 8) ;// MSB, triggers update_status()
    }
    rx2wc = uCPU->extended_status[1] = new_rx2wc ;
    // TR signal_transfer_request may stay active, if less bytes are trasnfered then in sector buffer.
    // assert(!uCPU->signal_transfer_request) ;
    done = true ; // controller ready, uCPU may remain busy
}

// thread
// no background activity for bus interface
void RX211_c::worker(unsigned instance) {
    UNUSED(instance); // only one

    while (!workers_terminate) {
        // write into RXDB starts DMA
        int res = pthread_cond_wait(&on_after_register_access_cond,
                                    &on_after_register_access_mutex);
        if (res != 0) {
            ERROR("RX211_c::worker() pthread_cond_wait = %d = %s>", res, strerror(res));
            continue;
        }

        if (state != state_dma_busy)
            continue ;
        // only worker operation is DMA.
        // RX2WC and RX2BA set via RXDB access
        switch (function_select) {
        case RX11_CMD_EMPTY_BUFFER:
            if (uCPU->rx2wc_overflow_error(function_select, function_density, rx2wc))
                break ; // update_status() already called by uCPU
            worker_transfer_uCPU2DMA() ;
            break ;
        case RX11_CMD_FILL_BUFFER:
            if (uCPU->rx2wc_overflow_error(function_select, function_density, rx2wc))
                break ; // update_status() already called by uCPU
            worker_transfer_DMA2uCPU() ;
            break ;
        case RX11_CMD_READ_ERROR_CODE:
            // fix 4 word transfer, no rx2wc overflow possible
            worker_transfer_uCPU2DMA() ;
            break ;
        default:
            assert(false) ; // unexpected state
        }

        state = state_base ;
        done = true ;
        update_status(__func__) ;
    }
}

