/* 
    rk05.hpp: implementation of RK05 disk drive, used with RK11D controller

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Contributed under the BSD 2-clause license.

*/
#ifndef _RK05_HPP_
#define _RK05_HPP_

#include <stdint.h>
#include <string.h>

#include "storagedrive.hpp"
#include "rk11.hpp"

enum DriveType
{
    RK05 = 0,
    RK05f = 1,
};

struct Geometry
{
    uint32_t Cylinders;
    uint32_t Heads;
    uint32_t Sectors;
    uint32_t Sector_Size_Bytes;
    uint32_t Sector_Size_Words;
};

class rk05_c: public storagedrive_c 
{
private:
        // Drive geometry details
        Geometry _geometry;

        // Current position of the heads 
        volatile uint32_t _current_cylinder;
        volatile int32_t _seek_count;
     
        // Current sector under the heads (used to satisfy RKDS register,
        // incremented by worker thread, unrelated to sector reads/writes)
        volatile uint32_t _sectorCount;

        // Status bits
        volatile bool _wps;          // Write Protect status
        volatile bool _rwsrdy;       // Indicates that the drive is ready to accept a new function.
        volatile bool _dry;          // Indicates that the drive is powered, loaded, running, rotating, and not unsafe.
        volatile bool _sok;          // Indicates that the _sectorCount value is not in a state of flux.
        volatile bool _sin;          // Indicates that the seek could not be completed.
        volatile bool _dru;          // Indicates that an unusual condition has occurred in the disk drive and is unsafe.
        volatile bool _rk05;         // Always set, identifies the drive as an RK05
        volatile bool _dpl;          // Set when an attempt to initiate a new function (or a function is in progress) when power is low.

        volatile bool _scp;          // Indicates the completion of a seek


        uint64_t get_disk_byte_offset(
            uint32_t cylinder,
            uint32_t surface,
            uint32_t sector);

     
public:
        Geometry get_geometry(void);
        uint32_t get_cylinder(void);
 
        // Status bits
        uint32_t get_sector_counter(void);
        bool get_write_protect(void);
        bool get_rws_ready(void);
        bool get_drive_ready(void);
        bool get_sector_counter_ok(void);
        bool get_seek_incomplete(void);
        bool get_drive_unsafe(void);
        bool get_rk05_disk_online(void);
        bool get_drive_power_low(void);

        // Not a status bit per-se, indicates whether a seek has completed since the last status change.
        bool get_search_complete(void);

        // Commands
        void read_sector(uint32_t cylinder, uint32_t surface, uint32_t sector, uint16_t* out_buffer);
        void write_sector(uint32_t cylinder, uint32_t surface, uint32_t sector, uint16_t* in_buffer);
        void seek(uint32_t cylinder);
        void set_write_protect(bool protect);
        void drive_reset(void);
         
public:
	DriveType _drivetype; 

	rk05_c(storagecontroller_c *controller);

    bool on_param_changed(parameter_c* param) override;

	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;

	void on_init_changed(void) override;

	// background worker function
	void worker(unsigned instance) override;
};

#endif
