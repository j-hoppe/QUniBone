/* rx0102cpu.cpp: implementation of RX01/RX02 floppy disk drive, attached to RX11/211 controller

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


 5-jan-2020	JH      entered beta phase
 */

#include <assert.h>

#include <array>

using namespace std;

#include "logger.hpp"
#include "timeout.hpp"
#include "utils.hpp"
#include "rx0102ucpu.hpp"
#include "rx0102drive.hpp"
#include "rx11.hpp"


// function codes to uCPU
#define RX11_CMD_FILL_BUFFER 	0
#define RX11_CMD_EMPTY_BUFFER	1
#define RX11_CMD_WRITE_SECTOR	2
#define RX11_CMD_READ_SECTOR	3
#define RX11_CMD_SET_MEDIA_DENSITY 4 // RX211 only
#define RX11_CMD_READ_STATUS	5
#define RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA	6
#define RX11_CMD_READ_ERROR_REGISTER 7



// link uCPU to its RX controller
RX0102uCPU_c::RX0102uCPU_c(RX11_c *controller): device_c(), controller(controller) {
    // static config
    name.value = "rxbox";
    type_name.value = "RX0102uCPU";
    log_label = "rxcpu";

    is_RX02 = false ;

    // imit
    power_switch.set(0) ;
    set_powerless() ;
}

RX0102uCPU_c::~RX0102uCPU_c() {}

// set signals to RX11 in "powerless" state
void RX0102uCPU_c::set_powerless(void) {
    // signal controller an ERROR., ERROR_L signal pulled low by powerlessRX drive logic.
    signal_done = true ;
    signal_error = true ;
    signal_transfer_request = false ;
}

const char * RX0102uCPU_c::function_code_text(unsigned function_code) {
    switch(function_code) {
    case RX11_CMD_FILL_BUFFER:
        return "FILL_BUFFER" ;
    case RX11_CMD_EMPTY_BUFFER:
        return "EMPTY_BUFFER" ;
    case RX11_CMD_WRITE_SECTOR:
        return "WRITE_SECTOR" ;
    case RX11_CMD_READ_SECTOR:
        return "READ_SECTOR" ;
    case RX11_CMD_SET_MEDIA_DENSITY:
        return "SET_MEDIA_DENSITY" ;
    case RX11_CMD_READ_STATUS:
        return "READ_STATUS" ;
    case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
        return "WRITE_SECTOR_WITH_DELETED_DATA" ;
    case RX11_CMD_READ_ERROR_REGISTER:
        return "READ_ERROR_REGISTER" ;
    default:
        return "???" ;
    }
}


/*** Program flow and steps for the RX01/02 internal uP ***/
const char * RX0102uCPU_c::step_text(enum step_e step) {
    // still no enum->string in C++ ??
    switch (step) {
    case step_none:
        return "none" ;
    case step_transfer_buffer_write:
        return "transfer_buffer_write" ;
    case step_transfer_buffer_read:
        return "transfer_buffer_read" ;
    case step_seek:
        return "seek" ;
    case step_head_settle:
        return "head_settle" ;
    case step_sector_write:
        return "sector_write" ;
    case step_sector_read:
        return "sector_read" ;
    case step_init_done:
        return "step_init_done" ;
    case step_done_read_error:
        return "done_read_error" ;
    case step_done:
        return "done" ;
    case step_error:
        return "error" ;
    default:
        return "???" ;
    } ;
}


enum RX0102uCPU_c::step_e RX0102uCPU_c::step_current() {
    if (program_counter >= program_steps.size())
        return step_none ;
    else
        return program_steps.at(program_counter) ;
}

// advance one step, if end not reached
void RX0102uCPU_c::step_next(void) {
    if (step_current() != step_none)
        program_counter++ ;
}

void RX0102uCPU_c::program_clear(void) {
    program_steps.clear() ;
    program_counter = 0 ;
}

// program counter after last step
bool RX0102uCPU_c::program_complete(void) {
    return (step_current() == step_none) ;
}


// execute program from current program_counter
//  signal worker() to start
void RX0102uCPU_c::program_start(void) {
    DEBUG("program_start()") ;
    pthread_mutex_lock(&on_worker_mutex);
    pthread_cond_signal(&on_worker_cond);
    pthread_mutex_unlock(&on_worker_mutex);
}

