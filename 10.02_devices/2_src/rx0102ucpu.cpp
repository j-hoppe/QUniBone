/* rx0102cpu.cpp: implementation of RX01/RX02 floppy disk CPU logic, attached to RX11/211 controller

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

//#include "gpios.hpp" // ARM_DEBUG_PIN
#include "logger.hpp"
#include "timeout.hpp"
#include "utils.hpp"
#include "rx0102ucpu.hpp"
#include "rx0102drive.hpp"
#include "rx11211.hpp"

// link uCPU to its RX controller
// RX01/02 type defined later by caller
RX0102uCPU_c::RX0102uCPU_c(RX11211_c *controller): device_c(), controller(controller) {

    signal_function_density	= false ; // const for RX01



    // init
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
    case RX211_CMD_SET_MEDIA_DENSITY:
        return "SET_MEDIA_DENSITY" ;
    case RX11_CMD_READ_STATUS:
        return "READ_STATUS" ;
    case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
        return "WRITE_SECTOR_WITH_DELETED_DATA" ;
    case RX11_CMD_READ_ERROR_CODE:
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
    case step_format_track:
        return "step_format_track" ;
    case step_seek_next:
        return "step_seek_next" ;
    case step_init_done:
        return "step_init_done" ;
    case step_done_read_error_code:
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
    case step_transfer_buffer_write: // RX(2)11 controller fills buffer before function execution ()
        // many programs accept parameters
        // transfer_byte_count at program setup
        transfer_byte_idx = 0 ;
        signal_transfer_request = true ; // may write first byte now
        controller->update_status("step_execute(step_transfer_buffer_write) -> update_status") ; // show TR bit in RXCS
        // wait for rxdb_after_write() to signal transfer completion
        pthread_cond_wait(&on_worker_cond, &on_worker_mutex);
//        logger->debug_hexdump(this, "transfer_buffer after write", (uint8_t *) transfer_buffer, sizeof(transfer_buffer), NULL);
        break ;
    case step_transfer_buffer_read: // RX(2)11 controller reads back buffer (only "empty")
        assert(program_function_code == RX11_CMD_EMPTY_BUFFER || program_function_code == RX11_CMD_READ_ERROR_CODE) ;
//        logger->debug_hexdump(this, "transfer_buffer before read", (uint8_t *) transfer_buffer, sizeof(transfer_buffer), NULL);
        // transfer_byte_count at program setup
        transfer_byte_idx = 0 ;
        // put first word of sector_buffer or extended_status into RXDB
        signal_transfer_request = true ; // when RXDB is valid
        rxdb = transfer_buffer[0] ; // 	1st byte readable
        DEBUG("transfer_buffer[0] = %06o", rxdb) ;
        controller->update_status("step_execute(step_transfer_buffer_read) -> update_status") ; // show TR bit in RXCS
        // wait for rxdb_after_read() to signal transfer completion
        pthread_cond_wait(&on_worker_cond, &on_worker_mutex);
        break ;
    case step_seek: // head movement
        pgmstep_seek() ;
        // complete_rxes() ; // no access to media
        break ;
    case step_head_settle: // if head has moved, it needs 100ms  to stabilize
        // [6] word 4 <5> Head Load Bit
        extended_status[6] |= BIT(5) ;
        // headsettle_time_ms set by seek()
        timeout.wait_ms(headsettle_time_ms / emulation_speed.value) ;
        break ;
    case step_sector_write: { // sector buffer to disk surface
        if (selected_drive()->is_double_density != program_function_density) {
            // density error
            extended_status[0] = 0240 ;
            rxes |= BIT(4) ;
            signal_error = true ;
        } else {
            signal_error = !selected_drive()->sector_write(sector_buffer, deleted_data_mark,
                           rxta, rxsa, true) ;
            if (signal_error)
                extended_status[0] = 0110 ; // no medium => no clock from data separator
        }
        complete_rxes() ;
    }
    break ;
    case step_sector_read: { // disk surface to sector buffer
        if (selected_drive()->is_double_density != program_function_density) {
            // density error
            extended_status[0] = 0240 ;
            rxes |= BIT(4) ;
            signal_error = true ;
        } else {
            signal_error = !selected_drive()->sector_read(sector_buffer, &deleted_data_mark,
                           rxta, rxsa, true) ;
            if (signal_error)
                extended_status[0] = 0110 ; // no medium => no clock from data separator
        }
        complete_rxes() ;
    }
    break ;
    case step_seek_next: {
        // cheap&dirty, only for "change media density"
        unsigned wait_ms = (selected_drive()->track_step_time_ms + selected_drive()->head_settle_time_ms) / selected_drive()->emulation_speed.value ;
        selected_drive()->set_cylinder(selected_drive()->get_cylinder()+1) ;
        DEBUG("drive %d stepping to next track, cyl = %d", selected_drive()->unitno.value, selected_drive()->get_cylinder());
        timeout.wait_ms(wait_ms) ;
    }
    break ;
    case step_format_track: {
        // write 26 sectors to current track
        uint8_t sector_00s[256] ;
        memset(sector_00s, 0, sizeof(sector_00s)) ;
        assert(selected_drive()->is_double_density  == program_function_density) ;
        for (unsigned sa=1 ; !signal_error && sa <= selected_drive()->sector_count ; sa++) {
            signal_error = !selected_drive()->sector_write(sector_00s, false, selected_drive()->get_cylinder(), sa, false) ;
            if (signal_error)
                extended_status[0] = 0110 ; // no medium => no clock from data separator
        }
        // wait explicit one rotation
        timeout.wait_ms(selected_drive()->get_rotation_ms() / selected_drive()->emulation_speed.value) ;
        complete_rxes() ;
    }
    break ;
    case step_done_read_error_code:
        // only case where rxdb is *not* rxes
        // RX02:
        initializing = false ; // also called at end of INIT
        signal_done = true ;
        signal_transfer_request = false ;
        complete_error_codes() ;
        if (is_RX02) {
            // not documented, Simh behaviour:
            rxdb = complete_rxes() ;
            rxdb &= ~(BIT(8) | BIT(7) | BIT(5)) ; //   SimH RYES_USEL|RYES_DDEN|RYES_DRDY

            /* footnote on 4-44 NOT UNDERSTOOD:
            "for  DMA interfaces the controlelr status soft register is sent to the interface
            at the end of the command. The four status bits are included in an 8-bit word.
            Unit select=bit 7, Density of drive 1= bit 6, Head load = bit 5,
            Density of read Error Register Command = bit 0""
            */
        } else
            rxdb = extended_status[0] ; // rxer
        controller->update_status("step_execute(step_done_read_error_code) -> update_status") ; // may trigger interrupt
        break ;
    case step_init_done: // idle between functions
        initializing = false ;
        signal_done = true ;
        signal_transfer_request = false ;
        complete_error_codes() ;
        complete_rxes() ;
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
        complete_error_codes() ;
        rxdb = complete_rxes() ;
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
        rxdb = complete_rxes() ;
        signal_done = true ;
        signal_error = true ;
        signal_transfer_request = false ;
        controller->update_status("step_execute(step_error) -> update_status") ; // may trigger interrupt
        break ;

    }
}


