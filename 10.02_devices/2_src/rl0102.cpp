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

#include <assert.h>

using namespace std;

#include "logger.hpp"
#include "timeout.hpp"
#include "utils.hpp"
#include "rl11.hpp"
#include "rl0102.hpp"

RL0102_c::RL0102_c(storagecontroller_c *controller) :
		storagedrive_c(controller) {
	log_label = "RL0102"; // to be overwritten by RL11 on create
	status_word = 0;
	set_type(2); // default: RL02
	runstop_button.value = false; // force user to load file assume drive is LOAD
	fault_lamp.value = false;
	cover_open.value = false;

}

// return false, if illegal parameter value.
// verify "new_value", must output error messages
bool RL0102_c::on_param_changed(parameter_c *param) {
	if (param == &enabled) {
		if (!enabled.new_value) {
			// disable switches power OFF.
			// must be power on by caller or user after enable
			power_switch.value = false;
			change_state(RL0102_STATE_power_off);
		}
	} else if (param == &type_name) {
		if (!strcasecmp(type_name.new_value.c_str(), "RL01"))
			set_type(1);
		else if (!strcasecmp(type_name.new_value.c_str(), "RL02"))
			set_type(2);
		else {
//		throw bad_parameter_check("drive type must be RL01 or RL02") ;			
			ERROR("drive type must be RL01 or RL02");
			return false;
		}
	}
	return storagedrive_c::on_param_changed(param); // more actions (for enable)
}

void RL0102_c::set_type(uint8_t drivetype) {
	this->drivetype = drivetype;
	switch (drivetype) {
	case 1:
		cylinder_count = 256;
		head_count = 2;
		sector_count = 40;
		type_name.value = "RL01";
		break;
	case 2:
		cylinder_count = 512;
		head_count = 2;
		sector_count = 40;
		type_name.value = "RL02";
		break;
	}
	block_count = cylinder_count * head_count * sector_count;
	sector_size_bytes = block_size_bytes = 256; // in byte
	capacity.value = block_size_bytes * block_count;
}

/* CRC16 as implemented by the DEC 9401 chip
 * simh/PDP11\pdp11_rl.c
 */
uint16_t RL0102_c::calc_crc(const int wc, const uint16_t *data) {
	uint32_t crc, j, d;
	int32_t i;

	crc = 0;
	for (i = 0; i < wc; i++) {
		d = *data++;
		/* cribbed from KG11-A */
		for (j = 0; j < 16; j++) {
			crc = (crc & ~01) | ((crc & 01) ^ (d & 01));
			crc = (crc & 01) ? (crc >> 1) ^ 0120001 : crc >> 1;
			d >>= 1;
		}
	}
	return (uint16_t) crc;
}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void RL0102_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	UNUSED(aclo_edge) ;
	// called at high priority.
	// mutex?
	if (dclo_edge == SIGNAL_EDGE_RAISING) {
		// FAULT lamp, while RL11 evals DC_LO
		// power-on defaults
		change_state( RL0102_STATE_power_off);
		update_status_word(drive_ready_line, true); // RL11 starts with error
	}
}

// if seeking or ontrack: retrack head to 0
void RL0102_c::on_init_changed(void) {
	// called at high priority.
	// mutex?

	if (init_asserted) {
		// seems not to retract head on INIT
		/*
		 if (state.value == RL0102_STATE_seek || state.value == RL0102_STATE_lock_on) {
		 seek_destination_cylinder = 0;
		 seek_destination_head = 0;
		 change_state( RL0102_STATE_seek) ;
		 }
		 */
		// clear_error_register(); !NO! voume check remains
	}
}

