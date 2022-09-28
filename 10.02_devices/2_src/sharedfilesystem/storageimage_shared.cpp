/* sfs_filesystem.cpp - single interface to base of different DEC file systems

  Copyright (c) 2021, Joerg Hoppe
  j_hoppe@t-online.de, www.retrocmp.com

  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  - Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  - Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


  06-mar-2021 JH      transfered from tu58fs, converted to C++

SFS: Shared File System between DEC emulation and Linux host
Content between an emulated DEC disk devices is published
and synced to a Linux file tree.

These classes hold different DEC filesystems with content.
They can be synced
- to Win10/Linux filesystems
- to image files of DEC disk devices.

- 5 different representations
1. directory
2. User file tree under Linux, which is to be mirrored onto a DEC device
3. Event resolver: corrects conflicting changes on host and DEC filesystem
4. Representation of files for DEC OS (different name, different attributes
        even multiple file streams for a single file
5. DEC file tree encoded to a binary device image
   (memory, physical tmp file)
   This is accessed and changed by emulated DEC disks.
6. NOT IMPLEMENTED:
   device image on SDcard.


[1]               [2]               [3]           [4]                        [5]                	[6]
Linux/Win10       Host filesystem   Event         DEC filesystem             temp DEC binary image  Host emulator
filesystem                          resolver      (XXDP, RT11, Files-11   )   (RX012,RK05,RL02,MSCP) image file
on disk           root                            root
                     |                               |                        temporary
"root/"              +---                            +---                     imagestream
"root/dir1"         dir1-.                          dir1-.                    (list of blocks)   Mounted image file
"root/dir1/file1/"   |   +file1-                     |   +file1                                  (list of blocks)
"root/dir1/file1/"   |   `file2-                     |   `file2
"root/file3"         +file3-                        +file3
"root/file4"         +file4-                        +file4

                             thread
                           -- sync()-->                         --render()-->       <--update on write
                           <--sync()---                         <--parse()--        --> update on change




- The set of files and data changes on two sides:
  > in the emulator image file (DEC emulation creating/reading/writing/deleting files) [5]
  > in the shared host file tree (user reading/writing/deleting files under Linux) [1]
  IN ANY CASE changes of the DEC emulation have higher priority then host user file system.
- two internal stages do actual conversion
   > an intern logical representation of the DEC filesystem
   > an internal temporary device image used for assembly [4]
- DEC files hold data in streams (memory copy or link to host file)
- device image is also a stream (memory or file)
- when updating the emulator image[5] with the internal filesystemimage [4],
  number of block writes should be optimized for SDcard based systems.

  Communication between filesystem classes via event flow.

  - Exchange of events: key=hostfilepath, operand = create/delete, data = file ptr (unless delete)
  - host filesystem changes via inotify, DEC filesystem changes via snaphsot<->current compare
  - host and DEC fs each *produce* events on file creation or deletione,
    and *consume* events, as change commands.
  - An update of an existing file or directory is translated to a pair of
	  delete + create events.
  - Events are evaluated (consumed) by the other filesystem.
     Files are there deleted or created accordingly.
  - This causes further change events on the consumer side, called "ack-events".
    They are *ignored* and not sent back to the producer.
  - Each consumer ignores create events for existing files, and deleted events for missing files.
    This also ignores ack-events.
  - ack-event filter for each filesystem:
  	* consume of an event marks the file in an "ack_event_filter"
  	* following changes on disk generate events, but not for files in "ack_event_filter"
  	* main logic deletes the ack_event_filter after all remaining events are processed


  Flow:
  Change on host filesystem:
  [1] change-> inotify -> event -> update [2] -> save in resolver [3]
  Change on DEC image:
  parse image -> compare prev and current -> event -> save in resolver [3]

  Filenames:
  DEC and host have different rules for valid filenames.
  For example RT11 and XXDP encode string with RAD50 and have a limited character set.
  Filenames change on copy between filesystems. Example
  Operation		host					DEC RT11
  sync DEC		aaa_read_me.txt.bak  -> AAAREA.BAK
  update								AAAREA.BAK changed
  sync host		AAAREA.BAK			<-  AAAREA.BAK ???
  				aaa_read_me.txt.bak				   ??? now 2 files on host
 To solve this, the host_path ("aaa_read_me.txt.bak") is kept on the DEC side too.

  Detecting changes in the DEC filesystem
  On the host side their are inotify events on file system changes.
  On the DEC side, change events are generated as follows:
  Image blocks and filesystem files both have a "changed" flag.
  After parsing, the relation between image blocks and logical files is known.
  Changes in file & directory structure are monitored by comparing
  a "curretn" parse with a "previous" parse.
  Detail algorithm:  endless loop performs:
  1. Parse image to "current" filesystem, all block_change and file_change flags reset.
  2. PDP11 works a while no image, creates, modifies and erases blocks/files.
    write() on image sets block_change_flags

	If any change to the image (by write()):

  3. Analyze "current" for changed file content
    calc_file_change_flags()
    change flags of image blocks => change flags for files
  4. swap "previous" and "current" filesystem.
    reparse "current"
    "previous" now contains state on host with file change flags updated
    "current" contains new state, without change flags
  5. Analyze for created, modified or dletet files
    Compare "previous" with "current":
    5.1 files only in "current" -> file event "create"
    5.2 files only in "previous" -> "file event "delete"
    5.3 files both in "current" and "previous",
     but size or timestamp different or any file "change" flag
    file events "change"
  6. eval list of file events by createing or deleting files on host
    execute "change" event as "delete on host", then "create on host"

  Change of "internal" files:
  bootblock + monitor: have image blocks assigned, like all other files.
    Test: only on RT11 INIT monitor and boot are updated
  volume info: always changed when any other image block changed.
  (new file count, new directory layout, etc)

 Critical szenario:
 PDP11 very fast deletes and recreates a file
  -> no change in directory struct.
  -> in step 3. block change flags for new file erroneously interpreted
     for another moved file
     or seem to hit previous "empty space"
  - but change of file recognized in 5.3
  - so only error: change event for an additonal wrong file.

   */
