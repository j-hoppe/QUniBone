/* rx11211.hpp: Interface defintions of the RX11&RX211 controller

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


21-feb-21	JH      movedRX01 -> RX01,02
 */
#ifndef _RX_HPP_
#define _RX_HPP_

#include <pthread.h>
#include "qunibusadapter.hpp"
#include "storagecontroller.hpp"
#include "rx0102ucpu.hpp"




// function codes to uCPU
#define RX11_CMD_FILL_BUFFER 	0
#define RX11_CMD_EMPTY_BUFFER	1
#define RX11_CMD_WRITE_SECTOR	2
#define RX11_CMD_READ_SECTOR	3
#define RX211_CMD_SET_MEDIA_DENSITY 4 // RX211 only
#define RX11_CMD_READ_STATUS	5
#define RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA	6
#define RX11_CMD_READ_ERROR_CODE 7


// the RX11 controller is just an stateless interface between
// UNIBUS/QBUS and the RX01/02 micro processor

// common interface for RX11 and RX211
class RX11211_c     {
    friend class RX11_c ;
    friend class RX211_c ;
private:
    RX0102uCPU_c	*uCPU ; // the single micro controller for both drive mechanics

public:
    bool	is_RXV21 ; // QBUS version?

    // callback from uCPU to RX11/211 controller
    virtual void update_status(const char *debug_info) = 0 ;

} ;

class RX11_c: public storagecontroller_c, public RX11211_c {

private:

    // register interface to RX11 controller
    qunibusdevice_register_t *busreg_RXCS;	// Control Status: offset +0
    qunibusdevice_register_t *busreg_RXDB;		// multipurpose data register: offset +2

    // RX11 has no DMA
    intr_request_c intr_request = intr_request_c(this);

public:

    RX11_c(void);
    ~RX11_c(void);


    /*** registers accessed via RXCS/RXDB port ***/

    bool interrupt_condition_prev ; // find raising interrupt condition
    bool interrupt_enable ;

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
    virtual void update_status(const char *debug_info) override ;
    pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER ;

    void reset(void);

    // background worker function
    void worker(unsigned instance) override;


};


// RX211 is a pimped RX11, with density flags and DMA
// states to serially receive DMA address and len via RXDB.
class RX211_c: public storagecontroller_c, public RX11211_c {
private:

    /*** State ***/
    enum state_enum {
        state_base,
        state_wait_rx2wc, // wait for RXDB write fo RX2WC
        state_wait_rx2ba, // wait for RXDB write fo RX2BA
        state_dma_busy // worker() doing DMA and trasnfer to uCPU
    } ;
    enum state_enum state ;

    uint8_t extended_address ; // bit 17,16 of DMA address

    uint8_t function_select ;
    bool	function_density ;
    uint8_t	csr09_10 ; //"future use"
    bool	transfer_request ;
    bool	done ;

    // register interface to RX11 controller
    qunibusdevice_register_t *busreg_RX2CS; 	// Control Status: offset +0
    qunibusdevice_register_t *busreg_RX2DB;		// multipurpose data register: offset +2


    intr_request_c intr_request = intr_request_c(this);
    bool interrupt_condition_prev ; // find raising interrupt condition
    bool interrupt_enable ;

    dma_request_c dma_request = dma_request_c(this); // operated by qunibusadapter


    unsigned dma_function_word_count ; // needed DMA wordcount for function
//    unsigned dma_effective_word_count ; // cycles to transfer, limit of tr2wc and dma_function_word_count
    bool	error_dma_nxm ;
//    bool	error_dma_word_count_overflow ;

    void worker_transfer_uCPU2DMA(void) ;
    void worker_transfer_DMA2uCPU(void) ;

public:
    RX211_c(void) ;
    ~RX211_c(void);

    // DMA stuff  visible tp UCPU
    uint16_t rx2ba ; // bit<15:0> of DMA bus address
    uint16_t rx2wc ; // DMA word count register. visible to uCPU

    bool on_param_changed(parameter_c *param) ;

    // called by qunibusadapter after DATI/DATO access to active emulated register
    // Runs at 100% RT priority, QBUS/UNIBUS is stopped by SSYN/RPLY while this is running.
    void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control)
    override;

    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;
    void on_drive_status_changed(storagedrive_c *drive) ;

    // assemble content of RXCS and RXDB read register
    // called by uCPU on any status change
    // uCPU calls virtual RX11_c::update_status()
    void update_status(const char *debug_info) override ;
    pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER ;

    //	  void on_uCPU_status_changed(const char *debug_info) ;

    void reset(void);

    // background worker function
    void worker(unsigned instance) override;

} ;


class RXV11_c: public RX11_c {
public:
    RXV11_c(void) ;
} ;

class RXV21_c: public RX211_c {
public:
    RXV21_c(void) ;
} ;

#endif
