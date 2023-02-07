/*
 mscp_drive.hpp: Implementation of MSCP drive, used with MSCP controller.

 Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
 Contributed under the BSD 2-clause license.

 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <memory>	// unique_ptr
#include "parameter.hpp"
#include "storagedrive.hpp"

//
// Implements the backing store for MSCP disk images
//
class mscp_drive_c: public storagedrive_c 
{
public:
    mscp_drive_c(storagecontroller_c *controller, uint32_t driveNumber);
    ~mscp_drive_c(void);

    bool on_param_changed(parameter_c *param) override;

    uint32_t GetBlockSize(void);
    uint32_t GetBlockCount(void);
    uint32_t GetRCTBlockCount(void);
    uint32_t GetMediaID(void);
    uint32_t GetDeviceNumber(void);
    uint16_t GetClassModel(void);
    uint16_t GetRCTSize(void);
    uint8_t GetRBNs(void);
    uint8_t GetRCTCopies(void);

    void SetOnline(void);
    void SetOffline(void);bool IsOnline(void);bool IsAvailable(void);

    void Write(uint32_t blockNumber, size_t lengthInBytes, uint8_t* buffer);

    uint8_t* Read(uint32_t blockNumber, size_t lengthInBytes);

    void WriteRCTBlock(uint32_t rctBlockNumber, uint8_t* buffer);

    uint8_t* ReadRCTBlock(uint32_t rctBlockNumber);

public:
    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;

public:
    parameter_bool_c use_image_size = parameter_bool_c(this, "useimagesize", "uis", false,
        "Determine unit size from image file instead of drive type");

private:

    struct DriveInfo 
    {
	    enum drive_type_e Type ;
        char TypeName[16];
        size_t BlockCount;
        uint32_t MediaID;
        uint8_t Model;
        uint16_t RCTSize;bool Removable;bool ReadOnly;
    };

    //
    // TODO: add a lot more drive types.
    // Does it make sense to support drive types not native to QBus/Unibus machines (SCSI types, for example?)
    // Need to add a ClassID table entry in that eventuality...
    // Also TODO: RCTSize parameters taken from SIMH rq source; how valid are these?
    DriveInfo g_driveTable[21] {
        //    Name     Blocks    MediaID     Model  RCTSize  Removable  ReadOnly
	   { drive_type_e::RX50, "RX50",   800,      0x25658032, 7,     0,       true,      false }, 
           { drive_type_e::RX33, "RX33",   2400,     0x25658021, 10,    0,       true,      false },
           { drive_type_e::RD51, "RD51",   21600,    0x25644033, 6,     36,      false,     false }, 
           { drive_type_e::RD31, "RD31",   41560,    0x2564401f, 12,    3,       false,     false }, 
           { drive_type_e::RC25, "RC25",   50902,    0x20643019, 2,     0,       true,      false },
           { drive_type_e::RC25F,"RC25F",  50902,    0x20643319, 3,     0,       true,      false }, 
           { drive_type_e::RD52, "RD52",   60480,    0x25644034, 8,     4,       false,     false }, 
           { drive_type_e::RD32, "RD32",   83236,    0x25641047, 15,    4,       false,     false },
           { drive_type_e::RD53, "RD53",   138672,   0x25644035, 9,     5,       false,     false },
           { drive_type_e::RA80, "RA80",   237212,   0x20643019, 1,     0,       false,     false }, 
           { drive_type_e::RD54, "RD54",   311200,   0x25644036, 13,    7,       false,     false }, 
           { drive_type_e::RA60, "RA60",   400176,   0x22a4103c, 4,     1008,    true,      false },
           { drive_type_e::RA70, "RA70",   547041,   0x20643019, 18,    198,     false,     false }, 
           { drive_type_e::RA81, "RA81",   891072,   0x25641051, 5,     2856,    false,     false }, 
           { drive_type_e::RA82, "RA82",   1216665,  0x25641052, 11,    3420,    false,     false }, 
           { drive_type_e::RA71, "RA71",   1367310,  0x25641047, 40,    1428,    false,     false },
           { drive_type_e::RA72, "RA72",   1953300,  0x25641048, 37,    2040,    false,     false }, 
           { drive_type_e::RA90, "RA90",   2376153,  0x2564105a, 19,    1794,    false,     false }, 
           { drive_type_e::RA92, "RA92",   2940951,  0x2564105c, 29,    949,     false,     false },
           { drive_type_e::RA73, "RA73",   3920490,  0x25641049, 47,    198,     false,     false },
           { drive_type_e::NONE, "", 0, 0, 0, 0, false, false } };

    bool SetDriveType(const char* typeName);
    void UpdateCapacity(void);
    void UpdateMetadata(void);
    DriveInfo _driveInfo;bool _online;
    uint32_t _unitDeviceNumber;
    uint16_t _unitClassModel;
    bool _useImageSize;

    //
    // RCT ("Replacement and Caching Table") data:
    // The size of this area varies depending on the drive.  This is
    // provided only to appease software that expects the RCT to exist --
    // since there will never be any bad sectors in our disk images
    // there is no other purpose.
    // This data is not persisted to disk as it is unnecessary.
    //
    std::unique_ptr<uint8_t> _rctData;
};
