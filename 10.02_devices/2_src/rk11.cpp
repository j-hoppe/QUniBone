/* 
    rk11_cpp: RK11 QBUS/UNIBUS controller 

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
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
#include "rk11.hpp"   
#include "rk05.hpp"

rk11_c::rk11_c() :
    storagecontroller_c(),
    _new_command_ready(false)
{
    // static config
    name.value = "rk";
    type_name.value = "RK11";
    log_label = "rk";

	// base addr, intr-vector, intr level
	set_default_bus_params(0777400, 10, 0220, 5) ;

    // The RK11 controller has seven registers,
    // We allocate 8 because one address in the address space is unused.
    register_count = 8;

    // Drive Status register (read-only)
    RKDS_reg = &(this->registers[0]); // @  base addr
    strcpy(RKDS_reg->name, "RKDS");
    RKDS_reg->active_on_dati = false; // no controller state change
    RKDS_reg->active_on_dato = false;
    RKDS_reg->reset_value = 0;
    RKDS_reg->writable_bits = 0x0000; // read only

    // Error Register (read-only)
    RKER_reg = &(this->registers[1]); // @  base addr + 2
    strcpy(RKER_reg->name, "RKER"); 
    RKER_reg->active_on_dati = false; // no controller state change
    RKER_reg->active_on_dato = false;
    RKER_reg->reset_value = 0;
    RKER_reg->writable_bits = 0x0000; // read only

    // Control Status Register (read/write)
    RKCS_reg = &(this->registers[2]); // @  base addr + 4
    strcpy(RKCS_reg->name, "RKCS");
    RKCS_reg->active_on_dati = false;
    RKCS_reg->active_on_dato = true;
    RKCS_reg->reset_value = 0x0080;	// RDY high after INIT
    RKCS_reg->writable_bits = 0x0f7f;

    // Word Count Register (read/write)
    RKWC_reg = &(this->registers[3]); // @  base addr + 6
    strcpy(RKWC_reg->name, "RKWC");
    RKWC_reg->active_on_dati = false;
    RKWC_reg->active_on_dato = false;
    RKWC_reg->reset_value = 0;
    RKWC_reg->writable_bits = 0xffff;

    // Current Bus Address Register (read/write)
    RKBA_reg = &(this->registers[4]); // @  base addr + 10
    strcpy(RKBA_reg->name, "RKBA");
    RKBA_reg->active_on_dati = false;
    RKBA_reg->active_on_dato = false;
    RKBA_reg->reset_value = 0;
    RKBA_reg->writable_bits = 0xffff;

    // Disk Address Register (write only?)
    RKDA_reg = &(this->registers[5]); // @  base addr + 12
    strcpy(RKDA_reg->name, "RKDA");
    RKDA_reg->active_on_dati = false;
    RKDA_reg->active_on_dato = true;  // To allow writes only when controller is in READY state.
    RKDA_reg->reset_value = 0;
    RKDA_reg->writable_bits = 0xffff;

    // Unused entry (@ Base addr + 14)
    
    // Data Buffer Register (read only)
    RKDB_reg = &(this->registers[7]); // @  base addr + 16
    strcpy(RKDB_reg->name, "RKDB");
    RKDB_reg->active_on_dati = false;
    RKDB_reg->active_on_dato = false;
    RKDB_reg->reset_value = 0;
    RKDB_reg->writable_bits = 0x0000; // read only

    _rkda_drive = 0;

    //
    // Drive configuration: up to eight drives.
    //
    drivecount = 8;
    for (uint32_t i=0; i<drivecount; i++)
    {
        rk05_c *drive = new rk05_c(this);
        drive->unitno.value = i;
        drive->activity_led.value = i ; // default: LED = unitno
        drive->name.value = name.value + std::to_string(i);
        drive->log_label = drive->name.value;
        drive->parent = this;
        storagedrives.push_back(drive);
    }
}
  
rk11_c::~rk11_c()
{
    for (uint32_t i=0; i<drivecount; i++)
    {
        delete storagedrives[i];
    }
}

// return false, if illegal parameter value.
// verify "new_value", must output error messages
bool rk11_c::on_param_changed(parameter_c *param) {
	// no own parameter or "enable" logic
	if (param == &priority_slot) {
		dma_request.set_priority_slot(priority_slot.new_value);
		intr_request.set_priority_slot(priority_slot.new_value);
	}  else if (param == &intr_level) {
		intr_request.set_level(intr_level.new_value);
	} else if (param == &intr_vector) {
		intr_request.set_vector(intr_vector.new_value);
	}	
	return storagecontroller_c::on_param_changed(param) ; // more actions (for enable)
}


void rk11_c::dma_transfer(DMARequest &request)
{
    timeout_c timeout;

    if (request.iba)
    {
        //
        // If IBA is set for this transfer ("inhibit incrementing Bus Address") then what
        // happens on the real machine is each word being transferred goes to the same
        // address.  We can simplify these operations without having to do weird things 
        // with DMA.
        //
        if (request.write)
        {
            // Write FROM buffer TO qunibus memory, IBA on:
            // We only need to write the last word in the buffer to memory.
				qunibusadapter->DMA(dma_request, true,
                QUNIBUS_CYCLE_DATO,
                request.address,
                request.buffer + request.count - 1,
                1);
            request.timeout = !dma_request.success ;
        }
        else
        {
            // Read FROM qunibus memory TO buffer, IBA on:
            // We read a single word from the qunibus and fill the
            // entire buffer with this value. 
            qunibusadapter->DMA(dma_request, true,
                QUNIBUS_CYCLE_DATI,
                request.address,
                request.buffer,
                1);
		request.timeout = !dma_request.success ;
        } 
    }
    else
    {
        // Just a normal DMA transfer.
        if (request.write)
        {
            // Write FROM buffer TO qunibus memory
            qunibusadapter->DMA(dma_request, true,
                QUNIBUS_CYCLE_DATO,
                request.address,
                request.buffer,
                request.count);
		request.timeout = !dma_request.success ;
        }
        else
        {
            // Read FROM qunibus memory TO buffer
            qunibusadapter->DMA(dma_request, true,
                QUNIBUS_CYCLE_DATI, 
                request.address,
                request.buffer,
                request.count);
		request.timeout = !dma_request.success ;
        }
    }


    // If an IBA DMA read from memory, we need to fill the request buffer
    // with the single word returned from memory by the DMA operation.
    if (request.iba && !request.write)
    {
        for(uint32_t i = 1; i < request.count; i++)
        {
            request.buffer[i] = request.buffer[0];
        }
    }
}

// Background worker.
// Handle device operations.
void rk11_c::worker(unsigned instance) 
{
	UNUSED(instance) ; // only one

    worker_init_realtime_priority(rt_device);

    _worker_state = Worker_Idle;
    WorkerCommand command = { 0 };

    bool do_interrupt = true;
    timeout_c timeout;
    while (!workers_terminate) 
    {
        switch (_worker_state)
        {
            case Worker_Idle:
                {
                	pthread_mutex_lock(&on_after_register_access_mutex);

                    while (!_new_command_ready)
                    {
                        pthread_cond_wait(
                           &on_after_register_access_cond,
                           &on_after_register_access_mutex); 
                    }
                     
                    //
                    // Make a local copy of the new command to execute.
                    //
                    command = _new_command;
                    _new_command_ready = false;
                    pthread_mutex_unlock(&on_after_register_access_mutex);

                    //
                    // Clear GO now that we've accepted the command.
                    //
                    _go = false;
                    update_RKCS();

                    //
                    // We will interrupt after completion of a command except
                    // in certain error cases.
                    //
                    do_interrupt = true;

                    //
                    // Move to the execution state to start executing the command.
                    //
                    _worker_state = Worker_Execute;
                }
                break;

            case Worker_Execute:
                switch(command.function)
                {
                    case Write:
                    case Read:
                    case Write_Check:
                        {
                            bool write = command.function == Write;
                            bool read_format = command.function == Read && command.format;
                            bool read = command.function == Read && !command.format;
                            bool write_check = command.function == Write_Check;

                            // We loop over the requested address range
                            // and submit DMA requests one at a time, waiting for
                            // their completion. 
                          
                            uint16_t sectorBuffer[256];
                            uint16_t checkBuffer[256];
 
                            uint32_t current_address = command.address;
                            int16_t current_count = -(int16_t)(get_register_dato_value(RKWC_reg));
                            bool abort = false;
                            while(current_count > 0 && !abort)
                            {
                                // If a new command has been written in the CS register, abandon
                                // this one ASAP.
                                if (_new_command_ready)
                                {
                                    DEBUG("Command canceled.");
                                    abort = true;
                                    continue;
                                }

                                // Validate that the current disk address is valid.
                                if (!validate_seek())
                                {
                                    // The above check set error bits accordingly.
                                    // Just abort the transfer now.
                                    // Set OVR if we've gone off the end of the disk.
                                    if (_rkda_cyl > 202)
                                    {
                                        _ovr = true;
                                        // update_RKER();
                                    }
                                    abort = true;
                                    continue;
                                }

                                //
                                // Clear the buffer.  This is only necessary because short writes
                                // and reads expect the rest of the sector to be filled with zeroes.
                                //
                                memset(sectorBuffer, 0, sizeof(sectorBuffer));
                                
                                if (read)
                                {
                                    // Doing a normal read from disk:  Grab the sector data and then
                                    // DMA it into memory.
                                    selected_drive()->read_sector(
                                        _rkda_cyl,
                                        _rkda_surface,
                                        _rkda_sector,
                                        sectorBuffer);
                                }
                                else if (read_format)
                                {
                                    // Doing a Read Format:  Read only the header word from the disk
                                    // and DMA that single word into memory.  
                                    // We don't actually read the header word from disk since we don't
                                    // store header data in the disk image; we just fake it up --
                                    // since we always seek correctly this is all that is required.
                                    //
                                    // The header is just the cylinder address, as in RKDA 05-12 (p. 3-9)   
                                    sectorBuffer[0] = (_rkda_cyl << 5);
                                }
                                else if (write_check)
                                {
                                    // Doing a Write Check:  Grab the sector data from the disk into
                                    // the check buffer.
                                    selected_drive()->read_sector(
                                        _rkda_cyl,
                                        _rkda_surface,
                                        _rkda_sector,
                                        checkBuffer); 
                                }

                                //
                                // Normal Read, or Write/Write Format: DMA the data to/from memory,
                                // etc.
                                // 
                                // build the DMA transfer request:
                                DMARequest request = { 0 };
                                request.address = current_address; 
                                request.count = (!read_format) ? 
                                    min(static_cast<int16_t>(256) , current_count) : 
                                    1;
                                request.write = !(write || write_check);  // Inverted sense from disk action
                                request.timeout = false;
                                request.buffer = sectorBuffer;
                                request.iba = command.iba;

                                // And actually do the transfer.   
                                dma_transfer(request);

                                // Check completion status -- if there was an error,
                                // we'll abort and set the appropriate flags.
                                if (request.timeout)
                                {
                                    _nxm = true; 
                                    _he = true;
                                    _err = true;
                                    // update_RKCS();
                                    // update_RKER(); 
                                    abort = true;
                                }
                                else
                                {
                                    if (write)
                                    {
                                        // Doing a write to disk:  Write the buffer DMA'd from
                                        // memory to the disk.
                                        selected_drive()->write_sector(
                                            _rkda_cyl,
                                            _rkda_surface,
                                            _rkda_sector,
                                            sectorBuffer);
                                    }
                                    else if (write_check)
                                    {
                                        // Compare check buffer with sector buffer, if there are
                                        // any discrepancies, set WCE and interrupt as necessary.
                                        for (int i = 0; i < request.count; i++)
                                        {
                                            if (sectorBuffer[i] != checkBuffer[i])
                                            {
                                                _wce = true;
                                                _err = true;
                                                // update_RKER();
                                                // update_RKCS();

                                                if (_sse)
                                                {
                                                    // Finish this transfer and abort.
                                                    abort = true;
                                                    break;
                                                }
                                            } 
                                        }

                                        if (_wce && _sse)
                                        {
                                            // Control action stops on error after this transfer
                                            //  if SSE (Stop on Soft Error) is set.
                                            abort = true; 
                                        } 
                                    }
                                    else  // Read
                                    {
                                        // After read complete, set RKDB to the last word
                                        // read (this satisfies ZRKK):
                                        set_register_dati_value(
                                            RKDB_reg, 
                                            sectorBuffer[request.count - 1],
                                            "RK11 READ");
                                    }
                                }

                                // Transfer completed.  Move to next and update registers.
                                current_count -= request.count;

                                set_register_dati_value(
                                    RKWC_reg,
                                    (uint16_t)(-current_count),
                                    __func__);

                                if (!command.iba)
                                {
                                    current_address += (request.count * 2);  // Byte address
                                    set_register_dati_value(
                                        RKBA_reg, 
                                        (uint16_t)current_address,
                                        __func__); 
                                    _mex = ((current_address) >> 16) & 0x3;
                                    // update_RKCS();    
                                }

                                // Move to next disk address
                                increment_RKDA();

                                // And go around, do it again. 
                            }

                            // timeout.wait_us(100);
                            DEBUG("R/W: Complete.");
                            _worker_state = Worker_Finish;
                        }
                        break;

                    case Read_Check:
                        //
                        // "...is identical to a normal Read function, except that no
                        //  NPRs occur.  Only the checksum is calculated and compared
                        //  with the checksum read from the disk drive... it must be
                        //  performed on a whole-sector basis only."
                        //  Since all data is error free in our emulation, the only 
                        //  thing we do here is increment RKDA the requisite number
                        //  of times and clear RKWC.
                        //
                        {
                            uint32_t current_address = command.address;
                            int16_t current_count = -(int16_t)(get_register_dato_value(RKWC_reg));
                            // TODO: could do all this without going through the motions. 
                            while (current_count > 0)
                            {
                                if (!validate_seek())
                                {
                                    // TODO: set RKER, etc.
                                    break; 
                                }

                                current_count -= 256;  // TODO: remove hardcoding.
                                if (!command.iba)
                                {
                                    current_address += (256 * 2);  // Byte address
                                    set_register_dati_value(
                                        RKWC_reg,
                                        (uint16_t)(-current_count),
                                        __func__);
                                    _mex = ((current_address) >> 16) & 0x3;
                                    // update_RKCS();
                                }

                                // Move to next disk address.
                                increment_RKDA();

                                // And go around, do it again.
                            }

                            _worker_state = Worker_Finish;
                        }   
                        break;

                    case Seek:
                        //
                        // Per ZRKK, if IDE is set, an interrupt is raised at the beginning
                        // of the seek, and another is raised when the seek is complete.
                        //
                        if (validate_seek())
                        {
                            // Per ZRKK, ID bits in RKDS get cleared at the beginning of a seek.
                            _id = 0;   
                            update_RKDS();

                            // Start the seek; this will complete asynchronously. 
                            selected_drive()->seek(
                                _rkda_cyl);
                        }
                        else
                        {
                            do_interrupt = false;
                        }
                        _worker_state = Worker_Finish;
                        break;

                    case Drive_Reset:
                        if (check_drive_present())
                        {
                            selected_drive()->drive_reset();
                        }
                        else
                        {
                            do_interrupt = false;
                        }
                        _worker_state = Worker_Finish;
                        break;

                    default:
                        INFO("Unhandled function %d.", command.function);
                        // For now we just move to the Finish state so that future
                        // commands can execute.
                        _worker_state = Worker_Finish;
                        break;
                }
                break;

             case Worker_Finish:
                // Set RDY, interrupt (if enabled) and go back to the Idle state.
                // We do this so that the update of ER, CS regs and the interrupt
                // are atomic w.r.t. RKCS access (diagnostic code polls CS and will
                // start a new operation immediately, lowering RDY before we invoke
                // the interrupt, causing behavior diagnostics do not expect.) 
                pthread_mutex_lock(&on_after_register_access_mutex);
                    _rdy = true;
                    update_RKER();
                    update_RKCS();

                    // Interrupt unless specified not to.
                    if (do_interrupt)
                    {
                        assert(_rdy); 
                        invoke_interrupt();
                    }
                pthread_mutex_unlock(&on_after_register_access_mutex);
                _worker_state = Worker_Idle;
                break; 
        }
    }
}

bool rk11_c::validate_seek(void)
{
    bool error = false;

    if (_rkda_sector > 11)
    {
        _nxs = true;
        error = true;
    }
  
    if (_rkda_cyl > 202)
    {
        _nxc = true;
        error = true;
    }

    if (!check_drive_present())
    {
        _nxd = true;
        error = true;
    }

    if (error)
    {
        // Update the RKER register 
        // update_RKER();
        
        // Set the Control/Status register HE and ERR registers.
        _he = true;
        _err = true;
        // update_RKCS();
 
        // Do not interrupt here, caller will do so based on
        // our return value.
        // invoke_interrupt(); 
    }

    return !error;
}

void rk11_c::increment_RKDA()
{
    _rkda_sector++;

    if (_rkda_sector > 11)
    {
        _rkda_sector = 0;
        _rkda_surface++;

        if (_rkda_surface > 1)
        {
            _rkda_surface = 0;
            _rkda_cyl++;
        }
    }
    update_RKDA();
}

//
// process DATI/DATO access to the RK11's "active" registers.
// !! called asynchronuously by PRU, with SSYN/RPLY asserted and blocking QBUS/UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void rk11_c::on_after_register_access(
    qunibusdevice_register_t *device_reg,
    uint8_t unibus_control)
{
    UNUSED(unibus_control);

    // The RK11 has only one "active" register, RKCS.
    // When "GO" bit is set, kick off an operation and clear the
    // RDY bit.
    switch(device_reg->index)
    {

        case 2:     // RKCS
            _go = !!(RKCS_reg->active_dato_flipflops & 0x1);
            _function = (RKCS_reg->active_dato_flipflops & 0xe) >> 1;
            _mex = (RKCS_reg->active_dato_flipflops & 0x30) >> 4;
            _ide = !!(RKCS_reg->active_dato_flipflops & 0x40);
            _sse = !!(RKCS_reg->active_dato_flipflops & 0x100);
            _exb = !!(RKCS_reg->active_dato_flipflops & 0x200);
            _fmt = !!(RKCS_reg->active_dato_flipflops & 0x400);
            _iba = !!(RKCS_reg->active_dato_flipflops & 0x800);
 
            //
            // GO bit is set:
            // "Causes the control to carry out the function contained in bits 01 through 03 of the
            // "RKCS (Function).  Remains set until the control actually begins to respond to GO, which
            // may take from 1us to 3.3ms, depending on the current operation of the selected disk drive..."
            // TODO: what happens if RDY is low and GO is high?
            if (_go)
            {
                bool error = false;

                //
                // Check for PGE (Programming Error): use of FMT with a function other than Read or Write.
                //
                if (_fmt && (_function != Read && _function != Write && _function != Control_Reset))
                {
                   _pge = true;
                   _he = true;
                   _err = true;
 
                   // GO gets cleared
                   _go = false;
                   // update_RKER();
                   // update_RKCS();
                   // error = true; 
                }
                else
                {
                switch(_function)
                {
                    case Control_Reset:
                        //
                        // "The Control Reset function initializes all internal registers and flip-flops
                        //  and clears all the bits of the seven programmable registers except RKCS 07 (RDY)
                        //  which it sets, and RKDS 01 through 11, which are not affected.
                        //
                        reset_controller();
                        break;

                    case Write_Lock:
                        //
                        // "Write-protects a selected disk drive until the condition is overridden by the
                        //  operation of the corresponding WT PROT switch on the disk drive..."
                        //
                        selected_drive()->set_write_protect(true);
                        _scp = false;
                        // update_RKCS();
                        break;

                    default:
                        // All other commands take significant time and happen on the worker thread.
                        // First check: 
                        // If this is not a Drive Reset command and the drive is not ready or has taken
                        // a fault, we will abort, set the appropriate error bits and interrupt.
                        //
                        if (_function != Drive_Reset && !check_drive_ready())
                        {
                            _dre = true;
                            // update_RKER();
                            _he = true;
                            _err = true;
                            _scp = false;
                            _go = false;
                            // update_RKCS();
                        //    INFO("setting DRE, fn %o dr rdy %d rws rdy %d", _function,
                        //        selected_drive()->get_drive_ready(),
                        //        selected_drive()->get_rws_ready());

                            // invoke_interrupt();
                            error = true;
                            break;
                        }
 
                        // If this is an attempt to address a nonexistent (not present or not
                        // loaded) drive, this triggers an NXD error.
                        if (!check_drive_present())
                        {
                            _nxd = true;
                            _he = true;
                            _err = true;
                            _scp = false;
                            _go = false;
                            // update_RKCS();
                            // update_RKER();
 
                            // invoke_interrupt();
                            error = true;
                            break;
                        } 
        
                        // Clear RDY, SCP bits:
                        pthread_mutex_lock(&on_after_register_access_mutex); 
                        _rdy = false;
                        _scp = false;
                        // update_RKCS();

                        //
                        // Stow command data for this operation so that if RKCS gets changed
                        // mid-op weird things won't happen.
                        //
                        _new_command.address = get_register_dato_value(RKBA_reg) | (_mex << 16);
                        _new_command.drive = selected_drive();
                        _new_command.function = _function;
                        _new_command.interrupt = _ide;
                        _new_command.stop_on_soft_error = _sse;
                        _new_command.format = _fmt;
                        _new_command.iba = _iba;
                        _new_command_ready = true;

                        //
                        // Wake the worker.
                        //
                        pthread_cond_signal(&on_after_register_access_cond);
                        pthread_mutex_unlock(&on_after_register_access_mutex);
                        break;
                }
                }

                update_RKER();
                update_RKCS();

                if (error)
                {
                    assert(_rdy);
                    invoke_interrupt();
                }
            }
            else
            {
                // Stow the value in the register.
                update_RKCS();
 
                // If IDE is set (interrupt on done) and RDY is set, we will interrupt.
                if (_rdy)
                {
                    invoke_interrupt();
                }
            }
            break;
      
        case 5:    // RKDA
            //
            // Writes are only accepted when RDY is high.
            //
            if (_rdy)
            {
                uint32_t oldDrive = _rkda_drive;

                _rkda_sector = (RKDA_reg->active_dato_flipflops & 0xf);
                _rkda_surface = (RKDA_reg->active_dato_flipflops & 0x10) >> 4;
                _rkda_cyl = (RKDA_reg->active_dato_flipflops & 0x1fe0) >> 5;
                _rkda_drive = (RKDA_reg->active_dato_flipflops & 0xe000) >> 13;
                update_RKDA();

                // Pick up new drive's status if drive ID changed.
                if (_rkda_drive != oldDrive)
                {
                    update_RKDS();
                }
            }
            break;

        default:
            // This should never happen.
	    assert(false);
            break;
    }   
    
}

bool rk11_c::check_drive_ready(void)
{
   if (!selected_drive()->get_drive_ready() || 
        selected_drive()->get_seek_incomplete() ||
        selected_drive()->get_drive_unsafe() ||
        selected_drive()->get_drive_power_low())
   {
       return false;
   }
   else
   { 
       return true;
   }
}

bool rk11_c::check_drive_present(void)
{
    return (selected_drive()->get_drive_ready());  
}

void rk11_c::on_drive_status_changed(storagedrive_c *drive)
{
    // only update if drive reporting a change
    // is the same as currently selected drive in RKDA.
    if (drive->unitno.value == selected_drive()->unitno.value)
    {
        update_RKDS();
    }

    // If interrupts are enabled and the drive has completed a seek,
    // interrupt now.  Note that the call to get_search_complete() has
    // the side effect (eww) of resetting the drive's SCP bit, so we do it
    // first (so it always gets cleared even if we're not interrupting.)
    if (dynamic_cast<rk05_c*>(drive)->get_search_complete() &&
        _ide)
    {
        // Set SCP to indicate that this interrupt was due to a previous
        // Seek or Drive Reset function.
        _scp = true;
        _id = drive->unitno.value;
        update_RKDS();
        update_RKCS();
        invoke_interrupt();
    }
}

void rk11_c::update_RKER(void)
{
    unsigned short new_RKER =
        (_wce ? 0x0001 : 0x0000) |
        (_cse ? 0x0002 : 0x0000) |
        (_nxs ? 0x0020 : 0x0000) |
        (_nxc ? 0x0040 : 0x0000) |
        (_nxd ? 0x0080 : 0x0000) |
        (_te  ? 0x0100 : 0x0000) |
        (_dlt ? 0x0200 : 0x0000) |
        (_nxm ? 0x0400 : 0x0000) |
        (_pge ? 0x0800 : 0x0000) |
        (_ske ? 0x1000 : 0x0000) |
        (_wlo ? 0x2000 : 0x0000) |
        (_ovr ? 0x4000 : 0x0000) |
        (_dre ? 0x8000 : 0x0000);

    set_register_dati_value(
        RKER_reg,
        new_RKER,
        "update_RKER");
}

void rk11_c::update_RKCS(void)
{
    unsigned short new_RKCS =
        (_go ? 0x0001 : 0x0000) |
        (_function << 1) |
        (_mex << 4) |
        (_ide ? 0x0040 : 0x0000) |
        (_rdy ? 0x0080 : 0x0000) | 
        (_sse ? 0x0100 : 0x0000) |
        (_exb ? 0x0200 : 0x0000) |
        (_fmt ? 0x0400 : 0x0000) |
        (_iba ?  0x0800 : 0x0000) |
        (_scp ? 0x2000 : 0x0000) |
        (_he ?  0x4000 : 0x0000) |
        (_err ? 0x8000 : 0x0000); 

    set_register_dati_value(
        RKCS_reg,
        new_RKCS,
        "update_RKCS");
}

void rk11_c::update_RKDS(void)
{
    rk05_c* drive = selected_drive();
    uint32_t sectorCounter = drive->get_sector_counter();
    bool sceqsa = sectorCounter == _rkda_sector;
    bool wps = drive->get_write_protect();
    bool rwsrdy = drive->get_rws_ready();
    bool dry = drive->get_drive_ready();
    bool sok = drive->get_sector_counter_ok();
    bool sin = drive->get_seek_incomplete();
    bool dru = drive->get_drive_unsafe();
    bool rk05 = drive->get_rk05_disk_online();
    bool dpl = drive->get_drive_power_low();

    unsigned short new_RKDS =
        sectorCounter |
        (sceqsa  ? 0x0010 : 0x0000) |
        (wps    ? 0x0020 : 0x0000) |
        (rwsrdy ? 0x0040 : 0x0000) |
        (dry    ? 0x0080 : 0x0000) |
        (sok    ? 0x0100 : 0x0000) |
        (sin    ? 0x0200 : 0x0000) |
        (dru    ? 0x0400 : 0x0000) |
        (rk05   ? 0x0800 : 0x0000) |
        (dpl    ? 0x1000 : 0x0000) |
        (_id << 13); 

    set_register_dati_value(
        RKDS_reg,
        new_RKDS,
        "update_RKDS");
}

void rk11_c::update_RKDA(void)
{
   unsigned short new_RKDA =
        _rkda_sector |
        (_rkda_surface << 4) |
        (_rkda_cyl << 5) |
        (_rkda_drive << 13);
 
    set_register_dati_value(
        RKDA_reg,
        new_RKDA,
        "update_RKDA");
}

void rk11_c::invoke_interrupt(void)
{
    //
    // Invoke an interrupt if the IDE bit of RKCS is set.
    //
    if (_ide)
    {
        qunibusadapter->INTR(intr_request, NULL, 0); // todo: link to interupt register
    }
}

void rk11_c::reset_controller(void)
{
    // This will reset the DATI values to their defaults.
    // We then need to reset our copy of the values to correspond.
    reset_unibus_registers();
 
    //
    // Clear all RKER bits.
    //
    _wce = false;
    _cse = false;
    _nxs = false;
    _nxc = false;
    _nxd = false;
    _te = false;
    _dlt = false;
    _nxm = false;
    _pge = false;
    _ske = false;
    _wlo = false;
    _ovr = false;
    _dre = false;
    update_RKER();

    //
    // Clear all RKCS bits except RDY.
    //
    _go = false;
    _function = 0;
    _mex = 0;
    _ide = false;
    _rdy = true;
    _sse = false;
    _exb = false;
    _fmt = false;
    _iba = false;
    _scp = false;
    _he = false;
    _err = false;
    update_RKCS();

    //
    // Clear all RKDA bits.
    //
    _rkda_sector = 0;
    _rkda_surface = 0;
    _rkda_cyl = 0;
    _rkda_drive = 0;
    update_RKDA();

    //
    // Update RKDS bits to match the newly selected drive (drive 0)
    //
    _id = 0;
    update_RKDS();

    //
    // Clear RKWC
    //
    set_register_dati_value(
        RKWC_reg,
        0,
        "reset_controller");

    //
    // Clear RKBA
    //
    set_register_dati_value(
        RKBA_reg,
        0,
        "reset_controller");
}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void rk11_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
    storagecontroller_c::on_power_changed(aclo_edge, dclo_edge);

    if (dclo_edge == SIGNAL_EDGE_RAISING) 
    { 
        // power-on defaults
        reset_controller();
    }
}

// QBUS/UNIBUS INIT: clear all registers
void rk11_c::on_init_changed(void) 
{
    // write all registers to "reset-values"
    if (init_asserted) 
    {
        reset_controller();
    }

    storagecontroller_c::on_init_changed();
}

rk05_c* rk11_c::selected_drive(void)
{
    return dynamic_cast<rk05_c*>(storagedrives[_rkda_drive]);
}