// execute a program step.
void RX0102uCPU_c::step_execute(enum step_e step) {
    timeout_c timeout ;

    if (step == step_none)
        return ;

    // rxes is updated only by some steps

    DEBUG("step_execute() step #%d = \"%s\".", program_counter,
          step_text(step)) ;

    switch(step) {
    case step_none:
        break ; // not reached
    case step_transfer_buffer_write: // controller fills buffer before function execution ()
        // transfer_byte_count at program setup
        transfer_byte_idx = 0 ;
        signal_transfer_request = true ; // may write first byte now
        controller->update_status("step_execute(step_transfer_buffer_write) -> update_status") ; // show TR bit in RXCS
        // wait for rxdb_after_write() to signal transfer completion
        pthread_cond_wait(&on_worker_cond, &on_worker_mutex);
//        logger->debug_hexdump(this, "transfer_buffer after write", (uint8_t *) transfer_buffer, sizeof(transfer_buffer), NULL);
        break ;
    case step_transfer_buffer_read: // controller reads back buffer (only "empty")
//        logger->debug_hexdump(this, "transfer_buffer before read", (uint8_t *) transfer_buffer, sizeof(transfer_buffer), NULL);
        // transfer_byte_count at program setup
        transfer_byte_idx = 0 ;
        // put first word into RXDB
        switch (signal_function_code) {
        case RX11_CMD_EMPTY_BUFFER:
            signal_transfer_request = true ; // when RXDB is valid
            rxdb = sector_buffer[0] ; // 	1st byte readable
            DEBUG("sector_buffer[0] = %06o", rxdb) ;
            break ;
        }
        controller->update_status("step_execute(step_transfer_buffer_read) -> update_status") ; // show TR bit in RXCS
        // wait for rxdb_after_read() to signal transfer completion
        pthread_cond_wait(&on_worker_cond, &on_worker_mutex);
        break ;
    case step_seek: // head movement
        pgmstep_seek() ;
        // set_rxes() ; // no access to media
        break ;
    case step_head_settle: // if head has moved, it needs 100ms  to stabilize
        // headsettle_time_ms set by seek()
        timeout.wait_ms(headsettle_time_ms / emulation_speed.value) ;
        break ;
    case step_sector_write: { // sector buffer to disk surface
        // transferbuffer must contain track,sector[]
        signal_error = !selected_drive()->sector_write(sector_buffer, deleted_data_mark,
                       rxta, rxsa) ;
        set_rxes() ;
    }
    break ;
    case step_sector_read: { // disk surface to sector buffer
        signal_error = !selected_drive()->sector_read(sector_buffer, &deleted_data_mark,
                       rxta, rxsa) ;
        set_rxes() ;
    }
    break ;
    case step_done_read_error: // only case where rxdb is *not* rxes
        initializing = false ; // also called at end of INIT
        signal_done = true ;
        signal_transfer_request = false ;
        rxdb = rxer ;
        controller->update_status("step_execute(step_done_read_error) -> update_status") ; // may trigger interrupt
        break ;
    case step_init_done: // idle between functions
        initializing = false ;
        signal_done = true ;
        signal_transfer_request = false ;
        set_rxes() ;
        rxes |= BIT(2) ; // INIT DONE only here
        if (selected_drive()->check_ready())
            rxes |= BIT(7) ; // drive ready
        rxdb = rxes ;
        controller->update_status("step_execute(step_init_done) -> update_status") ; // may trigger interrupt
        break ;
    case step_done: // idle between functions
        initializing = false ; // also called at end of INIT
        signal_done = true ;
        signal_transfer_request = false ;
        rxdb = set_rxes() ;
        // CZRXBF0 RX11 INTERFACE TEST
        // ERADR  FAST   FAPT			GOOD   BAD	   PASS
        // 003736 003612 003612 000204 000000 000040	  0
        // ZRXB; "done found true after function Read Status": ARM/PRU too fast for PDP-11 CPU.
        // the interface RX11/CPU is serial, with a 400ns "SHIFT" clock
        // to transfer a command to the PCU and get back the RXES result, there's always a delay
        timeout.wait_us(200) ; // long, for emulated CPU

        // timeout.wait_ns(400*16) ;

        controller->update_status("step_execute(step_done) -> update_status") ; // may trigger interrupt
        break ;
    case step_error: // error processing
        initializing = false ; // also called at end of INIT
        program_clear() ; // abort
        rxdb = set_rxes() ;
        signal_done = true ;
        signal_error = true ;
        signal_transfer_request = false ;
        controller->update_status("step_execute(step_error) -> update_status") ; // may trigger interrupt
        break ;

    }
}


