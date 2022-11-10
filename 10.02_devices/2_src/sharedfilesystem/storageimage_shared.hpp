/* sfs_filesystem.hpp - single interface to base of different DEC file systems

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

 */
#ifndef _SFS_STORAGEIMAGE_SHARED_BASE_HPP_
#define _SFS_STORAGEIMAGE_SHARED_BASE_HPP_

#include <pthread.h>
//#include <future>
#include <stdint.h>

#include "driveinfo.hpp"
#include "storageimage.hpp"

#include "filesystem_host.hpp"
#include "filesystem_dec.hpp"
//#include "filesystem_mapper.hpp"

namespace sharedfilesystem {


// common features for XXDP and RT11 filesystem
class storageimage_shared_c: public storageimage_base_c {
    friend class storageimage_partition_c ;

public:
    storageimage_shared_c(
        string _image_path,
        bool use_syncer_thread,
        enum filesystem_type_e filesystem_type,
        enum dec_drive_type_e drive_type_text,
        unsigned drive_unit,
        uint64_t capacity,
        std::string hostdir) ;
    virtual ~storageimage_shared_c() override ;
    pthread_mutex_t mutex;

    void lock() ; // mutex protection
    void unlock() ;

// memoryimage handling
protected:
    bool readonly ;
    void image_write_std144_bad_sector_table() ;

public:
    // storageimage_base, identic interface from emulated disk drive to all
    // filesystem memory images
    virtual bool open(bool create) override ;
    virtual bool is_readonly() override ;
    virtual bool is_open(	void) override ;
    virtual bool truncate(void) override ;
    virtual void read(uint8_t *buffer, uint64_t position, unsigned len) override ;
    virtual void write(uint8_t *buffer, uint64_t position, unsigned len) override ;
    virtual uint64_t size(void) override ;
    virtual void close(void) override ;
    virtual void get_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset, uint32_t data_size) override; // mandatory
    virtual void set_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset) override ; // mandatory
    virtual void save_to_file(string host_filename) override ; // mandatory

protected:
    enum filesystem_type_e type ;

    string image_path ; // DEC image on SDcard
    // derived file system implementation classes use this
    string		host_shared_rootdir ; // root of file tree on host, absolute path
    drive_info_c drive_info ; // block_count, block_size
    unsigned	drive_unit ; // unit number of drive

    // disk surface
    storageimage_base_c *image ;

    // dec_image_changed against bianry image and dec filesystem
    bool	dec_image_changed ; // did the PDP changed the image ?
    uint64_t	dec_image_change_time_ms ; // last PDP read or write operation
    void image_data_pdp_access(bool changing) ;

    // interface to filesystem
public:
    // for now there's only one parttion, the leading disk area
    // filesystem may have block_size differing from disk block size
    storageimage_partition_c  *main_partition ;
    storageimage_partition_c  *std144_partition ; // bad sector file


public: // for tests

    // PDP image access changes the logical file system,
    // hold two versions to generate "change" events
    filesystem_dec_c	*filesystem_dec_metadata_snapshot ; // without file data, old state
    filesystem_dec_c	*filesystem_dec ;

    filesystem_host_c	*filesystem_host ; // tree on Linux SDcard

private:
    // https://thispointer.com/c11-how-to-stop-or-terminate-a-thread/
    // std::promise<void> syncer_exitSignal;

    void syncer_image_access() ;
    volatile bool syncer_terminate ; // thread control



public:
    bool use_syncer_thread ; // thread not used in one-time conversion, if compiled into conversion tool
    // why can't i get std::thread to work???
    pthread_t	syncer_pthread ;
    void sync_worker();  // thread main loop
private:
    // macro operations to sync image, host disk and internal filesystems
    void sync_dec_image_to_filesystem_and_events() ;
    void sync_dec_filesystem_events_to_host() ;
    void sync_host_shared_dir_to_filesystem_and_events() ;
    void sync_host_filesystem_events_to_dec() ;
    void sync_dec_snapshot() ;
    void sync_host_restart() ;



} ;

void *storageimage_shared_syncer_worker_pthread_wrapper(void *context) ;


} // namespace

#endif // _SFS_STORAGEIMAGE_SHARED_BASE_HPP_