// seek is only possible if ready
bool RL0102_c::cmd_seek(unsigned destination_cylinder, unsigned destination_head) {
	assert(destination_cylinder < cylinder_count); // RL11 must calc correct #

	if (state.value != RL0102_STATE_lock_on) {
//	if (state.value != RL0102_STATE_seek && state.value != RL0102_STATE_lock_on) {
		WARNING("Drive seek to cyl.head=%d.%d failed, wrong state %d!", destination_cylinder,
				destination_head, state.value);
		return false; // illegal state
	}

	DEBUG("Drive start seek from cyl.head %d.%d to %d.%d", cylinder, head, destination_cylinder,
			destination_head);

	// seek may stop running seek ?
//	worker_mutex.lock() ;
	this->seek_destination_cylinder = destination_cylinder;
	this->seek_destination_head = destination_head; // time needed to center on track
	head = 0xff; // invalid, to get extra seek time

	// RL11 must see "READY=false" immediately
	update_status_word(/*drive_ready_line*/false, drive_error_line);
	change_state(RL0102_STATE_seek);
//	worker_mutex.unlock() ;
	return true;
}

// separate proc, to have a testpoint
void RL0102_c::change_state(unsigned new_state) {
	unsigned old_state = state.value;
	uint16_t old_status_word = status_word;
	// TODO: only in update_status_word() the visible state of state of ready line and error line should be set.!
	//renome "update_status_word()" to "update_visible_status()"
	state.value = new_state;
	update_status_word(); // contains state
	if (old_state != new_state)
		DEBUG("Change drive %s state from %d to %d. Status word %06o -> %06o.",
				name.value.c_str(), old_state, state.value, old_status_word, status_word);
}

/*** state functions, called repeatedly ***/
void RL0102_c::state_power_off() {
	// drive_ready_line = false; // verified
	// drive_error_line = true; // real RL02: RL11 show a DRIVE ERROR after power on / DC_LO
	type_name.readonly = false; // may be changed between RL01/RL02
	volume_check = true; // starts with volume check?
	cover_open.readonly = true;
	update_status_word(/*drive_ready_line*/false, /*drive_error_line*/true);
	ready_lamp.value = false;
	load_lamp.value = false;
	fault_lamp.value = false;
	writeprotect_lamp.value = false;
//	image_filepath.readonly = true ; // "door locked", disk can not be changed
	image_filepath.readonly = false; // don't be so complicated
	if (power_switch.value == true)
		change_state(RL0102_STATE_load_cartridge);
	state_timeout.wait_ms(100);

}

// drive stop, door unlocked, cartridge can be loaded
void RL0102_c::state_load_cartridge() {
	// drive_ready_line = false; // verified
	type_name.readonly = true; // must be powered of to changed between RL01/RL02
	update_status_word(/*drive_ready_line*/false, drive_error_line);
	load_lamp.value = 1;
	ready_lamp.value = 0;
	writeprotect_lamp.value = writeprotect_button.value;
	cover_open.readonly = false; // can be changed ("opened") only in LOAD state
	// only in "load" state may the "media" be changed
	image_filepath.readonly = false;
	// Path to FAULT: try to load illegal file
	// Path out of FAULT: File OK, or RUN runstop_button released
	// FAULT => state is "load cartridge"
	if (runstop_button.value == true && cover_open.value == false) {
		// LOAD released: start spinning, if file image OK
		// this test is repeated endlessly if button pressed and filename illegal
		if (image_open(image_filepath.value, /*create*/true)) {
			fault_lamp.value = false;
			change_state(RL0102_STATE_spin_up);
			return; // no wait
		} else {
			if (!fault_lamp.value) // error message only once
				ERROR("Could not open/create file \"%s\".", image_filepath.value.c_str());
			fault_lamp.value = true; // disables effect of runstop button
		}
	} else {
		// load cartridge: unlock file
		fault_lamp.value = false;
		if (image_is_open())
			image_close();
	}
	state_timeout.wait_ms(100);
}

void RL0102_c::state_spin_up() {
	unsigned calcperiod_ms = 100;
	// change of rpm in 0.1 secs
	unsigned rpm_increment = full_rpm / (time_spinup_sec * (1000 / calcperiod_ms));

	volume_check = true; // SIMH RLDS_VCK
	cover_open.readonly = true; // can be changed ("opened") only in LOAD state

	// drive_ready_line = false; // verified
	update_status_word(/*drive_ready_line*/false, drive_error_line);

	if (runstop_button.value == false || fault_lamp.value == true) { // stop spinning
		change_state(RL0102_STATE_spin_down);
		return;
	}

	rpm_increment *= emulation_speed.value;
	INFO("Spin up drive speed = %d", rotation_umin.value);

	rotation_umin.value += rpm_increment;
	if (rotation_umin.value > full_rpm) {
		rotation_umin.value = full_rpm;
		cylinder = 0;
		change_state(RL0102_STATE_brush_cycle);
		return;
	}

	load_lamp.value = 0;
	ready_lamp.value = 0;
	writeprotect_lamp.value = writeprotect_button.value || image_is_readonly();
	image_filepath.readonly = true; // "door locked", disk can not be changed

	state_timeout.wait_ms(calcperiod_ms);
}