// OR standard flags into RXES register
// "init done" and "drive ready" not set here, depends on function
uint16_t  RX0102uCPU_c::complete_rxes(void) {

    if (deleted_data_mark)
        rxes |= BIT(6) ;

    if (is_RX02) {
        if (!power_switch.value)
            rxes |= BIT(3) ; // we are powered OFF, RX AC LO ;-)

        // density error:
        bool double_density = selected_drive()->is_double_density ;
        if (double_density)
            rxes |= BIT(5) ;

        // UNIT select: only RX02, erroneously documented for RX11 too?
        if (signal_selected_drive_unitno)
            rxes |= BIT(8) ; // unit #1 select
    }

    DEBUG("complete_rxes(): rxes := %06o", rxes) ;
    return rxes ;
}

// generate content of RXER register depending on drive state
// None of the media and format related errors can occure here
// rxer for RX01, 4-word status for RX02.
// extended error status is localed in uCPU, but accessed by RX211 too.
void RX0102uCPU_c::clear_error_codes(void) {
    memset(extended_status, 0, sizeof(extended_status)) ;
}

void  RX0102uCPU_c::complete_error_codes(void) {
    // static information only, dynamical data inserted in program steps
    if (is_RX02) {
        extended_status[2] = drives[0]->get_cylinder() ;
        extended_status[3] = drives[1]->get_cylinder() ;

        // [6] word 4 <7> Unit Select Bit
        if (signal_selected_drive_unitno)
            extended_status[6] |= BIT(7) ;
        else
            extended_status[6] &= ~ BIT(7) ;
        if (drives[0]->is_double_density)
            extended_status[6] |= BIT(4) ;
        else
            extended_status[6] &= ~ BIT(4) ;
        if (drives[1]->is_double_density)
            extended_status[6] |= BIT(6) ;
        else
            extended_status[6] &= ~ BIT(6) ;
        // [6] word 4 <0> Density of Read Error Register Command
        // ?

        extended_status[7] = selected_drive()->get_cylinder() ;

        DEBUG("complete_error_codes(): RX02 status word1=%06o, word2=%06o, word3=%06o, word4=%06o",
              extended_status[0] + ((unsigned)extended_status[1] << 8),
              extended_status[4] + ((unsigned)extended_status[6] << 8),
              extended_status[6] + ((unsigned)extended_status[7] << 8)) ;
    } else
        DEBUG("complete_error_codes(): RXER = %03o", (unsigned) extended_status[0]) ;
}