#include <stdio.h>
#include <inttypes.h>

#include <unistd.h>

#include <cstring>
#include <cassert>
#include <queue>

#include "logger.hpp"
#include "utils.hpp"
#include "timeout.hpp"

#include "storageimage_shared.hpp"
#include "filesystem_dec.hpp"
#include "filesystem_xxdp.hpp"
#include "filesystem_rt11.hpp"


namespace sharedfilesystem {

// just save some parameters, open() does the main job.
storageimage_shared_c::storageimage_shared_c(
    string _image_path,
    bool _use_syncer_thread,
    enum filesystem_type_e _filesystem_type,
    enum dec_drive_type_e _drive_type,
    unsigned _drive_unit,
    uint64_t _capacity,
    string _hostdir) : storageimage_base_c()
{
    use_syncer_thread = _use_syncer_thread ;
    pthread_mutex_init(&mutex, NULL);
    image_path = _image_path ;
    type = _filesystem_type ;
    drive_info = drive_info_c(_drive_type);
    if (_capacity > 0)
        drive_info.capacity = _capacity ; // update from device emulation
    assert((_capacity % 256) == 0) ; // ad hoc, necessary?
    this->drive_unit = _drive_unit ;


//    assert(image_data_size <= raw_drive_size) ; // filesystem must not exceed drive

    host_shared_rootdir = absolute_path(&_hostdir) ;

    filesystem_dec_metadata_snapshot = nullptr ;
    filesystem_dec = nullptr ;
    filesystem_host = nullptr ;

    image = nullptr ; // lives between	open() and close()
}


storageimage_shared_c::~storageimage_shared_c()
{
    // handle recreation via param change with open images
    close() ; // deallocate memorydata and bool array
    pthread_mutex_destroy(&mutex) ;
}

void storageimage_shared_c::lock()
{
    pthread_mutex_lock(&mutex);
}

void storageimage_shared_c::unlock()
{
    pthread_mutex_unlock(&mutex);
}

// create the memory buffer, parse hostdir, create file system in memory buffer
// called by derived open() first
bool storageimage_shared_c::open(bool create)
{
    UNUSED(create) ;
    assert(! is_open()) ; // test the caller

    assert(filesystem_dec_metadata_snapshot == nullptr) ;
    assert(filesystem_dec == nullptr) ;
    assert(filesystem_host == nullptr) ;

    assert(!image_path.empty()) ;

    lock() ;

    image  = new storageimage_binfile_c(image_path);
//    image = new storageimage_memory_c(drive_info.capacity);

    if (!image->open(false)) {
        delete image ;
        image = nullptr ;
        unlock() ;
        // image not opened, if filesystem invalid or dir not set
        return false ;
    }


    // the file system occupies only part of the image:
    // from start until the bad sector table.
    // we call that area a "partition"
    uint64_t image_partition_size;
    if (drive_info.bad_sector_file_offset)
        image_partition_size = drive_info.bad_sector_file_offset ;
    else image_partition_size =  image->size();

    if (type == sharedfilesystem::fst_xxdp) {
        filesystem_dec_metadata_snapshot = new filesystem_xxdp_c( drive_info, image, image_partition_size) ;
        filesystem_dec = new filesystem_xxdp_c( drive_info, image, image_partition_size) ;
    } else if (type == sharedfilesystem::fst_rt11) {
        filesystem_dec_metadata_snapshot = new filesystem_rt11_c(drive_info, image, image_partition_size) ;
        filesystem_dec = new filesystem_rt11_c(drive_info, image, image_partition_size) ;
    } else {
        delete image ;
        image = nullptr ;
        unlock() ;
        // image not opened, if filesystem invalid or dir not set
        return false ;
    }
    filesystem_dec->readonly = readonly ;
    filesystem_dec->log_level_ptr = log_level_ptr ; // same level as image
    filesystem_dec->event_queue.log_level_ptr = log_level_ptr ;
    filesystem_dec_metadata_snapshot->log_level_ptr = log_level_ptr ;

    // write empty bad sector files to some disks
    image_write_std144_bad_sector_table() ;

    readonly = false ;

//image_save_to_disk("/tmp/sync_worker.dump") ;
//				filesystem_dec->debug_print() ;

    // init host directory, create if not exists
    if (! file_exists(&host_shared_rootdir)) {
        INFO(printf_to_cstr("Creating shared directory %s", host_shared_rootdir.c_str())) ;
        system(string("mkdir -p " + host_shared_rootdir).c_str()) ;
    }
    if (! file_exists(&host_shared_rootdir))
        FATAL(printf_to_cstr("Shared directory %s could not be created!", host_shared_rootdir.c_str())) ;
    filesystem_host = new filesystem_host_c(host_shared_rootdir) ;
    filesystem_host->log_level_ptr = log_level_ptr ; // same level as image
    filesystem_host->event_queue.log_level_ptr = log_level_ptr ;

    // initial synchronisation:
    bool init_from_host=false ;
    if (init_from_host) {
        // produce 1st image with empty file system
        filesystem_dec->render() ;
        dec_image_changed = false ;
        filesystem_host->parse() ;
        // filesystem_host->event_queue now initially filled
        // host shared dir initializes DEC filesystem and image
        sync_host_shared_dir_to_filesystem_and_events() ;
        sync_host_filesystem_events_to_dec() ;
        filesystem_host->changed = false ; // events produced -> changes processed
        sync_dec_snapshot() ; // init snapshot
    } else {
        // DEC filesystem initializes host shared dir
        filesystem_host->clear_rootdir() ; // delete internal tree, if any
        filesystem_host->clear_disk_dir() ; // delete all files in shared dir

        sync_dec_image_to_filesystem_and_events() ;
        // snapshot is clear, so all files are created on host
        sync_dec_filesystem_events_to_host() ;

        sync_dec_snapshot() ; // init snapshot
    }


    // start monitor thread
    if (use_syncer_thread) {
        syncer_terminate = false ;
        int status = pthread_create(&syncer_pthread, NULL, &storageimage_shared_syncer_worker_pthread_wrapper, this) ;
        if (status != 0)
            FATAL("Failed to create storageimage_shared_c.syncer_pthread with status = %d", status);
    }
    unlock() ;

    return true ;
}



// Put a DEC STD144 bad_sector table to the image
// on some disks the last track contains a bad sector table,
// see DEC_STD_144.txt
void storageimage_shared_c::image_write_std144_bad_sector_table()
{
    unsigned table_size ; // == sector size
    unsigned table_count ;

    if (drive_info.drive_type == devRL01) {
        table_size = 256 ; // one sector
        table_count = 10 ; // SimH
    } else if (drive_info.drive_type == devRL02) {
        table_size = 256 ; // one sector
        table_count = 10 ; // SimH
        //	}	else if (drive_info.drive_type == devRK067) {
        // complex repeated geometry
        //	table_size = 512 ;
    } else
        return ; // no bad sector table on cartridge

    // work on cached bad_sector file ... avoid many write()
    byte_buffer_c bad_sector_file ;
    get_bytes(&bad_sector_file, drive_info.bad_sector_file_offset, table_count * table_size);
    uint8_t *wp = bad_sector_file.data_ptr(); // start
    // write repeated empty tables
    for (unsigned i=0 ; i < table_count ; i++) {
//        assert((wp+table_size) < image_data_size) ;
        // 	4 words 0: serialno, serial no, reserved, reserved
        for (unsigned j=0 ;  j < 8 ;  j++)
            *wp++ = 0 ;
        // rest of table = ff: no bad sectors
        for (unsigned j=8 ;  j < table_size ;  j++)
            *wp++ = 0xff ;
    }
    set_bytes(&bad_sector_file) ;
}


bool storageimage_shared_c::is_readonly()
{
    return readonly ;
}

bool storageimage_shared_c::is_open(	void)
{
    return (image != nullptr) ;
}

// regsiter an access of the PDP to the binary image
void storageimage_shared_c::image_data_pdp_access(bool changing)
{
    if (changing) {
        dec_image_changed = true ;
        dec_image_change_time_ms = now_ms() ;
    }
}


void storageimage_shared_c::close(void)
{
    if (is_open()) {
        if (use_syncer_thread) {
            syncer_terminate = true ;
            int status = pthread_join(syncer_pthread, NULL) ;
            if (status != 0)
                FATAL("Failed to join with storageimage_shared_c.syncer_pthread with status = %d", status);
        }

        assert(filesystem_dec_metadata_snapshot != nullptr) ;
        delete filesystem_dec_metadata_snapshot;
        filesystem_dec_metadata_snapshot = nullptr ;
        assert(filesystem_dec != nullptr) ;
        delete filesystem_dec;
        filesystem_dec = nullptr ;
        assert(filesystem_host != nullptr) ;
        delete filesystem_host;
        filesystem_host = nullptr ;


//        assert(filesystem_mapper != nullptr) ;
//		delete filesystem_mapper ;
//		filesystem_mapper = nullptr ;

        delete image ;
        image = nullptr ;
    }
}

// set image size to 0
// called on "set media density". Caller (RX01/02) must close() before, and may open() after.
// then image allocation with new size
bool storageimage_shared_c::truncate(void)
{
    assert(! is_open()) ;
    return true ;
}

void storageimage_shared_c::read(uint8_t *buffer, uint64_t position, unsigned len)
{
    if (! is_open()) {
        ERROR("sharedfilesystem::storageimage_shared_c::read(): image %s %d closed", drive_info.device_name.c_str(), drive_unit);
        return ;
    }
    lock();

    image->read(buffer, position, len) ;

    image_data_pdp_access(/*changing*/false) ; // time access for syncer

    unlock();
}


void storageimage_shared_c::write(uint8_t *buffer, uint64_t position, unsigned len)
{
    uint32_t blknr;
    if (! is_open()) {
        ERROR("sharedfilesystem::storageimage_shared_c::write(): image %s %d closed", drive_info.device_name.c_str(), drive_unit);
        return ;
    }
    if (readonly) {
        ERROR("sharedfilesystem::storageimage_shared_c::write(): image %s %d read only", drive_info.device_name.c_str(), drive_unit);
        return ;
    }

    lock();

    image->write(buffer, position, len) ;

    // set dirty
    image_data_pdp_access(/*changing*/true) ;

    // mark all filesystem blocks in range
    unsigned block_size = filesystem_dec->get_block_size(); // alias
    for (blknr = position / block_size; blknr < (position + len) / block_size; blknr++)
        filesystem_dec->changed_blocks->bit_set(blknr);
    // boolarray_print_diag(_this->changedblocks, stderr, _this->block_count, "IMAGE");

    unlock();
    //return count
}


uint64_t storageimage_shared_c::size(void)
{
    assert(is_open()) ;
    return image->size() ;
}


// not really needed, but a storage_image_base_c must implement
void storageimage_shared_c::get_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset, uint32_t data_size)
{
    assert(is_open()) ;
    image->get_bytes(byte_buffer, byte_offset, data_size) ;
}