void RL0102_c::state_brush_cycle() {
	// a real brush was used only on early RL01
	// drive_ready_line = false ;
	update_status_word(/*drive_ready_line*/false, drive_error_line);

	state_timeout.wait_ms(100);
	change_state(RL0102_STATE_load_heads);
}

void RL0102_c::state_load_heads() {
	// drive_ready_line = false ;
	update_status_word(/*drive_ready_line*/false, drive_error_line);
	state_timeout.wait_ms(time_heads_out_ms);

	cylinder = 0;
	this->seek_destination_cylinder = 0;
	this->seek_destination_head = 0; // time needed to center on track
	head = 0xff; // invalid, to get extra seek time

	// now perform a "guard band seek": seek head 0, track 0
	change_state(RL0102_STATE_seek);

//	next_sector_segment_under_heads = 0; // init rotation angle
	next_sector_segment_under_heads = 12; // next header is 6
}

// DEC: seek = 100ms for 512/256 tracks
void RL0102_c::state_seek() {

	// drive_ready_line = false;
	update_status_word(/*drive_ready_line*/false, drive_error_line);

	unsigned calcperiod_ms = 10;
	// head can pass this much tracks per loop
	// calc for RL02
	unsigned trackmove_increment = 512 * calcperiod_ms / 100;
	unsigned trackmove_time_ms; // time for increment seek or part of it
	if (drivetype == 1)
		trackmove_increment /= 2; // RL01 tracks are wider apart
	// trackmove_increment *= emulation_speed.value;

	if (runstop_button.value == false || fault_lamp.value == true) { // stop spinning
		change_state(RL0102_STATE_spin_down);
		return;
	}

	load_lamp.value = 0;
	ready_lamp.value = 0;
	writeprotect_lamp.value = writeprotect_button.value || image_is_readonly();

	// need delay for head search (ZRLI, test 9)
	// here done BEFORE cylinder search ...
	// set cur head to "invalid" to get the extra head seek time
	if (seek_destination_head != head) {
		head = seek_destination_head;
		// wait for .. say .. 3 sector passes
		// unsigned sector_time_us = (60 * MILLION) / (full_rpm * sector_count); // = 625us
		// trackmove_time_ms = (5 * sector_time_us) / 1000;
		// ZRLJ test 1: any seek > 3ms
		trackmove_time_ms = 5;
		state_timeout.wait_ms(trackmove_time_ms); // must be > 0!
		DEBUG("Seek: head switch to %d", head);
		return;
	}

	// cylinder changes, velocity mode
	trackmove_time_ms = calcperiod_ms; // default: max movement

	if (seek_destination_cylinder > cylinder) {
		DEBUG("drive seeking outward, cyl = %d", cylinder);
		cylinder += trackmove_increment;
		if (cylinder >= seek_destination_cylinder) {
			// seek head outward finished
			// proportionally reduced seek time
			cylinder = seek_destination_cylinder;
			trackmove_time_ms = calcperiod_ms * (seek_destination_cylinder - cylinder)
					/ trackmove_increment;
			DEBUG("drive seek outwards complete, cyl = %d", cylinder);
			// DEBUG("Seek: trackmove_time_ms =%d", trackmove_time_ms);
			state_timeout.wait_ms(trackmove_time_ms);
			change_state(RL0102_STATE_lock_on);
		} else
			state_timeout.wait_ms(trackmove_time_ms);
	} else {
		// seek head inwards
		if ((cylinder - seek_destination_cylinder) <= trackmove_increment) {
			// proportionally reduced seek time
			trackmove_time_ms = calcperiod_ms * (cylinder - seek_destination_cylinder)
					/ trackmove_increment;
			cylinder = seek_destination_cylinder;
			DEBUG("drive seek inwards complete, cyl = %d", cylinder);
			// DEBUG("Seek: trackmove_time_ms =%d", trackmove_time_ms);
			state_timeout.wait_ms(trackmove_time_ms);
			change_state(RL0102_STATE_lock_on);
			return;
		} else {
			DEBUG("drive seeking inwards, cyl = %d", cylinder);
			cylinder -= trackmove_increment;
			state_timeout.wait_ms(trackmove_time_ms);
		}
	}
}

