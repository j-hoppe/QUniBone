/* filesystem_base.cpp - base classes for any hierachic file tree (DEC or Linux host)


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
#include <string.h>
#include <cstdarg>

#include "utils.hpp"
#include "logger.hpp"

#include "filesystem_base.hpp"


namespace sharedfilesystem {


enum filesystem_type_e filesystem_type2text(string filesystem_type_text)
{
    if (! strcasecmp(filesystem_type_text.c_str(), "XXDP"))
        return fst_xxdp ;
    else if (! strcasecmp(filesystem_type_text.c_str(), "RT11"))
        return fst_rt11 ;
    else
        return fst_none ;
}


// exception constructor with printf() arguments
// exception constructor with printf() arguments
filesystem_exception::filesystem_exception(const string msgfmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, msgfmt);
    vsprintf(buffer, msgfmt.c_str(), args);
    message = string(buffer) ;
    va_end(args);
}


string filesystem_event_c::operation_text()
{
    switch(operation) {
    case op_create:
        return "create" ;
    case op_modify:
        return "modify" ;
    case op_delete:
        return "delete" ;
    }
    return "???" ; // not reached
}


// also deletes all events
void filesystem_event_queue_c::clear()
{
    while (!empty()) {
        auto event = pop() ; // my pop deletes event and returns copy
        delete event ;
    }
}

// event into queue, queue takes ownership
// event transmission blocked if it's an expected ack_event
void filesystem_event_queue_c::push(filesystem_event_c *event)
{
    event->event_queue = this ;
    if (filesystem->ack_event_filter.is_filtered(event->host_path)) {
        DEBUG(printf_to_cstr("%s event_qeue.push() blocked by ack_event_filter: %s", filesystem->get_label().c_str(), event->as_text().c_str())) ;
        return ;
    }
    std::queue<filesystem_event_c*>::push(event) ;
    DEBUG(printf_to_cstr("%s event_qeue.push() %s", filesystem->get_label().c_str(), event->as_text().c_str())) ;
}

// implements front & pop. caller gets ownership
filesystem_event_c *filesystem_event_queue_c::pop()
{
    auto result = std::queue<filesystem_event_c*>::front() ;
    std::queue<filesystem_event_c*>::pop() ;
    return result ;
}

void filesystem_event_queue_c::debug_print(string info)
{
    if (logger->ignored(this, LL_DEBUG))
        return ;

    // temporary copy, shares pointers
    filesystem_event_queue_c tmp_event_queue = *this ;
    DEBUG(printf_to_cstr("%s. Debug dump of %s file system event queue:\n", info.c_str(), filesystem->get_label().c_str())) ;
//    printf("%s. Debug dump of %s file system event queue:\n", info.c_str(), filesystem->get_name().c_str()) ;
    while (!tmp_event_queue.empty()) {
        filesystem_event_c *event = tmp_event_queue.pop() ;
        DEBUG(event->as_text().c_str()) ;
//		printf("%s\n", event->as_text().c_str()) ;
    }
}



/*** quick find files and dirs by name ***/

// find a file by its path
void  file_by_path_map_c::remember(file_base_c *f)
{
    // remove old entry, if existing
    if ( get(f->path) != nullptr )
        forget(f) ;

    insert( {f->path, f}) ;
    // assert(file_by_path(f->path) != nullptr) ; // check readout
}


void file_by_path_map_c::forget(file_base_c *f)
{
    erase(f->path) ;
    // assert(file_by_path(f->path) == nullptr) ; // check readout
}

// get a file by its path
file_base_c *file_by_path_map_c::get(string path)
{
    auto item = std::map<std::string,file_base_c*>::find(path) ;
    if (item != end())
        return item->second ;
    else
        return nullptr ;
}


void file_by_path_map_c::debug_print(string info)
{
    printf("%s. Dump of file_by_path_map:\n", info.c_str()) ;
    for (auto it = begin(); it != end(); it++) {
        printf("path=%s\n", it->first.c_str()) ;
    }
}



/*** filesystem ***/

// compare functions: first group index, then name
bool sort_file_comp(file_base_c *f1, file_base_c *f2)
{
    // "< 0" => f1 < f2
    if (f1->sort_group < f2->sort_group)
        return -1 ;
    else return f1->get_filename().compare(f2->get_filename()) < 0 ;

}

