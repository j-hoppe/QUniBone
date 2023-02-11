/* 
    rf11_cpp: RF11 UNIBUS controller 

    Copyright (c) 2013 J. Dersch.
    Contributed under the BSD 2-clause license.

 */

#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "logger.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"
#include "storagecontroller.hpp"
#include "rf11.hpp"   
#include "rs11.hpp"

rf11_c::rf11_c() :
    storagecontroller_c(),
    _wc(0),
    _cma(0),
    _dar(0),
    _dbr(0),
    _mar(0),
    _ads(0),
    _new_command_ready(false),
    _drive(nullptr)
{
    // static config
    name.value = "rf";
    type_name.value = "RF11";
    log_label = "rf";

    // base addr, intr-vector, intr level
    set_default_bus_params(0777460, 10, 0204, 5) ;

    // The RF11 controller has eight registers,
    register_count = 8;

    // Drive Control/Status register (read/write)
    DCS_reg = &(this->registers[0]); // @  base addr
    strcpy(DCS_reg->name, "DCS");
    DCS_reg->active_on_dati = false; // no controller state change
    DCS_reg->active_on_dato = true;
    DCS_reg->reset_value =   0000200; // RDY set
    DCS_reg->writable_bits = 0000577; // write bits 8, 6-0

    // Word Count Register (read/write)
    WC_reg = &(this->registers[1]); // @  base addr + 2
    strcpy(WC_reg->name, "WC"); 
    WC_reg->active_on_dati = false; // no controller state change
    WC_reg->active_on_dato = false;
    WC_reg->reset_value = 0;
    WC_reg->writable_bits = 0177777;

    // Current Memory Address Register (read only)
    CMA_reg = &(this->registers[2]); // @  base addr + 4
    strcpy(CMA_reg->name, "CMA");
    CMA_reg->active_on_dati = false;
    CMA_reg->active_on_dato = false;
    CMA_reg->reset_value = 0000000;
    CMA_reg->writable_bits = 0177777; 

    // Disk Address Register (read/write)
    DAR_reg = &(this->registers[3]); // @  base addr + 6
    strcpy(DAR_reg->name, "DAR");
    DAR_reg->active_on_dati = false;
    DAR_reg->active_on_dato = false;
    DAR_reg->reset_value = 0;
    DAR_reg->writable_bits = 0177777;

    // Disk Address Ext & Error Register (read/write)
    DAE_reg = &(this->registers[4]); // @  base addr + 10
    strcpy(DAE_reg->name, "DAE");
    DAE_reg->active_on_dati = false;
    DAE_reg->active_on_dato = true;
    DAE_reg->reset_value = 0;
    DAE_reg->writable_bits = 0000677;  // write bits 8-7, 5-0

    // Disk Data Buffer Register (read/write)
    DBR_reg = &(this->registers[5]); // @  base addr + 12
    strcpy(DBR_reg->name, "DBR");
    DBR_reg->active_on_dati = false;
    DBR_reg->active_on_dato = false;
    DBR_reg->reset_value = 0;
    DBR_reg->writable_bits = 0177777;

    // Maintenance Register (write only)
    MAR_reg = &(this->registers[6]); // @  base addr + 14
    strcpy(MAR_reg->name, "MAR");
    MAR_reg->active_on_dati = false;
    MAR_reg->active_on_dato = true;
    MAR_reg->reset_value = 0;
    MAR_reg->writable_bits = 0177777;

    // Address of Disk Segment Register (read only)
    ADS_reg = &(this->registers[7]); // @  base addr + 16
    strcpy(ADS_reg->name, "ADS");
    ADS_reg->active_on_dati = false;
    ADS_reg->active_on_dato = false;
    ADS_reg->reset_value = 0;
    ADS_reg->writable_bits = 0;  // read only

    //
    // Drive configuration: we attach one rs11_c instance (which can encompass up to 8 rs11 drives)
    //
    _drive = new rs11_c(this);
    _drive->unitno.value = 0;
    _drive->activity_led.value = 0 ; // default: LED = unitno
    _drive->name.value = name.value + std::to_string(0);
    _drive->log_label = _drive->name.value;
    _drive->parent = this;
    storagedrives.push_back(_drive);

    reset_local_registers();
}
  
rf11_c::~rf11_c()
{
    delete _drive;
}

// return false, if illegal parameter value.
// verify "new_value", must output error messages
bool rf11_c::on_param_changed(parameter_c *param) 
{
   // no own parameter or "enable" logic
   if (param == &priority_slot) 
   {
      _dma_request.set_priority_slot(priority_slot.new_value);
      _intr_request.set_priority_slot(priority_slot.new_value);
   } 
   else if (param == &intr_level) 
   {
      _intr_request.set_level(intr_level.new_value);
   } 
   else if (param == &intr_vector) 
   {
      _intr_request.set_vector(intr_vector.new_value);
   }	
   return storagecontroller_c::on_param_changed(param) ; // more actions (for enable)
}