void RL0102_c::state_lock_on() {

	if (runstop_button.value == false || fault_lamp.value == true) { // stop spinning
		change_state(RL0102_STATE_unload_heads);
		return;
	}

	// drive_ready_line = true;
	update_status_word(/*drive_ready_line*/true, drive_error_line);

	load_lamp.value = 0;
	ready_lamp.value = 1;
	writeprotect_lamp.value = writeprotect_button.value || image_is_readonly();

	// fast polling, if ZRLI tests time of 0 cly seek with head switch
	state_timeout.wait_ms(1);
//	state_wait_ms = 100 ;
}

void RL0102_c::state_unload_heads() {
	drive_ready_line = false;
	state_timeout.wait_ms(time_heads_out_ms);
	change_state(RL0102_STATE_spin_down);
}

void RL0102_c::state_spin_down() {
	unsigned calcperiod_ms = 100;
	// change of rpm in 0.1 secs
	unsigned rpm_increment = full_rpm / (time_spinup_sec * (1000 / calcperiod_ms));
	rpm_increment *= emulation_speed.value;

	// drive_ready_line = false; // verified
	update_status_word(/*drive_ready_line*/false, drive_error_line);

	INFO("Spin down drive speed = %d", rotation_umin.value);

	if (rotation_umin.value <= rpm_increment) {
		rotation_umin.value = 0;
		change_state(RL0102_STATE_load_cartridge);
		return;
	} else
		rotation_umin.value -= rpm_increment;

	load_lamp.value = 0;
	ready_lamp.value = 0;
	writeprotect_lamp.value = writeprotect_button.value || image_is_readonly();
	image_filepath.readonly = true; // "door locked", disk can not be changed

	state_timeout.wait_ms(calcperiod_ms);
}

// clear volatile error conditions in status word
void RL0102_c::clear_error_register(void) {
	error_wge = false;
	volume_check = false;
	update_status_word(drive_ready_line, /*drive_error_line*/false);
}

// return drive status word for controller MP registers
void RL0102_c::update_status_word(bool new_drive_ready_line, bool new_drive_error_line) {

	uint16_t tmp = 0;
	if (state.value != RL0102_STATE_power_off)
		tmp |= state.value;
	if (state.value != RL0102_STATE_brush_cycle)
		tmp |= RL0102_STATUS_BH; // brush home
	if (state.value == RL0102_STATE_load_heads || state.value == RL0102_STATE_seek
			|| state.value == RL0102_STATE_lock_on)
		tmp |= RL0102_STATUS_HO; // heads out
	if (cover_open.value == true)
		tmp |= RL0102_STATUS_CO;
	if (head == 1) // which head is selected after last seek/read/write?
		tmp |= RL0102_STATUS_HS;
	if (drivetype == 2)
		tmp |= RL0102_STATUS_DT; // rl02
	/* OPI on RL11 controller
	 if (state.value == RL0102_STATE_power_off) {
	 tmp |= RL0102_STATUS_DSE; // drive select
	 drive_error_line = true;
	 }
	 */
	if (volume_check) {
		tmp |= RL0102_STATUS_VC;
		new_drive_error_line = true; // VC is error, tested on real RL02
	}
	if (error_wge) {
		tmp |= RL0102_STATUS_WGE; // write not possible
		new_drive_error_line = true;
	}
	if (image_is_readonly() || writeprotect_button.value == true) {
		// writeprotect_lamp.value = true ; not here!!
		tmp |= RL0102_STATUS_WL;
	}

	// notify the RL11 CSR?
	if (new_drive_ready_line != drive_ready_line || new_drive_error_line != drive_error_line
			|| tmp != status_word) {
		drive_ready_line = new_drive_ready_line;
		drive_error_line = new_drive_error_line;
		status_word = tmp;
		controller->on_drive_status_changed(this);
	}
}