// seek track, part of read/write sector.
void RX0102uCPU_c::pgmstep_seek(void) {
    timeout_c timeout ;
    RX0102drive_c *drive = selected_drive() ;
    DEBUG("pgmstep_seek(drive=%d, cur track = %d, rxta = %d)", signal_selected_drive_unitno, drive->get_cylinder(), rxta) ;
    uint8_t	track_address = rxta ;

    // periodically executed. false = ready
    unsigned calcperiod_ms = 10; // waits per loop
    // head can pass this much tracks per loop
    unsigned trackmove_increment = calcperiod_ms / selected_drive()->track_step_time_ms * emulation_speed.value; // 10/2 =5ms per track, this much per loop
    // parameter check already done
    assert(track_address < selected_drive()->cylinder_count) ;

    // nothing todo if already on track
    headsettle_time_ms = (track_address == drive->get_cylinder()) ? 0 : selected_drive()->head_settle_time_ms ;
    while (track_address > drive->get_cylinder()) {
        DEBUG("drive %d seeking outward, cyl = %d", drive->unitno.value, drive->get_cylinder());
        drive->set_cylinder(drive->get_cylinder() + trackmove_increment);
        if (drive->get_cylinder() >= track_address) {
            // seek head outward finished
            // proportionally reduced seek time?
            drive->set_cylinder(track_address);
            DEBUG("drive %d seek outwards complete, cyl = %d", drive->unitno.value, drive->get_cylinder());
            //drive->change_state(RX0102_STATE_lock_on);
        } else
            timeout.wait_ms(calcperiod_ms);
    }
    while (track_address < drive->get_cylinder()) {
        // seek head inwards
        if ((drive->get_cylinder() - track_address) <= trackmove_increment) {
            drive->set_cylinder(track_address);
            DEBUG("drive %d seek inwards complete, cyl = %d", drive->unitno.value, drive->get_cylinder());
            //change_state(RX0102_STATE_lock_on);
        } else {
            DEBUG("drive %d seeking inwards, cyl = %d", drive->unitno.value, drive->get_cylinder());
            drive->set_cylinder(drive->get_cylinder() - trackmove_increment);
            timeout.wait_ms(calcperiod_ms);
        }
    }
}