// create as 1 when not existing
void filesystem_event_filter_c::add(string path)
{
    auto it = find(path) ;
    if (it == end())
        insert( {path, 1}) ;
    else it->second++ ;
}

/*
// delete entry when reached 0
void filesystem_event_filter_c::remove(string path)
{
    auto it = find(path) ;
    if (it != end()) {
        it->second-- ;
        if (it->second == 0)
            erase(it, it) ;
    }

}
*/

bool filesystem_event_filter_c::is_filtered(string path)
{
    auto it = find(path) ;
    return (it != end()) ;
}



file_base_c::file_base_c()
{
    filesystem = nullptr ;
    parentdir = nullptr ;
    path = "" ;
    readonly = false ;
    file_size=0 ;
    memset(&modification_time, 0, sizeof(struct tm));
}

// clone constructor. only metadata
file_base_c::file_base_c(file_base_c *f)
{
    filesystem = nullptr ;
    parentdir = nullptr ;
    path = f->path ;
    readonly = f->readonly ;
    file_size = f->file_size ;
    modification_time = f->modification_time ;
}

file_base_c::~file_base_c()
{
    filesystem->file_by_path.forget(this) ;
}





directory_base_c::directory_base_c()
{
}

// constructor for rootdir
directory_base_c::directory_base_c(filesystem_base_c *fs)
{
    this->parentdir = nullptr ;
    this->filesystem = fs ;
}


directory_base_c::~directory_base_c()
{
    clear() ;	// clear member tree
}




// delete all directories and files in a directory
void directory_base_c::clear()
{
    for(unsigned i = 0; i < subdirectories.size(); ++i) {
        delete subdirectories[i] ; // recursive delete sub dir tree, calls clear()
    }
    subdirectories.clear() ; // empty lists of (now) invalid pointers

    for(unsigned i = 0; i < files.size(); ++i) {
        delete files[i] ; // delete files
    }
    files.clear() ;
    filesystem->changed = true;
    filesystem->change_time_ms = now_ms() ;
}



// adds an instantiated file to the 'files' list
void directory_base_c::add_file(file_base_c *newfile)
{
    newfile->parentdir = this ;
    newfile->filesystem = this->filesystem ; // uplink
    newfile->path = filesystem->get_filepath(newfile) ; //calc and cache path
    files.push_back(newfile) ;
    filesystem->file_by_path.remember(newfile) ; // register in name map
    filesystem->changed = true;
    filesystem->change_time_ms = now_ms() ;
}

// remove and free a file
// expensive, use clear() when possible
void directory_base_c::remove_file(file_base_c *oldfile)
{
    // files.remove(oldfile) ; // remove pointer from containing dir
    // find index in vector, then delete
    auto it = std::find(files.begin(), files.end(), oldfile) ;
    if (it != files.end())
        files.erase(it) ;
    delete oldfile ; // remove from name map
    filesystem->changed = true;
    filesystem->change_time_ms = now_ms() ;
}


unsigned directory_base_c::file_count() {
    return files.size() ;
}


// dump a directoy and all its files
void directory_base_c::debug_print(int level)
{
    int indent = 4 ;
    if (logger->ignored(filesystem, LL_DEBUG))
        return ;
    // print directories in "[   ]"
    printf("%*s[%s] => %s\n", indent*level, "", get_filename().c_str(), path.c_str()) ;
    //	DEBUG(printf_to_cstr("%*s[%s]", indent*level, "", get_filename())) ;
    // the subdirectories first
    level++ ;
    for (unsigned i=0 ; i < subdirectories.size() ; ++i)
        subdirectories[i]->debug_print(level) ;
    // then the files
    for (unsigned i=0 ; i < files.size() ; ++i) {
        file_base_c *f = files[i] ;
        char timebuff[80] ;
        strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", &(f->modification_time)) ;
        printf("%*s%s, path=%s, file_size=%u, mtime=%s\n", indent*(level), "",
               f->get_filename().c_str(), f->path.c_str(), f->file_size, timebuff ) ;
        //	DEBUG(printf_to_cstr("%*s%s", indent*level, "", files[i]->get_filename().c_str())) ;
    }
}



filesystem_base_c::filesystem_base_c()
{
    event_queue.filesystem = this ;
    rootdir = nullptr ;
//	: filesystem_event_queue_c(this)
    changed = false;
    change_time_ms = 0 ;
}