// OR standard flags into RXES register
// "init done" and "drive ready" not set here, depends on function
uint16_t  RX0102uCPU_c::set_rxes(void) {

    if (deleted_data_mark)
        rxes |= BIT(6) ;

    if (is_RX02) {
        // density error:
        bool double_density = selected_drive()->get_density() ;

        if (double_density != signal_function_density)
            rxes |= BIT(4) ; // Density error

        if (double_density)
            rxes |= BIT(5) ;

        // UNIT select: only RX02, erroneously documented for RX11 too?
        if (signal_selected_drive_unitno)
            rxes |= BIT(8) ; // unit #1 select
    }

    DEBUG("set_rxes(): rxes := %06o", rxes) ;
    return rxes ;
}

// generate content of RXER register depending on drive state
// None of the medai and format related errors can occure here
uint16_t  RX0102uCPU_c::set_rxer(void) {
    rxer = 0 ;
    if (selected_drive()->error_illegal_track)
        rxer = 0040 ;
    else if (selected_drive()->error_illegal_sector)
        rxer = 0070 ;
    DEBUG("set_rxer(): rxer := %06o", rxer) ;
    return rxer ;
}

#ifdef NEEDED
// set error code from drive into RXERR
void RX0102uCPU_c::set_drive_error(uint16_t error_code) {
    const char *txt = NULL ;
    switch(error_code) {
    case 0010:
        txt = "Drive 0 failed to see home on Initialize" ;
        break ;
    case 0020:
        txt = "Drive 1 failed to see home on Initialize" ;
        break ;
    case 0030:
        txt = "Found home when stepping out 10 tracks from INIT" ;
        break ;
    case 0040:
        txt = "Tried to access track greater than 77" ;
        break ;
    case 0050:
        txt = "Home was found before desired track as reached" ;
        break ;
    case 0060:
        txt = "Self-diagnostic error" ;
        break ;
    case 0070:
        txt = "Desired sector could not be found after looking at 52 sectors (2 revolutions)" ;
        break ;
    case 0110:
        txt = "More than 40us and no SEP clock seen." ;
        break ;
    case 0120:
        txt = "A preamble could not be found" ;
        break ;
    case 0130:
        txt = "Preamble found but no I/O mark found within allowable time span" ;
        break ;
    case 0140:
        txt = "CRC error on what we thought was a header" ;
        break ;
    case 0150:
        txt = "The header track address of a good header does not compare with the desired track" ;
        break ;
    case 0160:
        txt = "Too many tries for an IDAM" ;
        break ;
    case 0170:
        txt = "Data AM not found in alloted time" ;
        break ;
    case 0200:
        txt = "CRC error on reaching the sector from disk" ;
        break ;
    case 0210:
        txt = "All parity errors" ;
        break ;
    default:
        txt = "Undefined error" ;
    }
    INFO("Drive error %04o = %s", error_code, txt);
}
#endif



// seek track, part of read/write sector.
void RX0102uCPU_c::pgmstep_seek(void) {
    timeout_c timeout ;
    RX0102drive_c *drive = selected_drive() ;
    DEBUG("pgmstep_seek()") ;
    uint8_t	track_address = rxta ;

    // periodically executed. false = ready
    unsigned calcperiod_ms = 10; // waits per loop
    // head can pass this much tracks per loop
    unsigned trackmove_increment = 2 * emulation_speed.value; // 10/2 =5ms per track, this much per loop

    // parameter chek already done
    assert(track_address < selected_drive()->cylinder_count) ;

    // nothing todo if already on track
    headsettle_time_ms = 0 ;
    if (track_address > drive->cylinder) {
        headsettle_time_ms = 100 ;
        DEBUG("drive %d seeking outward, cyl = %d", drive->unitno.value, drive->cylinder);
        drive->cylinder += trackmove_increment;
        if (drive->cylinder >= track_address) {
            // seek head outward finished
            // proportionally reduced seek time
            drive->cylinder = track_address;
            DEBUG("drive %d seek outwards complete, cyl = %d", drive->unitno.value, drive->cylinder);
            //drive->change_state(RX0102_STATE_lock_on);
        } else
            timeout.wait_ms(calcperiod_ms);
    } else {
        // seek head inwards
        headsettle_time_ms = 100 ;
        if ((drive->cylinder - track_address) <= trackmove_increment) {
            drive->cylinder = track_address;
            DEBUG("drive %d seek inwards complete, cyl = %d", drive->unitno.value, drive->cylinder);
            //change_state(RX0102_STATE_lock_on);
        } else {
            DEBUG("drive %d seeking inwards, cyl = %d", drive->unitno.value, drive->cylinder);
            drive->cylinder -= trackmove_increment;
            timeout.wait_ms(calcperiod_ms);
        }
    }

}


