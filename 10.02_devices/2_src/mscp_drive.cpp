/*
 mscp_drive.cpp: Implementation of MSCP disks.

 Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
 Contributed under the BSD 2-clause license.

 This provides the logic for reads and writes to the data and RCT space
 for a given drive, as well as configuration for different standard DEC
 drive types.

 Disk data is backed by an image file on disk.  RCT data exists only in
 memory and is not saved -- it is provided to satisfy software that
 expects the RCT area to exist.  Since no bad sectors will ever actually
 exist, the RCT area has no real purpose, so it is ephemeral in this
 implementation.
 */

#include <assert.h>
#include <memory>

using namespace std;

#include "logger.hpp"
#include "utils.hpp"
#include "mscp_drive.hpp"
#include "mscp_server.hpp"

mscp_drive_c::mscp_drive_c(storagecontroller_c *controller, uint32_t driveNumber) :
		storagedrive_c(controller), _useImageSize(false) {
	set_workers_count(0) ; // needs no worker()
	log_label = "MSCPD";
	SetDriveType("RA81");
	SetOffline();

	// Calculate the unit's ID:
	_unitDeviceNumber = driveNumber + 1;
}

mscp_drive_c::~mscp_drive_c() {
	if (file_is_open()) {
		file_close();
	}
}

// on_param_changed():
//  Handles configuration parameter changes.
bool mscp_drive_c::on_param_changed(parameter_c *param) {
	// no own "enable" logic
	if (&type_name == param) {
		return SetDriveType(type_name.new_value.c_str());
	} else if (&image_filepath == param) {
		// Try to open the image file.
		if (file_open(image_filepath.new_value, true)) {
			image_filepath.value = image_filepath.new_value;
			UpdateCapacity();
			return true;
		}
		// TODO: if file is a nonstandard size?
	} else if (&use_image_size == param) {
		_useImageSize = use_image_size.new_value;
		use_image_size.value = use_image_size.new_value;
		UpdateCapacity();
		return true;
	} 
	return device_c::on_param_changed(param); // more actions (for enable)false;
}

//
// GetBlockSize():
//  Returns the size, in bytes, of a single block on this drive.
//  This is either 512 or 576 bytes.
//
uint32_t mscp_drive_c::GetBlockSize() {
	//
	// For the time being this is always 512 bytes.
	//
	return 512;
}

//
// GetBlockCount():
//  Get the size of the data space (not including RCT area) of this
//  drive, in blocks.
//
uint32_t mscp_drive_c::GetBlockCount() {
	if (_useImageSize) {
		// Return the image size / Block size (rounding down).
		return file_size() / GetBlockSize();
	} else {
		//
		// Use the size defined by the drive type.
		//
		return _driveInfo.BlockCount;
	}
}

//
// GetRCTBlockCount():
//  Returns the total size of the RCT area in blocks.
//
uint32_t mscp_drive_c::GetRCTBlockCount() {
	return _driveInfo.RCTSize * GetRCTCopies();
}

//
// GetMediaID():
//  Returns the media ID specific to this drive's type.
//
uint32_t mscp_drive_c::GetMediaID() {
	return _driveInfo.MediaID;
}

//
// GetDeviceNumber():
//  Returns the unique device number for this drive.
//
uint32_t mscp_drive_c::GetDeviceNumber() {
	return _unitDeviceNumber;
}

//
// GetClassModel():
//  Returns the class and model information for this drive.
//
uint16_t mscp_drive_c::GetClassModel() {
	return _unitClassModel;
}

//
// GetRCTSize():
//  Returns the size of one copy of the RCT.    
//
uint16_t mscp_drive_c::GetRCTSize() {
	return _driveInfo.RCTSize;
}

//
// GetRBNs():
//  Returns the number of replacement blocks per track for
//  this drive.
//
uint8_t mscp_drive_c::GetRBNs() {
	return 0;
}

//
// GetRCTCopies():
//  Returns the number of copies of the RCT present in the RCT
//  area.
//
uint8_t mscp_drive_c::GetRCTCopies() {
	return 1;
}

//
// IsAvailable():
//  Indicates whether this drive is available (i.e. has an image
//  assigned to it and can thus be used by the controller.)
//
bool mscp_drive_c::IsAvailable() {
	return file_is_open();
}

//
// IsOnline():
//  Indicates whether this drive has been placed into an Online
//  state (for example by the ONLINE command).     
//
bool mscp_drive_c::IsOnline() {
	return _online;
}