// update, if neither error nor ready changed
void RL0102_c::update_status_word(void) {
	update_status_word(drive_ready_line, drive_error_line);
}

// is sector with given header on current track?
bool RL0102_c::header_on_track(uint16_t header) {
	// fields of disk address word (read/write data, read header)
	unsigned header_cyl = (header >> 7) & 0x1ff; // bits <15:7>
	unsigned header_hd = (header >> 6) & 0x01; // bit 6
	unsigned header_sec = header & 0x03f; // bit <5:0>
	if (header_cyl != cylinder || header_hd != head || header_sec >= 40)
		return false;
	else
		return true;
}

/*
 simulate an alternating stream of sector_headers and sector_data on the rotating platter.
 sector_segment_under_heads counts 0..79
 if even: sector header under head
 if odd: data under heads
 time for one sector on platter:
 1 rotation = 40 sectors = (60/2400)= 25ms
 1 sector (header+data( = 25ms/40 = 625 us.
 */

#define NEXT_SECTOR_SEGMENT_ADVANCE do {	\
	next_sector_segment_under_heads = (next_sector_segment_under_heads + 1) % 80 ;	\
} while(0)

#ifdef OLD
#define NEXT_SECTOR_SEGMENT_ADVANCE do {	\
 	next_sector_segment_under_heads = (next_sector_segment_under_heads + 1) % 80 ;	\
 	if (next_sector_segment_under_heads & 1)		\
 		/* time to pass one sector 600 data, 25 header? */			\
 		rotational_timeout.wait_us(600) ;		\
 		else rotational_timeout.wait_us(25) ;		\
 } while(0)
#endif

// read next sector header from rotating platter
// then increments "sector_segment_under_heads" to next data
// sector header has the format
// 3 words: diskaddress, 0x0000, CRC
// samples from real RL02: cyl=0,head. each header(=sector) and crc
// (3,042000);(4,030001);(6,104000);(7,072001);(16,164002);(17,012003);(24,170005);
// (25,006004);(26,044004);(27,132005);(31,056007);(33,162006);(34,110007);
// (37,152007);(40,140013);(43,102013);(44,170012);(46,044013)
bool RL0102_c::cmd_read_next_sector_header(uint16_t *buffer, unsigned buffer_size_words) {
	if (state.value != RL0102_STATE_lock_on)
		return false; // wrong state

	assert(buffer_size_words >= 3);

	if (next_sector_segment_under_heads & 1) {
		// odd: next is data, let it pass the head
		NEXT_SECTOR_SEGMENT_ADVANCE
		;
		// nanosleep() for rotational delay?
	}

	unsigned sectorno = next_sector_segment_under_heads >> 1; // LSB is header/data phase

	// bits<0:5>=sector, bit<6>=head, bit<7:15>=cylinder
	assert(cylinder < 512);
	assert(head < 2);
	assert(sectorno < 40);
	// fill 3 word header
	buffer[0] = (cylinder << 7) | (head << 6) | sectorno;
	buffer[1] = 0x0000;
	buffer[2] = calc_crc(2, &buffer[0]); // header CRC

	// circular advance to next header: 40x headers, 40x data
	NEXT_SECTOR_SEGMENT_ADVANCE
	;
	// nanosleep() for rotational delay?
	return true;
}

