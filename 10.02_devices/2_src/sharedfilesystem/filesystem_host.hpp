/* filesystem_host.hpp - hierachic Linux file tree

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

  22-aug-2022 JH      created
 */

#ifndef _SHAREDFILESYSTEM_HOST_HPP_
#define _SHAREDFILESYSTEM_HOST_HPP_

#include <map>
#include <fstream>
#include "filesystem_base.hpp"

namespace sharedfilesystem {

class filesystem_dec_event_c ;
class file_dec_stream_c ;
class file_host_c;
class filesystem_host_event_c: public filesystem_event_c  {
public:
    file_host_c *host_file ;

    filesystem_host_event_c() ;
    filesystem_host_event_c(enum operation_e operation,
                            string path, bool is_dir, file_host_c *file) ;

    ~filesystem_host_event_c() override {}
    string as_text() override ;
} ;

class file_host_c: virtual public file_base_c {
    // directory inherits from directory_base -> file_base and file_host->file_base
    friend class directory_host_c;
private:
    string filename ;
public:
    file_host_c(string dirname) ;
    file_host_c(file_host_c *_f) ;

    ~file_host_c() override ;

    // access file content on disk
    fstream data ; //
    fstream *data_open(bool open_for_write) ; // open data stream and return ptr
    void data_close() ;


    virtual string get_filename() override {
        return filename ;
    }
    void load_disk_attributes() ;

    // evaluates inotify events in a state machine
    // create -> modify ->close_write
    // - a new directory/file is created on "CREATE"
    bool	inotify_create_pending ; // IN_CREATE, dir/file inserted but not yet stable
    bool	inotify_modify_pending ; // IN_ATTRIB or IN_MODIFY, dir/file must already exist

    virtual bool data_changed(file_base_c *cmp) override ;

    void render_to_disk(uint8_t *write_data, unsigned write_data_size) ; // write to disk
    void remove_from_disk() ;

} ;


// has also a filename
// handles the inotify events (wich are implemented "per directory")
class directory_host_c: public directory_base_c, public file_host_c {
    friend class filesystem_host_c ;
private:
//    string filename ;
    int inotify_wd ; // watch descriptor

public:
    directory_host_c(string dirname) ;
    ~directory_host_c() override;

    // watches on a "per parentdir" base
    void inotify_add_watch() ;
    void inotify_remove_watch() ;

    void parse_from_disk_dir() ;
    void create_events(enum filesystem_event_c::operation_e operation) ;

    // read inotify events for full tree
    void generate_events() ;

    // inotify_init
} ;


// almost identical with base
class filesystem_host_c: public filesystem_base_c {
    friend class file_host_c ;
    friend class directory_host_c ;
private:
    string rootpath ; // location of "rootdir" in filesystem

    int	inotify_fd ; // file descriptor for the inotify instance

public:
    filesystem_host_c(string rootpath) ;
    ~filesystem_host_c() override ;

    virtual string get_label() override ;

    // get the path of a file in the tree rleative to rootpath
    // "rootpath" is not part of path
    string get_filepath(file_base_c *f) override ;
    static string get_host_path(file_base_c *f) ; // static version of get_file_path

    // get the path of a file on the disk including rootpath
    string get_absolute_filepath(string path) ;

    virtual void add_directory(directory_base_c *parentdir, directory_base_c *dir) override ;
    virtual void remove_directory(directory_base_c *olddir) override ;


    void clear_disk_dir() ;

    // delete all subdirs and files,
    // generates delete events,
    // then scan disk and fill with files and subdirectories,
    // generated create events
    void parse() ;

    void produce_events() ;
    void clear_events() ;
    void update_event(filesystem_host_event_c *event) ;


private:
    // link inotify event  to dir vie dir->inotify_wd
    map<int,directory_host_c*> inotify_watch_map ;

    void inotify_event_eval(struct inotify_event *ino_event) ;
    void inotify_events_eval(bool discard) ;

    char *inotify_event_as_text(struct inotify_event *ino_event) ; // diag
    void inotify_test() ;

public:
    void import_dec_stream(file_dec_stream_c *stream) ;
private:

    void consume_event_do_create(filesystem_dec_event_c *event) ;
    void consume_event_do_delete(filesystem_dec_event_c *event) ;
public :
    void consume_event(filesystem_dec_event_c *event) ;

} ;


} // namespace
#endif // _SHAREDFILESYSTEM_HOST_HPP_


