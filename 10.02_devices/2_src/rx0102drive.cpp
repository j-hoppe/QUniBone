/* rx0102drive.cpp: implementation of RX01/RX02 disk drive, attached to RX0102 uCPU

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


 5-jan-2020	JH      start

 The microCPU board contains all logic and state for the RX01/02 subsystem.
 It is connected on one side two to "dump" electro-mechanical drives,
 on the other side two a RX11/RXV11/RX211/RXV21 UNIBUS/QBUS interface.
 */

#include <assert.h>

#include <array>

#include "logger.hpp"
#include "timeout.hpp"
#include "utils.hpp"
#include "rx0102ucpu.hpp"
#include "rx0102drive.hpp"

RX0102drive_c::RX0102drive_c(RX0102uCPU_c *_uCPU, bool _is_RX02) :
    // no controller assigned, command by box uCPU
    storagedrive_c(nullptr)
{
    geometry.cylinder_count = cylinder_count_const ;
	geometry.head_count = 1 ;
    geometry.sector_count = sector_count_const ;
	// sector_size_bytes is 128/256 depending on density

    uCPU = _uCPU ; // link to micro CPU board
    is_RX02 = _is_RX02 ;
    if (!is_RX02) {
        log_label = "RXDRV"; // to be overwritten by RX11 on create
        type_name.set("RX01") ;
        set_density(false) ;
        density_name.readonly = true ;
    } else {
        log_label = "RYDRV"; // to be overwritten by RX11 on create
        type_name.set("RX02") ;
        set_density(true) ; // default: start as 512k MFM
        density_name.readonly = false ;
    }
	assert(drive_type_c::is_RX(drive_type)) ; // set_density() OK?

    // some rxdp floppy images start at track #1 instead of #0
    imagetrack0.value = true ;

    // "enable" and power switch are controlled by uCPU in case box
    enabled.readonly = true ;
}

// return false, if illegal parameter value.
// verify "new_value", must output error messages
//
// density control &conversion:
// SD/DD can be set when image loaded, reinterprets image. User must know!
// over-long images may be generated
// load of image: SD/DD determined from file size
bool RX0102drive_c::on_param_changed(parameter_c *param) 
{
    if (param == &density_name) {
        // toupper() ... really!
        std::transform(density_name.new_value.begin(), density_name.new_value.end(), density_name.new_value.begin(), ::toupper);
        if (!strcasecmp(density_name.new_value.c_str(), "SD"))
            set_density(false);
        else if (!strcasecmp(density_name.new_value.c_str(), "DD"))
            set_density(true);
        else {
            // throw bad_parameter_check("drive type must be RX01 or RX02") ;
            ERROR("drive double_density SD or DD");
            return false;
        }
    } else if (image_is_param(param)
               && image_recreate_on_param_change(param) ) {
        // change of file image changes state, results also in close()
        // assume new "floppy" has no delete marks set
        memset(deleted_data_marks, 0, sizeof(deleted_data_marks)) ;

        if (imagetrack0.value)  // image contains unused track 0
        	geometry.filesystem_offset = geometry.sector_count * geometry.sector_size_bytes ; // disk data, boot loader at track #1
        else
            geometry.filesystem_offset = 0 ; // disk data, boot loader at image start
		
        image_open(true) ; // may fail
        // file size determines double_density
        if (is_RX02 && image_is_open()) {
            // RX02: user can set density only if not file loaded
            // if file: double_density set by file_size
            // RX02DD ?
            unsigned single_density_image_size = 128 * geometry.cylinder_count * geometry.sector_count ; // 256.256
            if (image_size() > single_density_image_size)
                set_density(true) ;
            else set_density(false) ;
        }
    }
    return storagedrive_c::on_param_changed(param); // more actions (for enable)
}

void RX0102drive_c::set_density(bool _double_density) 
{
    double_density = _double_density;
    if (!_double_density) {
        // RX01, RX02 FM encoding
        geometry.sector_size_bytes = 128; // in byte
        density_name.value = "SD"; // no "on change" callbacks
        drive_type = drive_type_e::RX01 ;
    } else {
        // RX02 MFM encoding = Double Density floppy
        geometry.sector_size_bytes = 256; // in byte
        density_name.value = "DD"; // no "on change" callbacks
        drive_type = drive_type_e::RX02 ;
    }
	capacity.value = geometry.get_raw_capacity() ;

//    uCPU->on_drive_state_changed(this) ;
}


// uCPU may query the "ready/ door open state"
// true: file image loaded: "door closed" + "floppy inserted"
bool RX0102drive_c::check_ready(void) 
{
    return image_is_open() ;
}