// not really needed, but a storage_image_base_c must implement
void storageimage_shared_c::set_bytes(byte_buffer_c *byte_buffer)
{
    assert(is_open()) ;
    image->set_bytes(byte_buffer) ;
}

// not really needed, but a storage_image_base_c must implement
void storageimage_shared_c::save_to_file(string host_filename)
{
    assert(is_open()) ;
    image->save_to_file(host_filename)  ;
}



/*** still not clear whats the problem with a std::thread * as member of storeageimage_shared_c ***/
void *storageimage_shared_syncer_worker_pthread_wrapper(void *context)
{
    storageimage_shared_c *storageimage_shared = (storageimage_shared_c *) context;
#define this storageimage_shared // make INFO work
    INFO("storageimage_shared->sync_worker() started");
    // call real worker member function
    storageimage_shared->sync_worker() ;
#undef this
    return NULL;
}


// parses DEC image into filesystem and generates change events
// filesystem_dec_metadata_snapshot must be valid
void storageimage_shared_c::sync_dec_image_to_filesystem_and_events()
{
//    filesystem_dec->debug_print("sync_dec_image_to_filesystem_and_events() BEFORE parse") ;
    image->save_to_file("/tmp/sync_worker_1.dump") ;
    // PDP11 has completed write transaction, image stable now (really?)
    // image -> filesystem

    filesystem_dec->parse() ;
//    filesystem_dec->debug_print("sync_dec_image_to_filesystem_and_events() AFTER parse") ;
    filesystem_dec->changed_blocks->clear() ;

    // which files have changed? generate change events
//            if (filesystem_dec_metadata_snapshot_valid) {
    filesystem_dec->produce_events(filesystem_dec_metadata_snapshot) ;
    filesystem_dec->event_queue.debug_print("sync_dec_image_to_filesystem_and_events()") ;


    filesystem_dec->ack_event_filter.clear() ; // responses to dec.consume() processed

}