filesystem_base_c::~filesystem_base_c()
{
    clear_rootdir() ;
    if (rootdir != nullptr)
        delete rootdir ;
}


/*
// delete all tree members
void filesystem_base_c::clear()
{
    if (rootdir != nullptr) {
        rootdir->clear() ;  // deletes all member tree
    }
}
*/

// preserves rootdir, is fast
void filesystem_base_c::clear_rootdir()
{
    if (rootdir == nullptr)
        return ;

    // makes file destructors fast
    file_by_path.clear() ;

    rootdir->clear() ; // recursive delete tree

    file_by_path.remember(rootdir) ; // restore
}


// adds a directory to the 'directories' list
// must be freed by "remove_directory()"
void filesystem_base_c::add_directory(directory_base_c *parentdir, directory_base_c *newdir)
{
    if (parentdir == nullptr) // newdir is root
        rootdir = newdir ;
    newdir->parentdir = parentdir ;
    newdir->filesystem = this ; // propagate uplink
    newdir->path = get_filepath(newdir) ; // calc and cache path
    if (parentdir != nullptr)
        parentdir->subdirectories.push_back(newdir) ;
    file_by_path.remember(newdir) ; // register in name map
    changed = true;
    change_time_ms = now_ms() ;
}


void filesystem_base_c::remove_directory(directory_base_c *olddir)
{
    // recursively remove all contained subdirectories and files in ,
    olddir->clear() ;

    directory_base_c *parentdir = olddir->parentdir ;

    // subdirectories.remove(olddir) ; // remove pointer from containing dir

    // find index in vector, then delete. Not if rootdir is to be deleted
    if (parentdir != nullptr) {
        auto it = std::find(parentdir->subdirectories.begin(), parentdir->subdirectories.end(), olddir) ;
        if (it != parentdir->subdirectories.end())
            parentdir->subdirectories.erase(it) ;
    }
    delete olddir ;
    changed = true;
    change_time_ms = now_ms() ;
}


// add a regex string, defining a new "group".
// the sort order of this group is higher than previous added ones.
// called only in constructor of derived filesystem before 1st sort()
void filesystem_base_c::sort_add_group_pattern(string pattern)
{
    sort_group_regex_c tmp ;

    try {
        tmp.pattern_const = pattern ;
        tmp.pattern_regex = std::regex(pattern, regex_constants::icase) ;
        tmp.group = sort_group_regexes.size() ; // enumerate
        sort_group_regexes.push_back(tmp);
    }
    catch (regex_error e) {
        ERROR("Error compiling tmp: %s", e.what());
    }
}

// *count maybe -1, then lists are NULL terminated
void filesystem_base_c::sort(vector<file_base_c *> files)
{
    unsigned i, j;
    bool match;

    // init
    for (i = 0; i < files.size(); i++)
        files[i]->sort_group = SORT_NOGROUP ;


    // 1) match each name against the exact pattern, no case sensitivity, no regex
    for (i = 0; i < files.size(); i++)
        for (match = false, j = 0; !match && j < sort_group_regexes.size(); j++)
            // exact match?
            if (!strcasecmp(sort_group_regexes[j].pattern_const.c_str(), files[i]->get_filename().c_str())) {
                files[i]->sort_group = j;
                match = true;
            }

    // 2) match each name against the regexes, first match defines group
    for (i = 0; i < files.size(); i++)
        for (match = false, j = 0; !match && j < sort_group_regexes.size(); j++)
            // any match?
            if (regex_search(files[i]->get_filename(), sort_group_regexes[j].pattern_regex)) {
                files[i]->sort_group = j;
                match = true;
            }

    // sort by group and name
    std::sort(files.begin(), files.end(), sort_file_comp) ;
}


void filesystem_base_c::debug_print(string info)
{
    if (logger->ignored(this, LL_DEBUG))
        return ;
    printf("%s. Dump of filesystem %s:\n", info.c_str(), get_label().c_str()) ;
    rootdir->debug_print(0) ;
}

void filesystem_base_c::timer_start()
{
    timer_start_ms = now_ms() ;
}

void filesystem_base_c::timer_debug_print(string info)
{
    if (logger->ignored(this, LL_DEBUG))
        return ;

    uint64_t elapsed_ms = now_ms() - timer_start_ms ;
    DEBUG(printf_to_cstr("%s. Elapsed time %0.3f sec\n", info.c_str(), (double)elapsed_ms / 1000.0)) ;
}

} // namespace