// Notify read access to RXDB by controller
// puts next buffer cell into RXDB.
// only for block read when state == state_transfer_read_result;
void RX0102uCPU_c::rxdb_after_read(void) {
    DEBUG("rxdb_after_read() in function %s, word %d/%d", function_code_text(signal_function_code),transfer_byte_idx,transfer_byte_count) ;

    if (transfer_byte_idx < transfer_byte_count && signal_function_code == RX11_CMD_EMPTY_BUFFER) {
        // read data from buffer, "empty"
        if (transfer_byte_idx+1 < transfer_byte_count) {
            // put next buffer byte into RXDB
            rxdb = sector_buffer[++transfer_byte_idx] ; // read 8bit, return 16 bit
            DEBUG("sector_buffer[%d] = %06o",transfer_byte_idx, rxdb) ;
            controller->update_status("rxdb_after_read() rxdb=buffer byte -> update_status") ; // new RXDB, new TR
        } else {
            // last byte transmitted: continue halted program
            signal_transfer_request = false ;
            transfer_byte_idx++ ; // move to "invalid"
            controller->update_status("rxdb_after_read() -> update_status") ; // new RXDB, new TR, before INTR
            pthread_mutex_lock(&on_worker_mutex);
            pthread_cond_signal(&on_worker_cond);
            pthread_mutex_unlock(&on_worker_mutex);
            // last word written: "fill" and "empty" programs executes DONE
            // last buffer word return to QBUS DATI, Interrupt raised simultaneously and pending
            // next RXDB is RXES together with INTR, see step_done
        }
    }
}

// Write access to RXDB by controller

void  RX0102uCPU_c::rxdb_after_write(uint16_t w) {
    bool complete = false ; // true when all requested words transfered.
    DEBUG("rxdb_after_write() function %s, word %d/%d", function_code_text(signal_function_code),transfer_byte_idx, transfer_byte_count) ;

    if (program_complete()) {
        // RXDB is read/write when not executing a command
        rxdb = w ;
        return ;
    }

    if (transfer_byte_idx >= transfer_byte_count)
        // not expecting any more data
        return ;

    switch(signal_function_code) {
    case RX11_CMD_FILL_BUFFER:
        // is expecting data
        assert(transfer_byte_idx < transfer_byte_count) ; // else state flips to "done"
        sector_buffer[transfer_byte_idx++] = w & 0xff ;
        if (transfer_byte_idx >= transfer_byte_count)
            complete = true ;
        break ;
    case RX11_CMD_READ_SECTOR:
    case RX11_CMD_WRITE_SECTOR:
    case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
        // accept sector + track address
        if (transfer_byte_idx == 0) {
            rxsa = w & 037; // bit 7-5 always 0, 15-8 don't care
            if (rxsa < 1 || rxsa > selected_drive()->sector_count) {
                signal_error = true ;
                rxer = 0070 ; // "Can't find sector"
            }
        } else if (transfer_byte_idx == 1) {
            rxta = w & 0177 ; // bit 7 always 0, 15-8 don't care
            if (rxta >= selected_drive()->cylinder_count) {
                signal_error = true ;
                rxer = 0040 ; // "Can't find track"
            }
            // even if sector is invalid, tr must be transfered.
            complete = true ;
        }
        transfer_byte_idx++ ;
        break ;
    }
    if (complete) {
        signal_transfer_request = false ;
        controller->update_status("rxdb_after_write() complete -> update_status") ; // new RXDB, new TR
        // transfer complete: continue halted program
        pthread_mutex_lock(&on_worker_mutex);
        pthread_cond_signal(&on_worker_cond);
        pthread_mutex_unlock(&on_worker_mutex);
        // last word written: "fill" and "empty" programs executes DONE
        // other functions proceed
        // last buffer word return to QBUS DATI, Interrupt raised simultaneously and pending
    } else
        controller->update_status("rxdb_after_write() incomplete -> update_status") ; // new RXDB
}