// Background worker.
// Handle device operations.
void rf11_c::worker(unsigned instance) 
{
    UNUSED(instance) ; // only one

    worker_init_realtime_priority(rt_device);
    _worker_state = Worker_Idle;
    timeout_c timeout;
    while (!workers_terminate) 
    {
       switch (_worker_state)
       {
          case Worker_Idle:
          {
             pthread_mutex_lock(&on_after_register_access_mutex);

             // Wait for a new command to show up:
             while (!_new_command_ready)
             {
                pthread_cond_wait(&on_after_register_access_cond, &on_after_register_access_mutex);
             }

             _new_command_ready = false;

             pthread_mutex_unlock(&on_after_register_access_mutex);
           
             // And move to the Execute state to actually do the work.
             _worker_state = Worker_Execute; 
          }
          break;

          case Worker_Execute:
             // Do the transfer one word at a time, pacing things approximately as the original
             // hardware did.
             {
             _wc = get_register_dato_value(WC_reg);
             uint16_t wordCount = ~_wc + 1;
             uint16_t buffer[wordCount]; 
             uint32_t currentAddress = get_register_dato_value(CMA_reg) | (_dcs.Flags.XM << 16);
             uint32_t currentDiskAddress = get_current_disk_address();    
 
             if (_dcs.Flags.FR == READ || _dcs.Flags.FR == WRITE_CHECK)
                {
                   // Either a Read (which just reads a word into memory from disk)
                   // or a Write Check (which reads a word and compares it to the one in memory)
                   if (!_drive->read(currentDiskAddress, buffer, wordCount))
                   {
                      // Invalid address:
                      _dcs.Flags.NED = 1;
                   }
                   else 
                   {
                      if (_dcs.Flags.FR == READ)
                      {
                         // Transfer the word to memory
                         if (!dma_write(currentAddress, buffer, wordCount))
                         {
                            // DMA failed, set the non-existent memory flag and abort.
                            _dae.Flags.NEM = 1;
                         }
                      }
                      else
                      {
                         // Compare the word to memory
                         uint16_t compareBuffer[wordCount];
                         if (!dma_read(currentAddress, compareBuffer, wordCount))
                         {
                            // As above
                            _dae.Flags.NEM = 1;
                         }
                         else if (memcmp(compareBuffer, buffer, wordCount))
                         {
                            _dcs.Flags.WCE = 1;
                         }
                      }
                   }
                }
                else
                {
                   // A Write operation
                   if (!dma_read(currentAddress, buffer, wordCount))
                   {
                      _dae.Flags.NEM = 1;
                   }
                   else if (!_drive->write(currentDiskAddress, buffer, wordCount))
                   {
                      // Invalid address:
                      _dcs.Flags.NED = 1;
                   } 
                }
  
                // Update the value in the Disk Buffer Register
                _dbr = buffer[wordCount - 1];
                update_DBR();

                if (!_dae.Flags.CMA_INH) 
                { 
                    update_memory_address(currentAddress + wordCount * 2);
                } 
                update_disk_address(currentDiskAddress + wordCount);

                // WC should be zero at the end of the transfer normally, but if there's a failure
                // it should point to where the failure took place.  However since we're not emulating
                // bad media (only NXM, NED errors) this is less of an issue. 
                _wc = 0;
                update_WC();

                // wait 16uS per word to simulate the delay of the platter rotation:
                timeout.wait_us(16 * wordCount);      
                _worker_state = Worker_Finish;
             }
             break;

          case Worker_Finish:
             // Transfer complete, set flags as appropriate:
             _dcs.Flags.RDY = 1;
             update_DAE();
             update_DCS();

             if (_dcs.Flags.IE)
             {
                // Invoke an interrupt to let the '11 know we're done:
                qunibusadapter->INTR(_intr_request, nullptr, 0);
             }
             _worker_state = Worker_Idle;
             pthread_mutex_unlock(&on_after_register_access_mutex);
             break;
        }
    }
}