// host consumes DEC change events, update host filesystem and shared dir
void storageimage_shared_c::sync_dec_filesystem_events_to_host()
{
//	filesystem_dec->debug_print("sync_dec_filesystem_events_to_host(): DEC events sent to host, DEC filesystem") ;
//	filesystem_dec->event_queue.debug_print("sync_dec_filesystem_events_to_host(): DEC change events") ;
    if (!filesystem_dec->event_queue.empty()) {
        while(!filesystem_dec->event_queue.empty()) {
            auto event = dynamic_cast<filesystem_dec_event_c*>(filesystem_dec->event_queue.pop()) ;
            assert(event) ;
            filesystem_host->consume_event(event) ;
        }
//    filesystem_host->debug_print("sync_dec_filesystem_events_to_host()") ;
    }
}

// eval Linux inotify events, update host_filesystem and produce change events
void storageimage_shared_c::sync_host_shared_dir_to_filesystem_and_events()
{
    filesystem_host->produce_events() ;
    filesystem_host->ack_event_filter.clear() ; // responses to host.consume() processed
}

// DEC consumes host events, update DEC filesystem and image
void storageimage_shared_c::sync_host_filesystem_events_to_dec()
{
    // send resolved host events to DEC
    if (!filesystem_host->event_queue.empty()) {
//        filesystem_host->debug_print("sync_host_filesystem_events_to_dec()") ;
//        filesystem_host->event_queue.debug_print("sync_host_filesystem_events_to_dec(): Host eval inotify events @ AAA") ;
        while(!filesystem_host->event_queue.empty()) {
            auto event = dynamic_cast<filesystem_host_event_c*>(filesystem_host->event_queue.pop()) ;
            assert(event) ;
            filesystem_host->update_event(event) ;
            filesystem_dec->consume_event(event) ;
        }

//        filesystem_dec->debug_print("sync_host_filesystem_events_to_dec()") ;
        //filesystem_dec->file_by_path.debug_print("DEC") ;
        filesystem_dec->render() ;
        image->save_to_file("/tmp/sync_worker_2.dump") ;
    }

    // "render() writes to the image and sets the change flags. caller clear it!
}



