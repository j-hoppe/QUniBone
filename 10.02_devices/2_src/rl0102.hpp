/* rl02.cpp: implementation of RL01/RL02 disk drive, attached to RL11 controller

 Copyright (c) 2018, Joerg Hoppe
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


 12-nov-2018  JH      entered beta phase
 */
#ifndef _RL0102_HPP_
#define _RL0102_HPP_

#include <stdint.h>
#include <string.h>

#include "storagedrive.hpp"

#define RL0102_STATE_power_off 0xff // not enabled. no valid state
// page 4-9 of RLO2 UG
#define RL0102_STATE_load_cartridge 0 // drive stop, door unlocked, cartridge can be loaded
#define RL0102_STATE_spin_up 1
#define RL0102_STATE_brush_cycle	2
#define RL0102_STATE_load_heads 3
#define RL0102_STATE_seek	4
#define RL0102_STATE_lock_on 5 // after seek track reached (READY lamp)
#define RL0102_STATE_unload_heads 6
#define RL0102_STATE_spin_down 7

// page 4-9 of RLO2 UG
#define RL0102_STATUS_STATE	0x0007 // bitmask for RL0102_STATE_*
#define RL0102_STATUS_BH 0x0008 // brushes home?
#define RL0102_STATUS_HO 0x0010 // heads out
#define RL0102_STATUS_CO 0x0020 // cover open
#define RL0102_STATUS_HS 0x0040 // head selected
#define RL0102_STATUS_DT 0x0080 // drive type: 0=RL01, 1 = RL02
#define RL0102_STATUS_DSE 0x0100 // drive select error (power off?)
#define RL0102_STATUS_VC 0x0200 // set on transition to "on-track". cleard by controller "get status"
#define RL0102_STATUS_WGE 0x0400 // write gate error: write protect/drive not ready/drive has error
#define RL0102_STATUS_SPE 0x0800 // spin error (not used)
#define RL0102_STATUS_SKTO 0x1000 // heads come not on-track (not used)
#define RL0102_STATUS_WL 0x2000 // drive is write protected
#define RL0102_STATUS_CHE 0x4000 // current head error (not used)
#define RL0102_STATUS_WDE 0x8000 // write data error (not used)

class RL11_c;
class RL0102_c: public storagedrive_c {
private:
	uint16_t calc_crc(const int wc, const uint16_t *data);
	void change_state(unsigned new_state);

	void state_power_off(void);
	void state_load_cartridge(void);
	void state_spin_up(void);
	void state_brush_cycle(void);
	void state_load_heads(void);
	void state_seek(void);
	void state_lock_on(void);
	void state_unload_heads(void);
	void state_spin_down(void);

	unsigned seek_destination_cylinder; // target for seek
	unsigned seek_destination_head;

	timeout_c state_timeout;
	timeout_c rotational_timeout;
//	unsigned state_wait_ms ;

public:
	// the RL11 controller my see everything
	// dynamic state
	volatile unsigned cylinder; // current head position
	volatile unsigned head; // selected head
//	unsigned seek_destination_head; // target for seek

	unsigned cylinder_count;
	unsigned head_count;
	unsigned sector_count;
	unsigned sector_size_bytes; // in byte
	unsigned block_size_bytes; // in byte
	unsigned block_count;

	unsigned full_rpm = 2400; // normal rotation speed

	// timeto spin up cartridge. Doc: 45 sec
	//	ZRLI: less than 30 secs => 25
	unsigned time_spinup_sec = 25;

	// ZRLI: > 200, < 300 ?
	unsigned time_heads_out_ms = 300;

	volatile bool volume_check; // set on "head-on-cylinder", cleared by controller
	volatile bool error_wge; // write not execute: readonly, or other reasons

	void update_status_word(void);
	void update_status_word(bool new_drive_ready_line, bool new_drive_error_line);

public:
	unsigned drivetype; // 1 =RL01, 2 = RL02

	// 1 = drive drive_ready_line to accept commands (flase while seeking, "get status" always allowed)
	bool drive_ready_line; // interface cable wire
	bool drive_error_line; // interface cable wire, drive signals ERROR

	volatile uint16_t status_word; // visible to controller

	parameter_unsigned_c rotation_umin = parameter_unsigned_c(this, "rotation", "rot",/*readonly*/
	true, "rpm", "%d", "Current speed of disk", 32, 10);
	// RL0102_STATE_*. no enum, is param
	parameter_unsigned_c state = parameter_unsigned_c(this, "state", "st", /*readonly*/
	true, "", "%d", "Internal state", 32, 10);

	// user controls
	parameter_bool_c power_switch = parameter_bool_c(this, "powerswitch", "pwr",/*readonly*/
	false, "State of POWER switch");
	parameter_bool_c runstop_button = parameter_bool_c(this, "runstopbutton", "rb",/*readonly*/
	false, "State of RUN/STOP button");
	parameter_bool_c load_lamp = parameter_bool_c(this, "loadlamp", "ll", /*readonly*/
	true, "State of LOAD lamp");
	parameter_bool_c ready_lamp = parameter_bool_c(this, "readylamp", "rl", /*readonly*/
	true, "State of READY lamp");
	parameter_bool_c fault_lamp = parameter_bool_c(this, "faultlamp", "fl", /*readonly*/
	true, "State of FAULT lamp");
	parameter_bool_c writeprotect_lamp = parameter_bool_c(this, "writeprotectlamp", "wpl", /*readonly*/
	true, "State of WRITE PROTECT lamp");
	parameter_bool_c writeprotect_button = parameter_bool_c(this, "writeprotectbutton", "wpb", /*readonly*/
	false, "Writeprotect button pressed");

	// cover normally always "closed", need to get opened for ZRLI
	parameter_bool_c cover_open = parameter_bool_c(this, "coveropen", "co", /*readonly*/
	false, "1, if RL cover is open");
	// not readonly only in "load" state

	RL0102_c(storagecontroller_c *controller);

	bool on_param_changed(parameter_c *param) override;

	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
	void on_init_changed(void) override;


	void set_type(uint8_t drivetype);

	bool cmd_seek(unsigned destination_cylinder, unsigned destination_head);

	// simulate an alternating stream of sector_headers and sector_data
	// on the rotating platter
	// stuff_under_heads count 0..79
	// if even: next segment on platter is sector header
	// if odd: next segmetn on platter is data
	unsigned next_sector_segment_under_heads = 0;

	// is sector with given header on current track?
	bool header_on_track(uint16_t header);

	// wait for next sector header from rotating platter, read it,
	// then increments "next_sector_segment_under_heads" to pos before next data
	bool cmd_read_next_sector_header(uint16_t *buffer, unsigned buffer_size_words);

	// wait for next data block from rotating platter, read it,
	// then increments "next_sector_segment_under_heads" to pos before next header
	// controller must address sector by waiting for it with cmd_read_next_sector_header()
	bool cmd_read_next_sector_data(uint16_t *buffer, unsigned buffer_size_words);

	// write data for current sector under head
	// then increments "stuff_under_heads" to pos before next header
	// controller must address sector by waiting for it with cmd_read_next_sector_header()
	bool cmd_write_next_sector_data(uint16_t *buffer, unsigned buffer_size_words);

	void clear_error_register(void);

	// background worker function
	void worker(unsigned instance) override;
};

#endif