//
// SetOnline():
//  Brings the drive online.
//
void mscp_drive_c::SetOnline() {
	_online = true;

	//
	// Once online, the drive's type and image cannot be changed until
	// the drive is offline.
	//
	// type_name.readonly = true;
	// image_filepath.readonly = true;
}

//
// SetOffline():
//  Takes the drive offline.
//
void mscp_drive_c::SetOffline() {
	_online = false;
	type_name.readonly = false;
	image_filepath.readonly = false;
}

//
// Writes the specified number of bytes from the provided buffer,
// starting at the specified logical block.
//
void mscp_drive_c::Write(uint32_t blockNumber, size_t lengthInBytes, uint8_t* buffer) {
	file_write(buffer, blockNumber * GetBlockSize(), lengthInBytes);
}

//
// Reads the specifed number of bytes starting at the specified logical
// block.  Returns a pointer to a buffer containing the data read.
// Caller is responsible for freeing this buffer.
//
uint8_t* mscp_drive_c::Read(uint32_t blockNumber, size_t lengthInBytes) {
	uint8_t* buffer = new uint8_t[lengthInBytes];

	assert(nullptr != buffer);

	file_read(buffer, blockNumber * GetBlockSize(), lengthInBytes);

	return buffer;
}

//
// Writes a single block's worth of data from the provided buffer into the
// RCT area at the specified RCT block.  Buffer must be at least as large 
// as the disk's block size.
//
void mscp_drive_c::WriteRCTBlock(uint32_t rctBlockNumber, uint8_t* buffer) {
	assert(rctBlockNumber < GetRCTBlockCount());

	memcpy(reinterpret_cast<void *>(_rctData.get() + rctBlockNumber * GetBlockSize()),
			reinterpret_cast<void *>(buffer), GetBlockSize());
}

//
// Reads a single block's worth of data from the RCT area (at the specified
// block offset).  Returns a pointer to a buffer containing the data read.
// Caller is responsible for freeing this buffer.
//
uint8_t* mscp_drive_c::ReadRCTBlock(uint32_t rctBlockNumber) {
	assert(rctBlockNumber < GetRCTBlockCount());

	uint8_t* buffer = new uint8_t[GetBlockSize()];
	assert(nullptr != buffer);

	memcpy(reinterpret_cast<void *>(buffer),
			reinterpret_cast<void *>(_rctData.get() + rctBlockNumber * GetBlockSize()),
			GetBlockSize());

	return buffer;
}

//
// UpdateCapacity():
//  Updates the capacity parameter of the drive based on the block count and block size.
//
void mscp_drive_c::UpdateCapacity() {
	capacity.value = GetBlockCount() * GetBlockSize();
}

//
// UpdateMetadata():
//  Updates the Unit Class / Model info and RCT area based on the selected drive type.
//
void mscp_drive_c::UpdateMetadata() {
	_unitClassModel = 0x0200 | _driveInfo.Model;

	// Initialize the RCT area
	size_t rctSize = _driveInfo.RCTSize * GetBlockSize();
	_rctData.reset(new uint8_t[rctSize]);
	assert(_rctData != nullptr);
	memset(reinterpret_cast<void *>(_rctData.get()), 0, rctSize);
}

//
//
// SetDriveType():
//  Updates this drive's type to the specified type (i.e.
//  RA90 or RD54).
//  If the specified type is not found in our list of known
//  drive types, the drive's type is not changed and false
//  is returned.
//
bool mscp_drive_c::SetDriveType(const char* typeName) {
	//
	// Search through drive data table for name,
	// and if valid, set the type appropriately.
	//
	int index = 0;
	while (g_driveTable[index].BlockCount != 0) {
		if (!strcasecmp(typeName, g_driveTable[index].TypeName)) {
			_driveInfo = g_driveTable[index];
			type_name.value = _driveInfo.TypeName;
			UpdateCapacity();
			UpdateMetadata();
			return true;
		}

		index++;
	}

	// Not found
	return false;
}

//
// worker():
//  worker method for this drive.  No work is necessary.
//
//
// on_power_changed():
//  Handle power change notifications.
//
// after UNIBUS install, device is reset by DCLO cycle
void mscp_drive_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	UNUSED(aclo_edge) ;
	UNUSED(dclo_edge) ;
	// Take the drive offline due to power change
	SetOffline();
}

//
// on_init_changed():
//  Handle INIT signal.
void mscp_drive_c::on_init_changed(void) {
	// Take the drive offline due to reset
	SetOffline();
}