// read next data block from rotating platter
// then increments "sector_segment_under_heads" to next header
// controller must address sector by waiting for it with cmd_read_next_sector_header()
bool RL0102_c::cmd_read_next_sector_data(uint16_t *buffer, unsigned buffer_size_words) {
	if (state.value != RL0102_STATE_lock_on)
		return false; // wrong state

	assert(buffer_size_words * 2 >= sector_size_bytes);

	if (!(next_sector_segment_under_heads & 1)) {
		// even: next segment is header, let it pass the head
		NEXT_SECTOR_SEGMENT_ADVANCE
		;
		// nanosleep() for rotational delay?
	}
	unsigned sectorno = next_sector_segment_under_heads >> 1; // LSB is header/data phase
	unsigned track_size_bytes = sector_count * sector_size_bytes;
	uint64_t offset = (uint64_t) (head_count * cylinder + head) * track_size_bytes
			+ sectorno * sector_size_bytes;

	// access image file
	// LSB saved before MSB -> word/byte conversion on ARM (little endian) is easy
	image_read((uint8_t *) buffer, offset, sector_size_bytes);
	DEBUG("File Read 0x%x words from c/h/s=%d/%d/%d, file pos=0x%llx, words = %06o, %06o, ...",
			sector_size_bytes / 2, cylinder, head, sectorno, offset, (unsigned )(buffer[0]),
			(unsigned )(buffer[1]));

	// circular advance to next header: 40x headers, 40x data
	NEXT_SECTOR_SEGMENT_ADVANCE
	;
	// nanosleep() for rotational delay?
	return true;
}

// write data for current sector under head
// then increments "stuff_under_heads" to next header
// controller must address sector by waiting for it with cmd_read_next_sector_header()
bool RL0102_c::cmd_write_next_sector_data(uint16_t *buffer, unsigned buffer_size_words) {
	if (state.value != RL0102_STATE_lock_on)
		return false; // wrong state

	assert(buffer_size_words * 2 >= sector_size_bytes);

	// error: write can not be executed, different reasons
	if (image_is_readonly() || writeprotect_button.value == true || !drive_ready_line) {
		error_wge = true;
		update_status_word();
		return true; // function did not fail, WGE is valid result
	}
	error_wge = false; // can read

	if (!(next_sector_segment_under_heads & 1)) {
		// even: next segment is header, let it pass the head
		NEXT_SECTOR_SEGMENT_ADVANCE
		;
		// nanosleep() for rotational delay?
	}
	unsigned sectorno = next_sector_segment_under_heads >> 1; // LSB is header/data phase
	unsigned track_size_bytes = sector_count * sector_size_bytes;
	uint64_t offset = (uint64_t) (head_count * cylinder + head) * track_size_bytes
			+ sectorno * sector_size_bytes;

	// access image file
	// LSB saved before MSB -> word/byte conversion on ARM (little endian) is easy
	image_write((uint8_t *) buffer, offset, sector_size_bytes);
	DEBUG("File Write 0x%x words from c/h/s=%d/%d/%d, file pos=0x%llx, words = %06o, %06o, ...",
			sector_size_bytes / 2, cylinder, head, sectorno, offset, (unsigned )(buffer[0]),
			(unsigned )(buffer[1]));

	// circular advance to next header: 40x headers, 40x data
	NEXT_SECTOR_SEGMENT_ADVANCE
	;
	// nanosleep() for rotational delay?

	return true;
}

// thread
void RL0102_c::worker(unsigned instance) {
	UNUSED(instance); // only one
	timeout_c timeout;

	// set prio to RT, but less than RL11 controller
	worker_init_realtime_priority(rt_device);

	while (!workers_terminate) {
//		worker_mutex.lock() ; // collision with cmd_seek() and on_xxx_changed()
		// states have set error flags not in RL11 CSR: just update
		update_status_word(drive_ready_line, drive_error_line);

		// global stuff for all states
		if (enabled.value && (!controller || !controller->enabled.value))
			// RL drive powered, but no controller: no clock -> FAULT
			fault_lamp.value = true;

		if (power_switch.value == false)
			change_state(RL0102_STATE_power_off);

		switch (state.value) {
		case RL0102_STATE_power_off:
			state_power_off();
			break;
		case RL0102_STATE_load_cartridge:
			state_load_cartridge();
			break;
		case RL0102_STATE_spin_up:
			state_spin_up();
			break;
		case RL0102_STATE_brush_cycle:
			state_brush_cycle();
			break;
		case RL0102_STATE_load_heads:
			state_load_heads();
			break;
		case RL0102_STATE_seek:
			state_seek();
			break;
		case RL0102_STATE_lock_on:
			state_lock_on();
			break;
		case RL0102_STATE_unload_heads:
			state_unload_heads();
			break;
		case RL0102_STATE_spin_down:
			state_spin_down();
			break;
		}
//	worker_mutex.unlock() ;

	}
}