//
// process DATI/DATO access to the RF11's "active" registers.
// !! called asynchronuously by PRU, with SSYN/RPLY asserted and blocking QBUS/UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void rf11_c::on_after_register_access(
    qunibusdevice_register_t *device_reg,
    uint8_t unibus_control)
{
    UNUSED(unibus_control);
    switch(device_reg->index)
    {
       case 0: // DCS
          // Mask in just the bits that are writeable:
          _dcs.Value = 
             (_dcs.Value & ~DCS_reg->writable_bits) | (DCS_reg->active_dato_flipflops & DCS_reg->writable_bits);

          if (_dcs.Flags.DISK_CLEAR)
          {
              reset_controller();
          }

          if (_dcs.Flags.GO)
          {
              pthread_mutex_lock(&on_after_register_access_mutex);

              // GO clears WCE, and GO (of the flags we're emulating)
              _dcs.Flags.GO = 0;
              _dcs.Flags.WCE = 0;
 
              if (!_dcs.Flags.RDY)
              {
                  // TODO: does this raise an error?  Docs don't make this clear,
                  // and there are no obvious status bits to set in this case.
                  // For now, I'm treating this as a no-op.
              }
              else if (_dcs.Flags.FR != NOP)
              {
                  _new_command_ready = true;

                  // Controller will be busy until the worker completes.
                  _dcs.Flags.RDY = 0;
                  _dcs.Flags.ERR = 0;
              }

              pthread_cond_signal(&on_after_register_access_cond);
              pthread_mutex_unlock(&on_after_register_access_mutex);
          }
          update_DCS();
          break; 

       case 4: // DAE
           _dae.Value =
             (_dae.Value & ~DAE_reg->writable_bits) | (DAE_reg->active_dato_flipflops & DAE_reg->writable_bits);
          INFO("DAE %o", _dae.Value); 
          update_DAE();
          break;

       case 6: // MAR
          _mar = MAR_reg->active_dato_flipflops;
          // TODO: do something if we plan on actually emulating maintenance stuff.
          INFO("MAR %o", _mar);
          break;

       default:
          // Should never happen
          INFO("Unexpected write to undefined register.");
          break;
    }
}

uint32_t rf11_c::get_current_disk_address()
{
    return get_register_dato_value(DAR_reg) | (_dae.Flags.TA << 16) | (_dae.Flags.DA << 18);
}

void rf11_c::update_memory_address(uint32_t newAddress)
{
   _cma = newAddress & 0xffff;
   _dcs.Flags.XM = (newAddress & 0x30000) >> 16;
   update_CMA();
   update_DCS();
}

void rf11_c::update_disk_address(uint32_t newAddress)
{
   _dar = newAddress & 0xffff;
   _dae.Flags.TA = ((newAddress &  0x30000) >> 16) & 0x3;
   _dae.Flags.DA = ((newAddress & 0x1c0000) >> 18) & 0x7;
   update_DAE();
   update_DAR();
}

bool rf11_c::dma_read(uint32_t address, uint16_t* buffer, size_t count)
{
   if (address + count * 2 > qunibus->addr_space_byte_count)
   {
      return false;
   }

   qunibusadapter->DMA(_dma_request,
      true,
      QUNIBUS_CYCLE_DATI,
      address,
      buffer,
      count);
   return _dma_request.success;
}

bool rf11_c::dma_write(uint32_t address, uint16_t* buffer, size_t count)
{
   if (address + count * 2 > qunibus->addr_space_byte_count)
   {
      return false;
   }

   qunibusadapter->DMA(_dma_request,
      true,
      QUNIBUS_CYCLE_DATO,
      address,
      buffer,
      count);
   return _dma_request.success;
}

void rf11_c::update_DCS()
{
    // (note: we only take into account error flags that we actually emulate)
    _dcs.Flags.FRZ = (_dae.Flags.NEM) ? 1 : 0;
    _dcs.Flags.ERR = (_dcs.Flags.FRZ | _dcs.Flags.WCE | _dcs.Flags.NED) ? 1 : 0;
    set_register_dati_value(DCS_reg, _dcs.Value, "update_DCS");
}

void rf11_c::update_DAE()
{
    INFO("DAE %o", _dae.Value);
    set_register_dati_value(DAE_reg, _dae.Value, "update_DAE");
}

void rf11_c::update_CMA()
{
    set_register_dati_value(CMA_reg, _cma, "update_CMA");
}

void rf11_c::update_WC()
{
    set_register_dati_value(WC_reg, _wc, "update_WC");
}

void rf11_c::update_DAR()
{
    set_register_dati_value(DAR_reg, _dar, "update_DAR");
}

void rf11_c::update_DBR()
{
    set_register_dati_value(DBR_reg, _dbr, "update_DBR");
}

void rf11_c::update_ADS()
{
    set_register_dati_value(ADS_reg, _ads, "update_ADS");
}

void rf11_c::on_drive_status_changed(storagedrive_c *drive)
{
}

void rf11_c::reset_controller(void)
{
    // This will reset the DATI values to their defaults.
    // We then need to reset our copy of the values to correspond.
    reset_unibus_registers();
    reset_local_registers();
}

void rf11_c::reset_local_registers()
{
    // Controller is ready after reset
    _dcs.Value = 0;
    _dcs.Flags.RDY = 1;
    _dae.Value = 0;
    _wc = 0;
    _cma = 0;
    _dar = 0;
    _dbr = 0;
}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void rf11_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
    storagecontroller_c::on_power_changed(aclo_edge, dclo_edge);

    if (dclo_edge == SIGNAL_EDGE_RAISING) 
    { 
        // power-on defaults
        reset_controller();
    }
}

// QBUS/UNIBUS INIT: clear all registers
void rf11_c::on_init_changed(void) 
{
    // write all registers to "reset-values"
    if (init_asserted) 
    {
        reset_controller();
    }

    storagecontroller_c::on_init_changed();
}
