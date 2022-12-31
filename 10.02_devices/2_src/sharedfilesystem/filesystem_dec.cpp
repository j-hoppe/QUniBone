/* filesystem_dec.cpp - base classes for any DEC file tree


  Copyright (c) 2022, Joerg Hoppe
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

  06-jan-2022 JH      created

 */

#include <stdio.h>
#include "logger.hpp"

#include "filesystem_dec.hpp"
#include "filesystem_host.hpp" // consumed event

namespace sharedfilesystem {

filesystem_dec_event_c::filesystem_dec_event_c() : filesystem_event_c() {}
filesystem_dec_event_c::filesystem_dec_event_c(     enum operation_e _operation,
        std::string _host_path, bool _is_dir, file_dec_stream_c *_stream)
    : filesystem_event_c()
{
    operation = _operation ;
    host_path = _host_path ;
    is_dir = _is_dir ;
    dec_stream = _stream ;
}

std::string filesystem_dec_event_c::as_text()
{
    assert(event_queue) ;
    filesystem_base_c *filesystem = event_queue->filesystem ;
    assert(filesystem) ;
    assert(dynamic_cast<file_dec_stream_c*>(dec_stream)) ;
    // delete events don't have file stream.
    const char *_host_path = (operation == op_delete) ? host_path.c_str() : dec_stream->host_path.c_str() ;
    return printf_to_string("DEC event \"%s\" on %s %s %s\n", operation_text().c_str(), filesystem->get_label().c_str(), is_dir? "dir" : "file",
                            _host_path) ;

}


file_dec_stream_c::file_dec_stream_c(file_dec_c *_file, std::string _stream_name)
{
    file = _file ;
    stream_name = _stream_name ;
    // file must've been added to filesystem, else get_host_path() will not work
    // can not testet here, if file is a stream, and called by file_c(): stream_c()
//    assert(file->filesystem != nullptr) ;
//    assert(file->parentdir != nullptr) ;
}

file_dec_stream_c::~file_dec_stream_c()
{
}


// reset to constructor state
void file_dec_stream_c::init()
{
    set_size(0) ;
}



// on any change, host files for all streams are simultaneously touched
// => produce the same events for all streams
void file_dec_c::produce_event_for_all_streams(filesystem_event_queue_c *target_event_queue, filesystem_event_c::operation_e operation, bool is_dir)
{
    for (unsigned i=0 ; i < get_stream_count() ; i++) {
        file_dec_stream_c* stream = get_stream(i) ;
        if (stream != nullptr) {
            // return original host path, if file created by host import
            assert(!stream->host_path.empty()) ;
            auto event = new filesystem_dec_event_c(operation, stream->host_path, is_dir, stream) ;
            target_event_queue->push(event) ;
        }
    }
}


filesystem_dec_c::filesystem_dec_c(       storageimage_partition_c *_image_partition): filesystem_base_c()
{
    image_partition = _image_partition ;
    image_partition->block_size = 0 ; // caller must set
    readonly = false ; // inherited from image
}

filesystem_dec_c::~filesystem_dec_c()
{
}



// how many blocks are needed to hold "byte_count" bytes?
unsigned filesystem_dec_c::needed_blocks(uint64_t byte_count)
{
    unsigned block_size = get_block_size() ;
    return  ((byte_count)+(block_size)-1) / (block_size) ;
}

unsigned filesystem_dec_c::needed_blocks(unsigned block_size, uint64_t byte_count)
{
    return  ((byte_count)+(block_size)-1) / (block_size) ;
}


// Recursive compare two directories A with B
// When entries in A are missing in B:
//	add event of type "event_op_missing" to this->event_queue
// When entries in A and B are different:
//	add event of type "event_op_differing" to this->event_queue
void filesystem_dec_c::compare_directories(
    directory_dec_c *dir_a,
    directory_dec_c *dir_b,
    filesystem_event_queue_c *target_event_queue,
    filesystem_event_c::operation_e event_op_missing,
    bool produce_modify_event_on_difference)
{
    assert(dir_a != nullptr) ;
    if (dir_b == nullptr) {
        // dir a is missing in	b
        dir_a->produce_event_for_all_streams(target_event_queue, event_op_missing, true) ;
        return ;
    }

    // has dir node a changed against b?
    if (dir_a->data_changed(dir_b) && produce_modify_event_on_difference) {
        dir_a->produce_event_for_all_streams(target_event_queue, filesystem_event_c::op_modify, true) ;
    }

    // dir nodes exists on both dir_a and dir_b trees. recurse into.
    for(unsigned i = 0; i < dir_a->subdirectories.size(); ++i) {
        directory_dec_c *subdir_a, *subdir_b ;
        subdir_a = dynamic_cast<directory_dec_c *>(dir_a->subdirectories[i]) ;
        subdir_b = dynamic_cast<directory_dec_c *>(dir_b->filesystem->file_by_path.get(subdir_a->path)) ;
        // subdir_b maybe be null, handled in recursion
        compare_directories(subdir_a, subdir_b, target_event_queue, event_op_missing, produce_modify_event_on_difference) ;
    }
    // compare file lists
    for(unsigned i = 0; i < dir_a->files.size(); ++i) {
        file_dec_c *file_a, *file_b ;
        file_a = dynamic_cast<file_dec_c *>(dir_a->files[i]) ;
        file_b = dynamic_cast<file_dec_c *>(dir_b->filesystem->file_by_path.get(file_a->path)) ;
        if (file_b == nullptr) { // file a missing in b
            file_a->produce_event_for_all_streams(target_event_queue, event_op_missing, false) ;
        } else if (file_a->data_changed(file_b) && produce_modify_event_on_difference) { // file a and b different
            file_a->produce_event_for_all_streams(target_event_queue, filesystem_event_c::op_modify, false) ;
        }
    }
}


// compare "this" with an older snapshot, and push according events into queue
void filesystem_dec_c::produce_events(filesystem_dec_c *metadata_snapshot)
{
    directory_dec_c *cur_rootdir = dynamic_cast<directory_dec_c *>(this->rootdir) ;
    directory_dec_c *metadata_snapshot_rootdir = dynamic_cast<directory_dec_c *>(metadata_snapshot->rootdir) ;
    // scan cur for new entries (not in metadata_snapshot)
    compare_directories(cur_rootdir, metadata_snapshot_rootdir, &event_queue, filesystem_event_c::op_create, true) ;
    // scan metadata_snapshot for deleted entries (not in cur anymore). ignore changes, scanned above
    compare_directories(metadata_snapshot_rootdir, cur_rootdir, &event_queue, filesystem_event_c::op_delete, false) ;
}


void filesystem_dec_c::consume_event(filesystem_host_event_c *event)
{
    DEBUG("%s: filesystem_dec_c::consume_event(): %s", get_label().c_str(), event->as_text().c_str()) ;

    if (event->operation == filesystem_event_c::op_create) {
        import_host_file(event->host_file) ;
    } else if (event->operation == filesystem_event_c::op_modify) {
        // change as delete-create pair
        delete_host_file(event->host_path) ;
        import_host_file(event->host_file) ;
    } else if (event->operation == filesystem_event_c::op_delete) {
        delete_host_file(event->host_path) ;
    }
    delete event ;
}

// create file system info and write to host
// VOLUMNE INFO not part of DEC filesystem, but part of host file system
void filesystem_dec_c::update_host_volume_info(std::string root_path)
{
    std::stringstream buffer ;
// printf("DEC filesystem changed, %s updated\n", volume_info_host_path.c_str()) ;

    produce_volume_info(buffer) ;
    std::ofstream fout(root_path + "/" + volume_info_host_path) ;
    fout << buffer.str() ;
}


} // namespace