// return false, if illegal parameter value.
// verify "new_value", must output error messages
bool RX0102uCPU_c::on_param_changed(parameter_c *param) {
    DEBUG("on_param_changed()") ;
    if (param == &enabled) {
        if (!enabled.new_value) {
            // flip OFF power switch by disable
            // must be "powered"-on by caller or user after enable
            power_switch.value = false;
            set_powerless() ;
//            controller->update_status("on_param_changed(enabled) -> update_status") ;

        }
        // forward "enabled" to drives, are in same box
        for (unsigned i=0 ; i < drives.size() ; i++) {
            RX0102drive_c *drive = drives[i] ;
            drive->enabled.set(enabled.new_value) ;
        }
        controller->update_status("on_param_changed(enabled) -> update_status") ;

    }  else if (param == &power_switch) {
        if (!power_switch.new_value) {
            // switch OFF by user
            set_powerless() ;
            controller->update_status("on_param_changed(power_switch) -> update_status") ;
        } else {
            // power-on reset sequence
            init() ;
        }
    }
    return device_c::on_param_changed(param); // more actions: worker() cntrl for enable

}


// set logic type and type of attached drives
void RX0102uCPU_c::set_RX02(bool is_RX02) {
    this->is_RX02 = is_RX02 ;
    for (unsigned i=0 ; i < drives.size() ; i++) {
        RX0102drive_c *drive = drives[i] ;
        if (! is_RX02) {
            drive->density_name.set("SD") ;
            drive->density_name.readonly = true ; // RX=01 only SD
            drive->type_name.set("RX01") ;
        } else {
            //	drive->density_name.set("SD") ;
            drive->density_name.readonly = false; // RX02 SD or DD
            drive->type_name.set("RX02") ;
        }
    }
}


void RX0102uCPU_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
    // RX drive box has own power supply, no action here
    UNUSED(aclo_edge) ;
    UNUSED(dclo_edge) ;
}

void RX0102uCPU_c::on_init_changed(void) {
    // handled by RX11 controller and forwarded to this
}

// called asynchronically by disk drive on image load: "door close", "floppy insert"
// if it interrupts a program, it like a wild floppy change:
// do an "illegal sector header error" or the like.
void RX0102uCPU_c::on_drive_state_changed(RX0102drive_c *drive) {
    // forward "drive ready" to RXES
    if (drive == selected_drive()) {
        controller->update_status("on_drive_state_changed() -> update_status") ;
    }
}


/***** Function initiated by RX* controller ****/

// called by on_register_access
// init state, seek track 0 of drive 1
// read sector 1 of track 1 of drive 0(?)
void RX0102uCPU_c::init() {
    DEBUG("init()") ;

    if (!power_switch.new_value) // else no init() in on_param_change
        return ; // powered off

    signal_done = false ;
    signal_error = false ;
    signal_transfer_request = false ;
    initializing = true ;
    rxdb = 0 ;
    rxes = 0 ;
    rxer = 0 ;

    // boot drive 0, drive 1 also homed

    // no "home" delay
    drives[0]->cylinder = 0 ;
    drives[1]->cylinder = 0 ;

    // generate "read sector", set transfer buffer as if RX11 issued a "read sector"
    rxta = 1 ; // track
    rxsa = 1 ; // sector
    transfer_byte_count = 2 ;
    signal_selected_drive_unitno = 0 ;

    // setup sequence
    pthread_mutex_lock(&on_worker_mutex);
    program_clear() ; // aborts worker()
    program_steps.push_back(step_seek) ;
    program_steps.push_back(step_sector_read) ;
    program_steps.push_back(step_init_done) ;

    controller->update_status("init() -> update_status") ;

    // wakeup worker, start program
    pthread_cond_signal(&on_worker_cond);
    pthread_mutex_unlock(&on_worker_mutex);

}


