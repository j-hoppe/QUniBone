/* filesystem_base.hpp - base classes for any hierachic file tree (DEC or Linux host)

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

  Generic tree management and file sorting

  06-jan-2022 JH      created
 */
#ifndef _SHAREDFILESYSTEM_BASE_HPP_
#define _SHAREDFILESYSTEM_BASE_HPP_

#include <vector>
#include <map>
#include <queue>
#include <regex>
#include <ctime>

#include "driveinfo.hpp"

namespace sharedfilesystem {

// error handling, nothing special to filesystems at the moment
class filesystem_exception: public std::exception {
	private:
		std::string message;
	public:
		filesystem_exception(std::string msgfmt, ...) ;
		virtual const char* what() const noexcept {
			return message.c_str();
		}
	};


/*
enum error_e {
    ERROR_OK = 0,
    ERROR_FILESYSTEM_FORMAT	= -1, // error in files ystem structure
    ERROR_FILESYSTEM_OVERFLOW = -2, // too many files on PDP
    ERROR_FILESYSTEM_DUPLICATE = -3, // duplicate file name
    ERROR_FILESYSTEM_INVALID = -4, // invalid PDP file system selected
    ERROR_HOSTDIR = -5 ,// error with shared directory
    ERROR_HOSTFILE = -6,  // file error on host file system
    ERROR_ILLPARAMVAL = -7 // illegal function parameter
} ;
*/
// supported file systems, see derived classes
enum filesystem_type_e {
    fst_none,
    fst_xxdp,
    fst_rt11
} ;

enum filesystem_type_e filesystem_type2text(string filesystem_type_text) ;

class filesystem_event_queue_c ;
class file_base_c ;
class directory_base_c ;
class filesystem_base_c ;


// notify of a change in filesystem
class filesystem_event_c {
public:
    filesystem_event_queue_c  *event_queue ; // uplink
    enum operation_e { op_create, op_modify, op_delete } ;
    enum operation_e operation ;

    // host_path of touched file or directory
    string host_path; // host path to this dir or file, "/dir/dir/file".
    // also DEC events use the host path, not the DEC path
    // indexes host file map, used with file_by_path() to get DEC file
    // for "op_delete", the file does not exist anymore

    bool is_dir ;

    filesystem_event_c() {
        event_queue = nullptr ;
    }

    virtual ~filesystem_event_c() {} // make it virtual

    string operation_text() ;

    virtual string as_text() = 0 ;
} ;

class filesystem_event_queue_c:  public	std::queue<filesystem_event_c*>, public logsource_c {
public:
    filesystem_event_queue_c() {
        filesystem = nullptr ;
    }
    filesystem_base_c *filesystem ; // uplink
    void clear(); // and free all events
    void debug_print(string info) ;
    void push(filesystem_event_c *event) ;
    filesystem_event_c *pop() ;
    // deletes event from heap and returns copy
};


// manages reference counters for filename strings
class filesystem_event_filter_c: public std::map<std::string, int>
{
public:
    void add(string path) ;
    // void remove(string path) ;
    bool is_filtered(string path) ;
} ;



class file_base_c: public logsource_c {
    friend class filesystem_base_c;
public:
    file_base_c() ;
    file_base_c(file_base_c *f) ;
    virtual ~file_base_c() ;

    directory_base_c *parentdir ; // nullptr if file is root dir
    filesystem_base_c *filesystem ;

    string path ; // filesystem specific path.
    // eg "/dir/dir/file" for host, "[a.b.c]file.ext;n"" for Files-11

    int sort_group ; // for sorting

    // universal properties
    uint32_t file_size ; // size in bytes on disk
    struct tm  modification_time; // like stat() st_mtime
    bool	readonly ;
    virtual string get_filename() = 0 ;
    // has "this" changed against cmp?
    virtual bool data_changed(file_base_c *cmp) = 0 ;


} ;

// list of directories and files
// is also a file on its own

class directory_base_c: virtual public file_base_c {
public:
    directory_base_c() ;
    directory_base_c(filesystem_base_c *fs) ;
    directory_base_c(directory_base_c *d): file_base_c(d) 	{	}

    virtual ~directory_base_c() override ;

    // a parentdir contains other subdirectories, of differing types
    std::vector<directory_base_c *> subdirectories ;
    // a parentdir contains files
    std::vector<file_base_c *> files ;

    // general tree handling
    virtual void clear() ; // does sync with path map vie file destructor
    virtual void add_file(file_base_c *file) ;
    virtual void remove_file(file_base_c *oldfile) ;
public:
    virtual unsigned file_count() ;
    void debug_print(int level) ;

} ;

/*** quick find files and dirs by name ***/
class file_by_path_map_c: public std::map<std::string, file_base_c*>
{
public:
    void remember(file_base_c *f) ;
    void forget(file_base_c *f) ;
    file_base_c *get(string path) ;

    void debug_print(string info) ;
} ;


class filesystem_base_c: public logsource_c {
    friend class file_base_c ;
    friend class directory_base_c ;

protected:
    filesystem_base_c() ;
    virtual ~filesystem_base_c() ;


public:
    directory_base_c *rootdir ; // derived filesystems must instantiate of correct type
    void clear_rootdir() ; // results in empty root node

    virtual string get_name() = 0 ;

    bool changed ; // unprocessed changes? set by add/rmeove|directory/file
    uint64_t change_time_ms ;


    // quick find files and dirs by name
    file_by_path_map_c	file_by_path ;

    // block re-production of consumed events
    filesystem_event_filter_c ack_event_filter ;

public:
    // get the path of a file in the tree. key to hash map
    // path is RELATIVE to some root, but the topmost dir is "/"
    // Example: shared image tree located in Dir in /root/qunibone/myfiles11image
    // DEC Path = [TEST.SRC]main.c;1
    // dann get_filepath() => "/test/src/main.c;1"
    // Absolute path: /root/qunibone/myfiles11image/test/src/main.c;1
    virtual string get_filepath(file_base_c *f) = 0 ;

    virtual void add_directory(directory_base_c *parentdir, directory_base_c *newdir) ;
    virtual void remove_directory(directory_base_c *olddir);

    // pending insert/update/delete events
    filesystem_event_queue_c event_queue ;



public:


protected:


    /*** file & dir sorting ***/
    const int SORT_NOGROUP = 0xffffff ; // "no group" index, sorts to the end

    // a compiled group regex
    class sort_group_regex_c {
    public:
        int group;
        string pattern_const ; // regex as text
        std::regex pattern_regex;
    } ;

protected:
    // an ordered list of regex pattern = list of groups
    vector<sort_group_regex_c>	sort_group_regexes ;
    void sort_add_group_pattern(string pattern) ;

    void sort(vector<file_base_c *> file_list) ;

public:
    void debug_print(string info) ;

} ;

} // namespace
#endif // _SHAREDFILESYSTEM_BASE_HPP_