void RX0102drive_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
    UNUSED(dclo_edge) ;
    UNUSED(aclo_edge) ;
    uCPU->on_drive_state_changed(this) ; // not needed, uCPU controls power
}

// if seeking or ontrack: retrack head to 0
void RX0102drive_c::on_init_changed(void) 
{
}

unsigned RX0102drive_c::get_rotation_ms() 
{
    return (1000*60) / full_rpm ; // time for index hole to pass
}

unsigned RX0102drive_c::get_cylinder() {
    return cylinder ;
}

void RX0102drive_c::set_cylinder(unsigned cyl) 
{
    cylinder = cyl ;
    current_track.set(cyl) ;
}


bool RX0102drive_c::check_disk_address(unsigned track, unsigned sector) 
{
    error_illegal_track = (track >= geometry.cylinder_count) ;
    error_illegal_sector = (sector < 1 || sector > geometry.sector_count) ;
    return !error_illegal_track && !error_illegal_sector ;
}

// sector offset in image file in bytes
int RX0102drive_c::get_sector_image_offset(unsigned track, unsigned sector) 
{
    return ((track * geometry.sector_count) + (sector-1)) * geometry.sector_size_bytes ;
}

// false: error
bool RX0102drive_c::sector_read(uint8_t *sector_buffer, bool *deleted_data_mark, unsigned track, unsigned sector, bool with_delay) 
{
    if (!check_disk_address(track,sector))
        return false ;
    if (!check_ready())
        return false ; // no floppy image

    // wait for 1 sector to pass, else ZRXB failures
    unsigned sector_us = 1000 * get_rotation_ms() / geometry.sector_count ; // about 6.5 ms
    if (with_delay)
        timeout_c().wait_us(sector_us / emulation_speed.value) ;

    *deleted_data_mark = deleted_data_marks[track][sector] ;
    DEBUG_FAST("sector_read(): delmark=%d, track=%d, sector=%d", (unsigned)*deleted_data_mark, (unsigned)track, (unsigned)sector) ;

    if (! imagetrack0.value) {
        // file image does not contain track 0: skip it
        if (track == 0) {
            memset(sector_buffer, 0, geometry.sector_size_bytes);
            return true ;
        } else {
            track-- ;
        }
    }

    int offset = get_sector_image_offset(track, sector) ;
    DEBUG_FAST("sector_read(): reading 0x%03x bytes from file offset 0x%06x", (unsigned) geometry.sector_size_bytes, (unsigned) offset);
    image_read(sector_buffer, (unsigned) offset, geometry.sector_size_bytes) ;
    // logger->debug_hexdump(this, "image_read():", (uint8_t *) sector_buffer, sector_size_bytes, NULL);
    return true ;
}

// false: error
bool RX0102drive_c::sector_write(uint8_t *sector_buffer, bool deleted_data_mark, unsigned track, unsigned sector, bool with_delay) 
{
    if (!check_disk_address(track,sector))
        return false ;
    if (!check_ready())
        return false ; // no floppy image

    if (image_is_readonly()) {
        // No means to detect write-protected floppies?
        WARNING("Write access to readonly floppy image file ignored") ;
        return true ;
    }

    // wait for 1 sector to pass, else ZRXB failures
    unsigned rotation_ms =  (1000*60) / full_rpm ; // time for index hole to pass
    unsigned sector_us = 1000 * rotation_ms / geometry.sector_count ; // about 6.5 ms
    if (with_delay)
        timeout_c().wait_us(sector_us / emulation_speed.value) ;

    deleted_data_marks[track][sector] = deleted_data_mark ;

    DEBUG_FAST("sector_write(): delmark=%d, track=%d, sector=%d", (unsigned)deleted_data_mark, (unsigned)track, (unsigned)sector) ;

    if (!imagetrack0.value) {
        // file image does not contain track 0: skip it

        if (track == 0) {
            // do not write to ignored track
            return true ;
        } else {
            track-- ;
        }
    }
    int offset = get_sector_image_offset(track, sector) ;
    DEBUG_FAST("sector_write(): writing 0x%03x bytes to file offset 0x%06x", (unsigned) geometry.sector_size_bytes, (unsigned) offset);
    image_write(sector_buffer, (unsigned) offset, geometry.sector_size_bytes) ;
    // logger->debug_hexdump(this, "image_write():", (uint8_t *) sector_buffer, sector_size_bytes, NULL);
    return true ;
}


// no thread, just passive mechnical device
void RX0102drive_c::worker(unsigned instance) 
{
    UNUSED(instance); // only one
}