void RX0102uCPU_c::go() { // execute function_code
    // program starts when transfer buffer filled
    DEBUG("go(), function=%d=%s", signal_function_code, function_code_text(signal_function_code)) ;

    if (!power_switch.new_value)
        return ; // powered off

    pthread_mutex_lock(&on_worker_mutex);

    signal_done = false ;
    signal_error = false ;
    signal_transfer_request = false ;
    deleted_data_mark = false ;
    transfer_byte_count = 0 ; // default: no data input expectedepcted
    rxes = 0 ;

    program_clear() ;

    switch(signal_function_code) {

    case RX11_CMD_FILL_BUFFER:
        rxer = 0 ;
        transfer_byte_count = 128 ; // buffer
        program_steps.push_back(step_transfer_buffer_write) ; // start by data
        program_steps.push_back(step_done) ;
        break ;
        /*cmd_fill_buffer2: // RX211
            step_transfer_buffer_write(2) receive WC,BA
            step_switch_to_worker // now blocking
            step_DMA_read // read memory to trasnfer buffer
            step_done
        	break ;
        */
    case RX11_CMD_EMPTY_BUFFER:
        rxer = 0 ;
        transfer_byte_count = 128 ; // buffer
        program_steps.push_back(step_transfer_buffer_read) ;
        program_steps.push_back(step_done) ;
        break ;
        /*
        cmd_empty_buffer2: // RX211
            step_transfer_buffer_write(2) receive WC,BA
            step_sector_to_buffer
            step_switch_to_worker // now blocking
            step_DMA_write // write transfer buffer to memory
            step_done
        	break ;
        */
    case RX11_CMD_READ_SECTOR:
        rxer = 0 ;
        transfer_byte_count = 2 ; // sector&track
        program_steps.push_back(step_transfer_buffer_write) ; // start by disk address
        program_steps.push_back(step_seek) ;
        program_steps.push_back(step_sector_read) ;
        program_steps.push_back(step_done) ;
        break ;
    case RX11_CMD_WRITE_SECTOR:
    case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
        rxer = 0 ;
        deleted_data_mark = (signal_function_code == RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA) ;
        transfer_byte_count = 2 ; // sector&track
        program_steps.push_back(step_transfer_buffer_write) ; // start by disk address
        program_steps.push_back(step_seek) ;
        program_steps.push_back(step_sector_write) ;
        program_steps.push_back(step_done) ;
        break ;
    case RX11_CMD_SET_MEDIA_DENSITY:
        // reformat of whole disk, rx211 only
        rxer = 0 ;
        program_steps.push_back(step_done) ;
        break ;
    case RX11_CMD_READ_STATUS:
        // "drive ready bit" in RXCS only valid here or after INIT ?
        if (selected_drive()->check_ready())
            rxes |= BIT(7) ; // drive ready
        program_steps.push_back(step_done) ;
        // step_done sets more rxes flags
        break ;
    case RX11_CMD_READ_ERROR_REGISTER:
        program_steps.push_back(step_done_read_error) ;
        break ;
        /*
        cmd_read_error_register2: RX211
            rxer -> rxdb
            step_transfer_buffer_write(2) receive WC,BA
            step_register_dump // to own buffer
            step_switch_to_worker // now blocking
            step_DMA_write // write registerdump to memory
            step_done
        break ;
        */
    } // switch
    // wake up worker, start program
    controller->update_status("go() -> update_status") ;

    pthread_cond_signal(&on_worker_cond);
    pthread_mutex_unlock(&on_worker_mutex);

}

// thread
void RX0102uCPU_c::worker(unsigned instance) {
    UNUSED(instance); // only one

    assert(!pthread_mutex_lock(&on_worker_mutex));

    // set prio to RT, but less than RL11 controller
    worker_init_realtime_priority(rt_device);

    while (!workers_terminate) {
        if (program_complete()) {
            // wait for start signal
            int res = pthread_cond_wait(&on_worker_cond,
                                        &on_worker_mutex);
            if (res != 0) {
                ERROR("RX0102uCPU_c::worker() pthread_cond_wait = %d = %s>", res, strerror(res));
                continue;
            }
        } else {
            // execute one step
            enum step_e step_cur = step_current() ;
            step_execute(step_cur) ; // may contain  timeout_c.wait() or pthread_cond_wait
            step_next() ; // programcounter always on next step
        }
        if (signal_error) {
            // stop execution on error
            program_clear() ;
            step_execute(step_done) ;
        }
    }
    assert(!pthread_mutex_unlock(&on_worker_mutex));
}


