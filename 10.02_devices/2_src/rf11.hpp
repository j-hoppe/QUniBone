/* 
    rf11.hpp: RF11 DECdisk UNIBUS controller

    Copyright (c) 2023 J. Dersch.
    Contributed under the BSD 2-clause license.

 */
#ifndef _RF11_HPP_
#define _RF11_HPP_

#include <fstream>
#include <cstdint>

using namespace std;

#include "utils.hpp"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"
#include "storagecontroller.hpp"
#include "rs11.hpp"

class rf11_c: public storagecontroller_c
{
private:
    // RF11 Registers (see Chapter 3 of manual):
    qunibusdevice_register_t *DCS_reg;   // Disk Control Status Register (777460)
    qunibusdevice_register_t *WC_reg;    // Word Count Register (777462)
    qunibusdevice_register_t *CMA_reg;   // Current Memory Address Register (777464)
    qunibusdevice_register_t *DAR_reg;   // Disk Address Register (777466)
    qunibusdevice_register_t *DAE_reg;   // Disk Address Ext & Error Register (777470)
    qunibusdevice_register_t *DBR_reg;   // Disk Data Buffer Register (777472)
    qunibusdevice_register_t *MAR_reg;   // Maintenance Register (777474)
    qunibusdevice_register_t *ADS_reg;   // Address of Disk Segment Register (777476)

    enum Function
    {
       NOP = 0,
       READ = 2,
       WRITE = 1,
       WRITE_CHECK = 3,
    };

    // DCS Register bits
    union DCS_bits
    {
       struct {
          unsigned GO  : 1;
          Function FR  : 2;
          unsigned MA  : 1;
          unsigned XM  : 2;
          unsigned IE  : 1;
          unsigned RDY : 1;
          unsigned DISK_CLEAR : 1;
          unsigned MXF : 1;
          unsigned WLO : 1;
          unsigned NED : 1;
          unsigned DPE : 1;
          unsigned WCE : 1;
          unsigned FRZ : 1;
          unsigned ERR : 1;
       } Flags;

       uint16_t Value; 
    } _dcs;

    uint16_t _wc;
    uint16_t _cma;
    uint16_t _dar;

    // DAE Register bits
    union DAE_bits
    {
       struct 
       {
          unsigned TA  : 2;
          unsigned DA  : 3;
          unsigned DA14 : 1;
          unsigned unused1 : 1;
          unsigned DRL : 1;
          unsigned CMA_INH : 1;
          unsigned unused2 : 1;
          unsigned NEM : 1;
          unsigned unused3 : 1;
          unsigned CTER : 1;
          unsigned BTER : 1;
          unsigned ATER : 1;
          unsigned APE : 1;
       } Flags;

       uint16_t Value; 
    } _dae;

    uint16_t _dbr;
    uint16_t _mar;   // TODO: implement only if necessary
    uint16_t _ads;

    volatile bool _new_command_ready;

    // Causes an interrupt if IDE is set
    void invoke_interrupt(void);

    // Resets all register values on BUS INIT or Control Reset functions
    // and any other relevant local state.
    void reset_controller(void);

    void update_memory_address(uint32_t newAddress);
    void update_disk_address(uint32_t newAddress);
    uint32_t get_current_disk_address(void);
   
    bool dma_read(uint32_t address, uint16_t* buffer, size_t count);
    bool dma_write(uint32_t address, uint16_t* buffer, size_t count);

    void reset_local_registers();
    void update_DCS();
    void update_WC();
    void update_CMA();
    void update_DAE();
    void update_DAR();
    void update_DBR();
    void update_ADS();

    enum WorkerState
    {
       Worker_Idle = 0,
       Worker_Execute = 1,
       Worker_Finish = 2,
    } _worker_state;

    rs11_c* _drive;

    // Unibusadapter: RK11 has one INTR and DMA
    // should be merged with RK11::DMARequest
    dma_request_c _dma_request = dma_request_c(this) ; // operated by qunibusadapter
    intr_request_c _intr_request = intr_request_c(this) ;

	
public:

    rf11_c();
    virtual ~rf11_c();    

    // background worker function
    void worker(unsigned instance) override;

    // called by qunibusadapter on emulated register access
    void on_after_register_access(
        qunibusdevice_register_t *device_reg,
        uint8_t unibus_control) override;

    bool on_param_changed(parameter_c *param) override;

    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;
  
    void on_drive_status_changed(storagedrive_c* drive);
};


#endif
