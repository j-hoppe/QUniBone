/* rx11.hpp: Implementation of the RX11,RX211 controller

 Copyright (c) 2020, Joerg Hoppe
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


5-jan-20	JH      cloned from RL
 */
#ifndef _RX11_HPP_
#define _RX11_HPP_

#include "qunibusadapter.hpp"
#include "storagecontroller.hpp"
#include "RX0102ucpu.hpp"


// the RX controller is jsut an stateless interface between
// UNIBUS/QBUS and the RX01/02 micro processor

class RX11_c: public storagecontroller_c {
    friend class RXV11_c ;
    friend class RX211_c ;
    friend class RXV21_c ;

private:

    /** everything shared between different threads must be "volatile" */
    RX0102uCPU_c	*uCPU ; // the single micro controller for both drive mechanics


    /*** registers accessed via RXCS/RXDB port ***/
    // RX11 has no DMA, RX211 has one INTR and DMA
    dma_request_c dma_request = dma_request_c(this); // operated by qunibusadapter
    intr_request_c intr_request = intr_request_c(this);

    bool interrupt_condition_prev ; // find raising interrupt condition
    bool interrupt_enable ;


public:

    // register interface to  RL11 controller
    qunibusdevice_register_t *busreg_RXCS; 	// Control Status: offset +0
    qunibusdevice_register_t *busreg_RXDB;		// multipurpose data register: offset +2

    RX11_c(void);
    ~RX11_c(void);

    bool on_before_install(void) override ;
    void on_after_install(void) override ;
    void on_after_uninstall(void) override ;

    bool on_param_changed(parameter_c *param) override;

    // called by qunibusadapter after DATI/DATO access to active emulated register
    // Runs at 100% RT priority, QBUS/UNIBUS is stopped by SSYN/RPLY while this is running.
    void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control)
    override;

    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;
    void on_drive_status_changed(storagedrive_c *drive) ;

    // assemble content of RXCS and RXDB read register
    // called by uCPU on any status change
    void update_status(const char *debug_info);

//    void on_uCPU_status_changed(const char *debug_info) ;

    void reset(void);

    // background worker function
    void worker(unsigned instance) override;


};


// mutants
class RX211_c: public RX11_c {
public:
    RX211_c(void) ;
} ;


class RXV11_c: public RX11_c {
public:
    RXV11_c(void) ;
} ;

class RXV21_c: public RX11_c {
public:
    RXV21_c(void) ;
} ;

#endif