// wipe pending changes, by initializing the metadata snapshot
void storageimage_shared_c::sync_dec_snapshot()
{
    // snapshot current structure (without file data!)
    // for next change event generation
    filesystem_dec_metadata_snapshot->clear_rootdir() ;
    filesystem_dec->copy_metadata_to(filesystem_dec_metadata_snapshot) ;
//    filesystem_dec_metadata_snapshot->debug_print("sync_dec_snapshot(): DEC filesystem_dec_metadata_snapshot updated") ;
//    filesystem_dec->event_queue.clear() ; keep some unprocessed ack_events
}

// wipe pending changes, by clearing received inotify events
void storageimage_shared_c::sync_host_restart()
{
    //	  filesystem_host->event_queue.clear() ; keep some unprocessed ack_events
    filesystem_host->changed = false ;
}



// polls for changes in the PDP image and on shared host filesystem
// !! runs in parallel thread -> mutex !!
void storageimage_shared_c::sync_worker()
{
    syncer_terminate = false ;



    while (!syncer_terminate) {
        timeout_c::wait_ms(1000) ; // TEST inotify

        lock() ; // block PDP access to image

        // A.1 poll host for changes, also recognizes "stable" condition
        // (inotifys are synced with file access, so no need to wait for "stable")
        sync_host_shared_dir_to_filesystem_and_events() ; // updates host_filesystem_changed
        bool host_filesystem_stable = ( now_ms() - filesystem_host->change_time_ms) > 1000 ;

        // is operation on these filesystem allowed? probably no further access now
        bool dec_image_stable = ( now_ms() - dec_image_change_time_ms) > 1000 ;


        // wait until operations on shared dir and DEC image completed
        if (host_filesystem_stable && dec_image_stable) {

            // A.2 Update DEC fileystem from image and generate change events
//            if (dec_image_changed)
//                sync_dec_image_to_filesystem_and_events() ; // may change filesystem_dec->changed

            // B. consume DEC and host events to update the respective other side
            // ! Produces new ack-events on the other side, which are ignored.
            // ! parallel changes in the DEC image are lost
            // ! parallel changes on the host remain, but are not synced to DEC.

            // render DEC file system again if no host events for 1 sec
            sync_host_filesystem_events_to_dec() ;
            filesystem_host->changed = false ; // events produced -> changes processed

            // if one side has changed, the other is also changed now.
            // reset producer, and wipe ack-events on consumer
            if (dec_image_changed || filesystem_dec->changed) {
                sync_dec_image_to_filesystem_and_events() ; // may change filesystem_dec->changed
                // changed by DEC write() or sync_host_filesystem_events_to_dec()
                sync_dec_snapshot() ;
            }
            filesystem_dec->changed = false ;
            dec_image_changed = false ;

            // update host. triggered by sync_dec_image_to_filesystem_and_events() or sync_host_filesystem_events_to_dec()
            sync_dec_filesystem_events_to_host() ;

            // if (filesystem_host->changed)
            // 	// changed by inotify or sync_dec_filesystem_events_to_host()
            //     sync_host_restart() ; // filesystem_host->changed=false
        }


        unlock() ;
    }
}



} // namespace