// Notify read access to RXDB by controller
// puts next buffer cell into RXDB.
// only for block read when state == state_transfer_read_result;
void RX0102uCPU_c::rxdb_after_read(void) {
    if (program_complete())
        return ;

    DEBUG("rxdb_after_read() in function %s, word %d/%d", function_code_text(program_function_code),transfer_byte_idx,transfer_byte_count) ;

    if (transfer_byte_idx >= transfer_byte_count)
        return ;

    if (program_function_code == RX11_CMD_EMPTY_BUFFER || program_function_code == RX11_CMD_READ_ERROR_CODE ) {
        // read data from buffer
        if (transfer_byte_idx+1 < transfer_byte_count) {
            // put next buffer byte into RXDB
            assert(transfer_buffer) ;
            rxdb = transfer_buffer[++transfer_byte_idx] ; // read 8bit, return 16 bit
            DEBUG("transfer_buffer[%d] = %06o",transfer_byte_idx, rxdb) ;
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

    if (program_complete()) {
        // RXDB is read/write when not executing a command
        rxdb = w ;
        if (is_RX02) // patch
            rxdb &= 0173767 ; // RX211: bit 11,4 not readable ? ZRXFB0 test 12
        DEBUG("rxdb_after_write() rxdb = w") ;
        controller->update_status("rxdb_after_write() no op -> update_status") ; // new RXDB, new TR
        return ;
    }
    DEBUG("rxdb_after_write() function %s, word %d/%d", function_code_text(program_function_code),transfer_byte_idx, transfer_byte_count) ;

    if (transfer_byte_idx >= transfer_byte_count)
        // not expecting any more data
        return ;

    switch(program_function_code) {
    case RX11_CMD_FILL_BUFFER:
        // is expecting data.
        assert(transfer_byte_idx < transfer_byte_count) ; // else state flips to "done"
        assert(transfer_buffer == sector_buffer) ;
        transfer_buffer[transfer_byte_idx++] = w & 0xff ;
        if (transfer_byte_idx >= transfer_byte_count)
            complete = true ;
        break ;
    case RX11_CMD_READ_SECTOR:
    case RX11_CMD_WRITE_SECTOR:
    case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
        // accept sector + track address
        if (transfer_byte_idx == 0) {
            rxsa = w & 037; // bit 7-5 always 0, 15-8 don't care
            // [5] word 3 <15:8> Target Sector of Current Disk Access
            extended_status[5] = rxsa ;
            if (rxsa < 1 || rxsa > selected_drive()->sector_count) {
                signal_error = true ;
                extended_status[0] = 0070 ; // "Can't find sector"
            }
        } else if (transfer_byte_idx == 1) {
            rxta = w & 0177 ; // bit 7 always 0, 15-8 don't care
            // [4] word 3 <7:0> Target Track of Current Disk Access
            extended_status[4] = rxta ;
            if (rxta >= selected_drive()->cylinder_count) {
                signal_error = true ;
                extended_status[0] = 0040 ; // "Can't find track"
            }
            // even if sector is invalid, tr must be transfered.
            complete = true ;
        }
        transfer_byte_idx++ ;
        break ;
    case RX211_CMD_SET_MEDIA_DENSITY:
        // wait for "I" key word
        if (transfer_byte_idx == 0) {
            if (w != 'I') {
                signal_error = true ;
                extended_status[0] = 0250 ; // "Wrong key word"
            }
            complete = true ;
        }
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
// last step of construction after drives have been assigned
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
    if (is_RX02) {
        name.value = "rybox";
        type_name.value = "RX0102uCPU";
        log_label = "rycpu";
    } else {
        name.value = "rxbox";
        type_name.value = "RX0102uCPU";
        log_label = "rxcpu";
    }

}

// which buffer to transfer via RXDB?
uint8_t	*RX0102uCPU_c::get_transfer_buffer(uint8_t function_code)
{
    if (is_RX02) {
        switch (function_code) {
        case RX11_CMD_FILL_BUFFER:
        case RX11_CMD_EMPTY_BUFFER:
            return sector_buffer ;
        case RX11_CMD_READ_ERROR_CODE:
            return extended_status ;
        default:
            return NULL ;
        }
    } else {
        switch (function_code) {
        case RX11_CMD_FILL_BUFFER:
        case RX11_CMD_EMPTY_BUFFER:
            return sector_buffer ;
        default:
            return NULL ;
        }
    }
}

// how many data bytes are to be transfered via RXDB
// for each function code?
unsigned	RX0102uCPU_c::get_transfer_byte_count(uint8_t function_code, bool double_density=false)
{
    if (is_RX02) {
        switch (function_code) {
        case RX11_CMD_FILL_BUFFER:
        case RX11_CMD_EMPTY_BUFFER:
            // Here data bytes, DMA does words.
            return double_density ? 256 : 128 ;
        case RX11_CMD_READ_SECTOR:
        case RX11_CMD_WRITE_SECTOR:
        case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
            return 2; // sector & track
        case RX211_CMD_SET_MEDIA_DENSITY:
            return 1 ; // mandatory ASCII "I"
        case RX11_CMD_READ_ERROR_CODE:
            return 8; // 4 words DMA
        default:
            return 0 ;
        }
    } else {
        // RX01
        switch (function_code) {
        case RX11_CMD_FILL_BUFFER:
        case RX11_CMD_EMPTY_BUFFER:
            return 128 ; // sector buffer
        case RX11_CMD_READ_SECTOR:
        case RX11_CMD_WRITE_SECTOR:
        case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
            return 2; // sector & track
        default:
            return 0 ;
        }
    }
}

// Not clear how the hardware communicates RX2WC between controller and uCPU.
// Here 2 weird interface functions:

// Check wether rx2wc is too large for current transfer len
// if true: abort function and update RXCS status
bool RX0102uCPU_c::rx2wc_overflow_error(uint8_t function_code, bool double_density, uint16_t rx2wc) {
    assert(is_RX02) ;
    unsigned transfer_byte_count = get_transfer_byte_count(function_code, double_density) ;

    if (rx2wc > transfer_byte_count) {
        signal_error = signal_error_word_count_overflow = true ;
        extended_status[0] = 0230 ;
        step_execute(step_done) ;
        return true ;
    } else
        return false ;
}

uint16_t  RX0102uCPU_c::rx2wc() {
    RX211_c *rx211 = dynamic_cast<RX211_c*>(controller) ;
    assert(rx211) ;
    extended_status[1] = rx211->rx2wc ; // update often
    return rx211->rx2wc ;
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

    // density on INIT always SD? Read boot sector from DD disk?
    // program_function_density = 0;
    // ZRXF requires boot sector read with automatic density select
    program_function_density = selected_drive()->is_double_density;
    signal_done = false ;
    signal_error = false ;
    signal_transfer_request = false ;
    initializing = true ;
    rxdb = 0 ;
    rxes = 0 ;
    clear_error_codes() ;
    controller->update_status("init() -> update_status") ;


    // boot drive 0, drive 1 also homed
    // no "home" delay
    drives[0]->set_cylinder(0) ;
    drives[1]->set_cylinder(0) ;

    // generate "read sector", set transfer buffer as if RX11 issued a "read sector"
    rxta = 1 ; // track
    rxsa = 1 ; // sector
    // transfer_byte_count = 2 ;
    signal_selected_drive_unitno = 0 ;

    // setup sequence
    pthread_mutex_lock(&on_worker_mutex);
    program_clear() ; // aborts worker()
    program_steps.push_back(step_seek) ;
    program_steps.push_back(step_sector_read) ;
    program_steps.push_back(step_init_done) ;

    // wakeup worker, start program
    pthread_cond_signal(&on_worker_cond);
    pthread_mutex_unlock(&on_worker_mutex);

}


void RX0102uCPU_c::go() { // execute function_code
    // program starts when transfer buffer filled
    DEBUG("go(), function=%d=%s", signal_function_code, function_code_text(signal_function_code)) ;
    program_function_code = signal_function_code ; // stabilze against CSR changes
    program_function_density = signal_function_density ;

    if (!power_switch.new_value)
        return ; // powered off

    pthread_mutex_lock(&on_worker_mutex);

    signal_done = false ;
    signal_error = false ;
    signal_error_word_count_overflow = false ;
    signal_transfer_request = false ;
    deleted_data_mark = false ;

    transfer_byte_count = get_transfer_byte_count(program_function_code, program_function_density) ;
    transfer_buffer = get_transfer_buffer(program_function_code) ;

    rxes = 0 ;

    program_clear() ;


    switch(program_function_code) {

    case RX11_CMD_FILL_BUFFER:
        clear_error_codes() ;
        if (is_RX02) // byte count set by DMA word count
            transfer_byte_count = 2*rx2wc() ; // must have been checked by rx2wc_overflow_error()
        // EK-0RX02-TM, 5.3.2.7: "when filling the buffer for word counts less than maximum, 
        // the unused portion of the buffer is filled with zeros"
        memset(sector_buffer, 0, sizeof(sector_buffer)) ; // == transfer_buffer
        program_steps.push_back(step_transfer_buffer_write) ; // start by data
        program_steps.push_back(step_done) ;
        break ;
    case RX11_CMD_EMPTY_BUFFER:
        clear_error_codes() ;
        if (is_RX02) // byte count set by DMA word count
            transfer_byte_count = 2*rx2wc() ; // must have been checked by rx2wc_overflow_error()
        program_steps.push_back(step_transfer_buffer_read) ;
        program_steps.push_back(step_done) ;
        break ;
    case RX11_CMD_READ_SECTOR:
        clear_error_codes() ;
        program_steps.push_back(step_transfer_buffer_write) ; // start by disk address
        program_steps.push_back(step_seek) ;
        program_steps.push_back(step_head_settle) ;
        program_steps.push_back(step_sector_read) ;
        program_steps.push_back(step_done) ;
        break ;
    case RX11_CMD_WRITE_SECTOR:
    case RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA:
        clear_error_codes() ;
        deleted_data_mark = (program_function_code == RX11_CMD_WRITE_SECTOR_WITH_DELETED_DATA) ;
        program_steps.push_back(step_transfer_buffer_write) ; // start by disk address
        program_steps.push_back(step_seek) ;
        program_steps.push_back(step_head_settle) ;
        program_steps.push_back(step_sector_write) ;
        program_steps.push_back(step_done) ;
        break ;
    case RX211_CMD_SET_MEDIA_DENSITY:
        clear_error_codes() ;
        if (is_RX02) {
            // reformat of whole disk, RX211 only

            // empty file
            signal_error = !selected_drive()->image_is_open() ;
            if (!signal_error) {
                selected_drive()->image_truncate() ;
                selected_drive()->set_density(program_function_density) ;
                // file mounted, media formattable
                rxta = 0 ; // seek to HOME

                program_steps.push_back(step_transfer_buffer_write) ; // start by key word to RXDB
                program_steps.push_back(step_seek) ;
                // format each track, then step put wards
                program_steps.push_back(step_format_track) ;
                for (unsigned i=1 ; i < selected_drive()->cylinder_count ; i++) {
                    // rxta const == 0
                    program_steps.push_back(step_seek_next) ;
                    program_steps.push_back(step_format_track) ;
                }
            }
            // no home to track 0
            program_steps.push_back(step_done) ;
        } else {
            // noop on RX01
            step_execute(step_done) ;
        }
        break ;
    case RX11_CMD_READ_STATUS:
        // "drive ready bit" in RXCS only valid here or after INIT ?
        if (selected_drive()->check_ready())
            rxes |= BIT(7) ; // drive ready
        program_steps.push_back(step_done) ;
        // step_done sets more rxes flags
        break ;
    case RX11_CMD_READ_ERROR_CODE:
        if (is_RX02)
            program_steps.push_back(step_transfer_buffer_read) ;
        program_steps.push_back(step_done_read_error_code) ;
        break ;
    }
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


