/* 
 rk05.cpp: implementation of RK05 disk drive, attached to RK11D controller

 Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
 Contributed under the BSD 2-clause license.

 */

#include <assert.h>

using namespace std;

#include "logger.hpp"
#include "utils.hpp"
#include "timeout.hpp"
#include "rk11.hpp"
#include "rk05.hpp"

rk05_c::rk05_c(storagecontroller_c *controller) :
		storagedrive_c(controller), _current_cylinder(0), _seek_count(0), _sectorCount(0), _wps(
		false), _rwsrdy(true), _dry(false), _sok(false), _sin(false), _dru(false), _rk05(
		true), _dpl(false), _scp(false) {
	name.value = "RK05";
	type_name.value = "RK05";
	log_label = "RK05";
	_geometry.Cylinders = 203;   // Standard RK05
	_geometry.Heads = 2;
	_geometry.Sectors = 12;
	_geometry.Sector_Size_Bytes = 512;
}

//
// Status registers
//

uint32_t rk05_c::get_sector_counter(void) {
	return _sectorCount;
}

bool rk05_c::get_write_protect(void) {
	return _wps;
}

bool rk05_c::get_rws_ready(void) {
	return _rwsrdy;
}

bool rk05_c::get_drive_ready(void) {
	return _dry;
}

bool rk05_c::get_sector_counter_ok(void) {
	return _sok;
}

bool rk05_c::get_seek_incomplete(void) {
	return _sin;
}

bool rk05_c::get_drive_unsafe(void) {
	return _dru;
}

bool rk05_c::get_rk05_disk_online(void) {
	return _rk05;
}

bool rk05_c::get_drive_power_low(void) {
	return _dpl;
}

bool rk05_c::get_search_complete(void) {
	bool scp = _scp;
	_scp = false;
	return scp;
}

bool rk05_c::on_param_changed(parameter_c *param) {
	if (param == &enabled) {
		if (!enabled.new_value) {
			// disable switches power OFF.
			drive_reset();
		}
	} else if (&image_filepath == param) {
		if (image_open(image_filepath.new_value, true)) {
			_dry = true;
			controller->on_drive_status_changed(this);
			image_filepath.value = image_filepath.new_value;
			return true;
		}
	} 
	return storagedrive_c::on_param_changed(param); // more actions (for enable)
}

//
// Reset / Power handlers
//

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void rk05_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	UNUSED(aclo_edge) ;
// called at high priority.
	if (dclo_edge == SIGNAL_EDGE_RAISING) {
		// power-on defaults
		drive_reset();
	}
}

void rk05_c::on_init_changed(void) {
// called at high priority.

	if (init_asserted) {
		drive_reset();
	}
}

//
// Disk actions (read/write/seek/reset)
//

void rk05_c::read_sector(uint32_t cylinder, uint32_t surface, uint32_t sector,
		uint16_t* out_buffer) {
	assert(cylinder < _geometry.Cylinders);
	assert(surface < _geometry.Heads);
	assert(sector < _geometry.Sectors);

	_current_cylinder = cylinder;

// SCP is cleared at the start of any function.
	_scp = false;

//
// reset Read/Write/Seek Ready flag while we do this operation
//
	_rwsrdy = false;
	controller->on_drive_status_changed(this);

	timeout_c delay;

// Delay for seek / read.
// TODO: maybe base this on real drive specs.
	delay.wait_ms(10);

// Read the sector into the buffer passed to us.
	image_read(reinterpret_cast<uint8_t*>(out_buffer),
			get_disk_byte_offset(cylinder, surface, sector), _geometry.Sector_Size_Bytes);

// Set RWS ready now that we're done.
	_rwsrdy = true;

	controller->on_drive_status_changed(this);
}

void rk05_c::write_sector(uint32_t cylinder, uint32_t surface, uint32_t sector,
		uint16_t* in_buffer) {
	assert(cylinder < _geometry.Cylinders);
	assert(surface < _geometry.Heads);
	assert(sector < _geometry.Sectors);

	_current_cylinder = cylinder;

// SCP is cleared at the start of any function.
	_scp = false;

//
// reset Read/Write/Seek Ready flag while we do this operation
//
	_rwsrdy = false;
	controller->on_drive_status_changed(this);

	timeout_c delay;

// Delay for seek / read.
// TODO: maybe base this on real drive specs.
	delay.wait_ms(10);

// Read the sector into the buffer passed to us.
	image_write(reinterpret_cast<uint8_t*>(in_buffer),
			get_disk_byte_offset(cylinder, surface, sector), _geometry.Sector_Size_Bytes);

// Set RWS ready now that we're done.
	_rwsrdy = true;

	controller->on_drive_status_changed(this);
}

void rk05_c::seek(uint32_t cylinder) {
	assert(cylinder < _geometry.Cylinders);

	_seek_count = abs((int32_t) _current_cylinder - (int32_t) cylinder) + 1;
	_current_cylinder = cylinder;

	if (_seek_count > 0) {
		// We'll be busy for awhile:
		_rwsrdy = false;
		_scp = false;
	} else {
		_rwsrdy = true;
		_scp = true;
	}
	controller->on_drive_status_changed(this);
}

void rk05_c::set_write_protect(bool protect) {
	UNUSED(protect);

// Not implemented at the moment.
	_scp = false;
}

void rk05_c::drive_reset(void) {
//
// "The controller directs the selected disk drive to move its
//  head mechanism to cylinder address 000 and reset all active
//  error status lines."
//
// This is basically the same as a seek to cylinder 0 plus
// a reset of error status.
//
	_sin = false;
	_dru = false;
	_dpl = false;
	controller->on_drive_status_changed(this);

	seek(0);
// SCP change will be posted when the seek instigated above is completed.
}

void rk05_c::worker(unsigned instance) {
	UNUSED(instance) ; // only one
	timeout_c timeout;

	while (true) {
		if (_seek_count > 0) {
			// A seek is active.  Wait at least 10ms and decrement
			// The seek count by a certain amount.  This is completely fudged.
			timeout.wait_ms(3);
			_seek_count -= 25;
			// since simultaneous interrupts
			// confuse me right now

			if (_seek_count < 0) {
				// Out of seeks to do, let the controller know we're done.
				_scp = true;
				controller->on_drive_status_changed(this);

				// Set RWSRDY only after posting status change / interrupt...
				_rwsrdy = true;
			}
		} else {
			// Move SectorCounter to next sector
			// every 1/300th of a second (or so).
			// (1600 revs/min = 25 revs / sec = 300 sectors / sec)
			timeout.wait_ms(3);
			if (image_is_open()) {
				_sectorCount = (_sectorCount + 1) % 12;
				_sok = true;
				controller->on_drive_status_changed(this);
			}
		}
	}
}

uint64_t rk05_c::get_disk_byte_offset(uint32_t cylinder, uint32_t surface, uint32_t sector) {
	return _geometry.Sector_Size_Bytes
			* ((cylinder * _geometry.Heads * _geometry.Sectors) + (surface * _geometry.Sectors)
					+ sector);
}
