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


#include <assert.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/time.h>
#include <dirent.h>

#include "logger.hpp"
#include "utils.hpp"

#include "filesystem_host.hpp"
#include "filesystem_dec.hpp" // consumed event


namespace sharedfilesystem {

filesystem_host_event_c::filesystem_host_event_c() {}
filesystem_host_event_c::filesystem_host_event_c(         enum operation_e _operation,
        std::string _path, bool _is_dir, file_host_c *_file) : filesystem_event_c()
{
    operation = _operation ;
    host_path = _path ;
    is_dir = _is_dir ;
    host_file = _file ;
}


std::string filesystem_host_event_c::as_text()
{
    assert(event_queue) ;
    filesystem_base_c *filesystem = event_queue->filesystem ;
    assert(filesystem) ;
    return printf_to_string("Host event \"%s\" on %s %s %s\n", operation_text().c_str(), filesystem->get_label().c_str(), is_dir? "dir" : "file", host_path.c_str()) ;
}


file_host_c::file_host_c(std::string _filename): file_base_c()
{
    filename = _filename ;
    inotify_create_pending = inotify_modify_pending = false ;
}

// clone constructor. only metadata
file_host_c::file_host_c(file_host_c *f) : file_base_c(f)
{
    filename = f->filename ;
}



file_host_c::~file_host_c()
{
    if (data.is_open() )
        data.close() ;
}




// load file attributes (date, read_only) from disk
void file_host_c::load_disk_attributes()
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;
    std::string abspath = fs->get_absolute_filepath(this->path) ;
    struct stat stat_buff ;
    if (stat(abspath.c_str(), &stat_buff) < 0)
        ERROR("file_host_c::load_attributes(): can not stat %s, error = %d", abspath.c_str(), errno);

    modification_time = *localtime(&stat_buff.st_mtime);
    file_size = stat_buff.st_size ;
    // file is readonly, if data stream has no user write permission (see stat(2))
    readonly = !(stat_buff.st_mode & S_IWUSR) ;
    assert(access(abspath.c_str(), F_OK) == 0) ;
    // even in mode 444, root may write. so here always readonly = false
    // readonly = (access(abspath.c_str(), W_OK) != 0) ;
}

// open data stream and return ptr
std::fstream *file_host_c::data_open(bool open_for_write)
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;
    std::string abspath = fs->get_absolute_filepath(this->path) ;
    std::ios_base::openmode mode ;
    if (open_for_write)
        mode = std::ios::out ;
    else mode = std::ios::in ;
    data.open(abspath, mode) ;
    assert(data.is_open()) ;
    return &data ;
}


void file_host_c::data_close()
{
    data.close() ;
    assert(!data.is_open()) ;
}


// have file attributes or data content changed?
// filename not compared, speed!
// actual file data not compared, size should do.
// This fucntion isn't used anyway, host file changes come over intofy
bool file_host_c::data_changed(file_base_c *_cmp)
{
    auto cmp = dynamic_cast <file_host_c*>(_cmp) ;
    return
//			get_filename().compare(cmp->get_filename()) != 0
        memcmp(&modification_time, &cmp->modification_time, sizeof(modification_time)) // faster
        //	|| mktime(modification_time) == mktime(cmp->modification_time)
        || readonly != cmp->readonly
        || file_size != cmp->file_size ;
}


// write file to disk
void file_host_c::render_to_disk(uint8_t *write_data, unsigned write_data_size)
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;
    std::string abspath = fs->get_absolute_filepath(this->path) ;

    // copy data bytes
    data_open(/*write*/true) ;
    data.write((char *)write_data, write_data_size);
    data_close();

    // 2. set disk attributes

    chmod(abspath.c_str(), readonly? 0444 : 0644) ;

    // access and modfication times.
    struct tm _tm = modification_time ;
    time_t _time = mktime(&_tm ) ; // changes modtime
    if (_time < 0) // not represatable? oldest Linux time
        _time = 0 ; // for some internal (e.g. RT11 Bootblock and Monitor)
    struct timeval times[2] ;
    times[0].tv_usec = 0 ;
    times[0].tv_sec = _time ; // secs since 1970
    times[1].tv_usec = 0 ;
    times[1].tv_sec = _time ;
    utimes(abspath.c_str(), times) ;
}


// delete file from disk. opposite to render_to_disk()
void file_host_c::remove_from_disk()
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;
    std::string abspath = fs->get_absolute_filepath(this->path) ;

    unlink(abspath.c_str()) ;
}







directory_host_c::directory_host_c(std::string _dirname): directory_base_c(), file_host_c(_dirname)
{
    inotify_wd = 0 ; // not set
}

directory_host_c::~directory_host_c()
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;

    if (inotify_wd > 0)
        ::inotify_rm_watch(fs->inotify_fd, inotify_wd) ;
}

/* monitor all events on host dir. Must exists.*/
void directory_host_c::inotify_add_watch()
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;
    std::string abspath =fs->get_absolute_filepath(this->path) ;
//    const char *hostdir_realpath = abspath.c_str() ;

    DEBUG(printf_to_cstr("inotify_add_watch(%s)\n", abspath.c_str()));
    assert(inotify_wd == 0) ;
    unsigned mask ;
    mask = IN_ACCESS // (+) File was accessed (e.g., read(2), execve(2)).
           | IN_ATTRIB // (*)  Metadata changed�for example, permissions (e.g.,
           //  chmod(2)), timestamps (e.g., utimensat(2)), extended
           // attributes (setxattr(2)), link count (since Linux
           // 2.6.25; e.g., for the target of link(2) and for
           // unlink(2)), and user/group ID (e.g., chown(2)).
           | IN_CLOSE_WRITE // (+) File opened for writing was closed.
           | IN_CLOSE_NOWRITE // (*) File or parentdir not opened for writing was closed.
           | IN_CREATE // (+) File/parentdir created in watched parentdir (e.g.,
           // open(2) O_CREAT, mkdir(2), link(2), symlink(2),
           // bind(2) on a UNIX domain socket).
           | IN_DELETE // (+) File/parentdir deleted from watched parentdir.
           | IN_DELETE_SELF // Watched file/parentdir was itself deleted.  (This
           // event also occurs if an object is moved to another
           // filesystem, since mv(1) in effect copies the file to
           // the other filesystem and then deletes it from the
           // original filesystem.)	In addition, an IN_IGNORED
           // event will subsequently be generated for the watch
           // descriptor.
           | IN_MODIFY // (+) File was modified (e.g., write(2), truncate(2)).
           | IN_MOVE_SELF // Watched file/parentdir was itself moved.
           | IN_MOVED_FROM // (+) Generated for the parentdir containing the old
           // filename when a file is renamed.
           | IN_MOVED_TO // (+) Generated for the parentdir containing the new
           // filename when a file is renamed.
           | IN_OPEN // (*) File or parentdir was opened
           ;

    //  * the events marked above with an asterisk (*) can occur both
    //	 for the parentdir itself and for objects inside the parentdir;
    //  * the events marked with a plus sign (+) occur only for objects
    //	 inside the parentdir (not for the parentdir itself).

    // we don't need pure read accesses
    mask &= ~(IN_ACCESS | IN_OPEN | IN_CLOSE_NOWRITE) ;

    inotify_wd = ::inotify_add_watch(fs->inotify_fd,  abspath.c_str(), mask) ;
    if (inotify_wd < 0)
        FATAL("inotify_add_watch") ;
}

void directory_host_c::inotify_remove_watch()
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;
    std::string abspath = fs->get_absolute_filepath(this->path);

    DEBUG(printf_to_cstr("inotify_rm_watch(%s)\n", abspath.c_str()));
    int res = ::inotify_rm_watch(fs->inotify_fd, inotify_wd) ;

    if (res < 0)
        FATAL("inotify_rm_watch") ;
    inotify_wd = 0 ;
}



// create a "create" or delete" event for each file and subdir, recursive,
// but not for "this"
// events are pushed to filesystem->event_queue
void directory_host_c::create_events(enum filesystem_event_c::operation_e operation)
{
    for(unsigned i = 0; i < subdirectories.size(); ++i) {
        auto subdir = dynamic_cast<directory_host_c *>(subdirectories[i]) ;
        auto event = new filesystem_host_event_c(operation, subdir->path, true, subdir) ;
        filesystem->event_queue.push(event) ;
        subdir->create_events(operation) ;
    }

    // all files in this subdir
    for(unsigned i = 0; i < files.size(); ++i) {
        auto f = dynamic_cast<file_host_c*>(files[i]) ;
        auto event = new filesystem_host_event_c(operation, files[i]->path, false, f) ;
        filesystem->event_queue.push(event) ;
    }
}


// scan disk for subdirectories and files
void directory_host_c::parse_from_disk_dir()
{
    auto fs = dynamic_cast<filesystem_host_c*>(filesystem) ;
    struct dirent *entry ;
    DIR *dp ;

    // recurse into all subdirs on disk
    std::string abspath = fs->get_absolute_filepath(this->path) ;
    dp = opendir(abspath.c_str()) ;
    assert(dp) ;
    while ((entry = readdir(dp)))
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            // d_name full or last segment?
            directory_host_c *newdir = new directory_host_c(entry->d_name ) ;
            filesystem->add_directory(this, newdir) ;
            newdir->load_disk_attributes() ;
            newdir->parse_from_disk_dir() ; // recurse into
        }
    closedir(dp) ;


    // add all files in current dir
    dp = opendir(abspath.c_str()) ;
    assert(dp) ;
    while ((entry = readdir(dp)))
        if (entry->d_type == DT_REG ) {
            file_host_c *newfile = new file_host_c(entry->d_name) ;
            // newfile date via lstat(get_absolute_path())
            add_file(newfile) ;
            newfile->load_disk_attributes() ;
        }
    closedir(dp) ;
}



// read inotify source and push into filesyewte,_event_queue
void directory_host_c::generate_events()
{
}


filesystem_host_c::filesystem_host_c(std::string _rootpath)
    : filesystem_base_c()
{

//    inotify_test() ;
    rootpath = _rootpath ;

    // creating the INOTIFY instance, is used in add_directory()
    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        FATAL("%s: inotify_init1()", get_label().c_str());
    }

    // create root dir.
    add_directory(nullptr, new directory_host_c("") ) ;
    assert(rootdir->filesystem == this) ;


}

filesystem_host_c::~filesystem_host_c()
{
    close(inotify_fd) ;
    delete rootdir ;
    rootdir = nullptr ; // signal to base class destructor
}

// Like "Host dir /root/10.02_devices/3_test/sharedfilesystem/xxdp-rl02"
// 			      /root/10.03_app_demo/5_applications/rt11.rl02/shared_rl1

std::string filesystem_host_c::get_label() 
{
	return "Host dir " + rootpath ;
}



// get the path of a file in the tree.
// "rootdir" is not part of path
// f can be file or directory
// used by filesystem.add() to calc file->path
// Path begins with "/" in any case
std::string filesystem_host_c::get_filepath(file_base_c *f) {
    return get_host_path(f) ;
}

// static version of get_file_path, needed for event_dec generation
std::string filesystem_host_c::get_host_path(file_base_c *f)
{
    if (!f->parentdir)
        return "/" ; // root
    std::string result = "";
    while(f->parentdir) { // all upwards, but stop before root
        result = "/" + f->get_filename() + result ;
        f = f->parentdir ; // step up
    }
    return result ;
}

// empty path allowed
std::string filesystem_host_c::get_absolute_filepath(std::string path)
{
    std::string result = rootpath ;
	if (path.empty()) 
		return result ;

    // the local path begins usally with a "/", don't double it
    if (path.at(0) != '/')
        result.append("/") ;

    result.append(path) ;
    // does not need to exist
    /*
    char *buffer = realpath(result.c_str(), NULL) ;
    if (!buffer)
        FATAL("%s: Invalid absolute host path: %s", get_label().c_str(), result.c_str()) ;
    result = std::string(buffer) ;
    free(buffer) ;
    */
    return result ;
}


// inotify watches, when the directory is part of filesystem
void filesystem_host_c::add_directory(directory_base_c *_parentdir, directory_base_c *_dir)
{
    filesystem_base_c::add_directory(_parentdir, _dir) ;
    auto dir = dynamic_cast<directory_host_c *>(_dir) ;
    dir->inotify_add_watch() ;

    inotify_watch_map[dir->inotify_wd] = dir ;
}

void filesystem_host_c::remove_directory(directory_base_c *_dir)
{
    auto dir = dynamic_cast<directory_host_c *>(_dir) ;
    inotify_watch_map.erase(dir->inotify_wd) ; // remove link to dir
    dir->inotify_remove_watch() ;

    filesystem_base_c::remove_directory(dir) ;
}


// delete everything in the the Linux directory
void filesystem_host_c::clear_disk_dir()
{
    char buffer[4096] ;
    // Boy, this is dangerous. some minor security checks:
    assert(file_exists(&rootpath)) ;
    assert(rootpath.size() >= 4) ;	// not just "/", at least "/tmp" ...

    // https://unix.stackexchange.com/questions/77127/rm-rf-all-files-and-all-hidden-files-without-error
    // do not remove hidden with just ".*", will recurse upwards to ".." !
    sprintf(buffer, "/bin/sh -c 'rm --force --recursive  %s/..?* %s/.[!.]* %s/*'",
            rootpath.c_str(),rootpath.c_str(),rootpath.c_str()) ;
    DEBUG(buffer) ;
    system(buffer) ; // waits until ready
}



// afterwards event-queue is filled with "delete" and "create" events
void filesystem_host_c::parse()
{
	timer_start() ;
	
    // create "delete events" for all existing files and subdirs
    auto rd = dynamic_cast<directory_host_c *>(rootdir) ;
    rd->create_events(filesystem_event_c::op_delete) ;
    rd->clear() ;

    rd->parse_from_disk_dir() ;
    rd->create_events(filesystem_event_c::op_create) ;

	timer_debug_print(get_label() + " parse()") ;
}




// query global inotify descriptor and convert to change events in event_queue
// discard: when true, just empty inotify queue
void filesystem_host_c::inotify_events_eval(bool discard)
{
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
// small buffer, read() repeatedly
#define EVENT_BUF_LEN     ( 3 * ( EVENT_SIZE + 16 ) )

    // recursive for all directories
//	auto rd = dynamic_cast<directory_host_c *>(rootdir) ;
//	rd->produce_events() ;

    int avail;
    do {
        // read a chunk of inotify events into a buffer
        // read the inotify data stream into an byte buffer
//    ioctl(inotify_fd, FIONREAD, &avail);
//    if (avail == 0)
//        return ; // no inotify events

        uint8_t buffer[EVENT_BUF_LEN];
        // read to determine the event change happens on all watch descriptors/directories.
        // This read blocks until the change event occurs, but we have asserted data avail
        avail = read(inotify_fd, buffer, EVENT_BUF_LEN);
        if (avail < 0 ) {
            // checking for error. EAGAIN means: no data, "Resource temporarily unavailable" */
            // avail == 0 results in noop here
            if (errno != EAGAIN)
                FATAL("%s: read(inotify_fd) failed with errno %d", get_label().c_str(), errno) ;
        } else  {
            int offset = 0;
            while (offset < avail) {
                struct inotify_event *ino_event = (inotify_event*)(buffer + offset);
                offset = offset + sizeof(inotify_event) + ino_event->len;

                // Insert logic here
                // printf("%s\n", inotify_event_as_text(ino_event)) ;
                if (!discard) {
                    inotify_event_eval(ino_event) ;
                }
            }
        }
    } while(avail > 0)  ;
}


void filesystem_host_c::produce_events()
{
    inotify_events_eval(false) ;
}

void filesystem_host_c::clear_events()
{
    inotify_events_eval(true) ;
    event_queue.clear()	 ;
}

// An event popped from the queue may be outdated,
// as the file system may change while the event rested in the queue.
// Further events will reflect this, but the event nevertheless must be corrected.
void filesystem_host_c::update_event(filesystem_host_event_c *event)
{
    std::string abspath = get_absolute_filepath(event->host_path) ;
    switch (event->operation) {
    case filesystem_event_c::op_create:
        if (! file_exists(&abspath)) // created file deleted again?
            event->operation = filesystem_event_c::op_delete ;
        break ;
    case filesystem_event_c::op_modify:
        if (! file_exists(&abspath)) // modified file now deleted?
            event->operation = filesystem_event_c::op_delete ;
        break ;
    case filesystem_event_c::op_delete:
        if (file_exists(&abspath)) // file was re-created?
            event->operation = filesystem_event_c::op_delete ;
        break ;
    }
}



/*
  Process one inotify event, and update the filesystem accoringly.
  the dir/file is optinally created in CREATE OR ATTRIB,
  and completed on CLOSE_WRITE
  then an filesystem_event is genmarted

  Ob CLOSE, the filesystem msut be in sync with the disk state

  inotify events for different file operation. See test/synthetic scripts
  Pure read accesses suppressed.

  Test data:
  ---------
  mkdir synthetic
  cd synthetic

  mkdir dir1
  mkdir dir1/dir11
  mkdir dir1/dir12
  mkdir dir2
  ls -al >file1
  ls -al >file2
  ls -al >file3
  ls -al >dir1/file11
  ls -al >dir1/file12
  ls -al >dir1/dir11/file111
  ls -al >dir1/dir11/file112

  # creation
  cd dir1
  # tests inside directory
  # 1.1. file creation
  cp file11 file19
  inotify event: wd=2, mask=IN_CREATE 	  , cookie=0, file "dir/name" = "dir1/file19".
  inotify event: wd=2, mask=IN_MODIFY 	  , cookie=0, file "dir/name" = "dir1/file19".
  inotify event: wd=2, mask=IN_CLOSE_WRITE  , cookie=0, file "dir/name" = "dir1/file19".
  # 1.2. file attrib change
  touch file12
  inotify event: wd=2, mask=IN_ATTRIB 	  , cookie=0, file "dir/name" = "dir1/file12".
  inotify event: wd=2, mask=IN_CLOSE_WRITE  , cookie=0, file "dir/name" = "dir1/file12".
  # 1.3. file content change
  cat file12 >>file19
  inotify event: wd=2, mask=IN_MODIFY 	  , cookie=0, file "dir/name" = "dir1/file19".
  inotify event: wd=2, mask=IN_CLOSE_WRITE  , cookie=0, file "dir/name" = "dir1/file19".
  # 1.4. delete file
  rm file11
  inotify event: wd=2, mask=IN_DELETE 	  , cookie=0, file "dir/name" = "dir1/file11".
  # 1.5. move
  mv file19 file11
  inotify event: wd=2, mask=IN_MOVED_FROM   , cookie=698, file "dir/name" = "dir1/file19".
  inotify event: wd=2, mask=IN_MOVED_TO	  , cookie=698, file "dir/name" = "dir1/file11".
  # tests with directory, still inside dir1
  # 2.1. dir creation.
  mkdir dir19
  inotify event: wd=2, mask=IN_CREATE 	  , cookie=0, directory "dir/name" = "dir1/dir19".
  # Now we must set new watch on dir19!

  # 2.2. dir change? no!
  touch file12
  inotify event: wd=2, mask=IN_ATTRIB 	  , cookie=0, file "dir/name" = "dir1/file12".
  inotify event: wd=2, mask=IN_CLOSE_WRITE  , cookie=0, file "dir/name" = "dir1/file12".
  # 2.3. dir + file change? No!
  ls >dir19/file191
  inotify event: wd=5, mask=IN_CREATE 	  , cookie=0, file "dir/name" = "dir1/dir19/file191".
  inotify event: wd=5, mask=IN_MODIFY 	  , cookie=0, file "dir/name" = "dir1/dir19/file191".
  inotify event: wd=5, mask=IN_CLOSE_WRITE  , cookie=0, file "dir/name" = "dir1/dir19/file191".
  # 2.4 dir name change, with file content
  mv dir19 dir18
  inotify event: wd=2, mask=IN_MOVED_FROM   , cookie=700, directory "dir/name" = "dir1/dir19".
  inotify event: wd=2, mask=IN_MOVED_TO	  , cookie=700, directory "dir/name" = "dir1/dir18".
  inotify event: wd=5, mask=IN_MOVE_SELF	  , cookie=0, file "dir/name" = "dir1/dir19/NULL".
  # Now we must set new watch on dir18!
  # 2.4 delete dir - failed "not empty"
  rmdir dir18
  # 2.5 delete dir recursive
  rm -r dir18
  inotify event: wd=4, mask=IN_DELETE 	  , cookie=0, file "dir/name" = "dir1/dir18/file191".
  inotify event: wd=4, mask=IN_DELETE_SELF  , cookie=0, file "dir/name" = "dir1/dir18/NULL".
  inotify event: wd=4, mask=IN_IGNORED	  , cookie=0, file "dir/name" = "dir1/dir18/NULL".
  inotify event: wd=2, mask=IN_DELETE 	  , cookie=0, directory "dir/name" = "dir1/dir18".
  # tests across directories. Back to "sythetic" root
  cd $home
  # 3.1 move file
  mv dir1/dir11/file111 dir1/dir12/file129
  inotify event: wd=5, mask=IN_MOVED_FROM   , cookie=701, file "dir/name" = "dir1/dir11/file111".
  inotify event: wd=3, mask=IN_MOVED_TO	  , cookie=701, file "dir/name" = "dir1/dir12/file129".
  cp dir1/dir12/file129 dir1/dir11/file111
  inotify event: wd=5, mask=IN_CREATE 	  , cookie=0, file "dir/name" = "dir1/dir11/file111".
  inotify event: wd=5, mask=IN_MODIFY 	  , cookie=0, file "dir/name" = "dir1/dir11/file111".
  inotify event: wd=5, mask=IN_CLOSE_WRITE  , cookie=0, file "dir/name" = "dir1/dir11/file111".
  rm dir1/dir12/file129
  inotify event: wd=3, mask=IN_DELETE 	  , cookie=0, file "dir/name" = "dir1/dir12/file129".
  # 3.2 dir name change, with subdirectories
  mv dir1 dir9
  inotify event: wd=1, mask=IN_MOVED_FROM   , cookie=702, directory "dir/name" = "/dir1".
  inotify event: wd=1, mask=IN_MOVED_TO	  , cookie=702, directory "dir/name" = "/dir9".
  inotify event: wd=2, mask=IN_MOVE_SELF	  , cookie=0, file "dir/name" = "dir1/NULL".
  mv dir9 dir1
*/


void filesystem_host_c::inotify_event_eval(struct inotify_event *ino_event)
{
    // 1. find dir/file referenced in ino_event
    bool is_dir = (ino_event->mask & IN_ISDIR) != 0;

	directory_host_c* dir = nullptr ;
	file_host_c* file = nullptr ;
	

//    DEBUG(printf_to_cstr("filesystem_host_c::inotify_event_eval(): %s", inotify_event_as_text(ino_event))) ;
//printf("filesystem_host_c::inotify_event_eval(): %s\n", inotify_event_as_text(ino_event));
    auto it = inotify_watch_map.find(ino_event->wd) ; // search dir for watch event
    assert (it != inotify_watch_map.end()) ; // must be found
    auto parentdir = it->second;

    std::string path ;
    if (ino_event->len == 0) // no name, the watched directory itself changed
        path = parentdir->path ;
    else { // it's path/dir or path/subpath
        // !! identical to later ->get_filepath() result, else mismatch with base->file_by_pathmap entries!
        // path always begins iwth "/"
        if (!parentdir->parentdir) // file direct under root
            path = "/" + std::string(ino_event->name) ;
        else // file on subdir
            path = parentdir->path + "/" + std::string(ino_event->name) ;
    }

    file_base_c *fbase = file_by_path.get(path) ;

    if ((ino_event->mask & (IN_CREATE |IN_MOVED_TO)) && (fbase == nullptr)) {
        // file create event, usable after IN_CLOSE_...
        // fbase may exist, if the file is created by a render() operation
        if (is_dir) {
            fbase = dir = new directory_host_c(std::string(ino_event->name));
            add_directory(parentdir, dir) ;
            dir->inotify_create_pending = true ;
        } else {
            fbase = file = new file_host_c(std::string(ino_event->name)) ;
            parentdir->add_file(file) ;
            file->inotify_create_pending=true ;
        }
    }
    if (ino_event->mask & IN_ATTRIB) {
        assert(fbase != nullptr) ; //  sync fs<->disk: modified must exist
        if (is_dir) {
            dir = dynamic_cast<directory_host_c*>(fbase) ;
            assert(dir != nullptr) ; // existing file must be directory
            dir->load_disk_attributes() ; // update changed attributes
            auto event = new filesystem_host_event_c(filesystem_event_c::op_modify, path, true, dir) ;
            event_queue.push(event) ;
        } else {
            file = dynamic_cast<file_host_c*>(fbase) ;
            assert(file != nullptr) ;
            file->load_disk_attributes() ; // update changed attributes
            auto event = new filesystem_host_event_c(filesystem_event_c::op_modify, path, false, file) ;
            event_queue.push(event) ;
        }
    }
    if (ino_event->mask & IN_MODIFY) {
        assert(fbase != nullptr) ; //  sync fs<->disk: modified must exist
        if (is_dir) {
            dir = dynamic_cast<directory_host_c*>(fbase) ;
            assert(dir != nullptr) ; // existing file must be directory
            dir->inotify_modify_pending = true ;
        } else {
            file = dynamic_cast<file_host_c*>(fbase) ;
            assert(file != nullptr) ;
            file->inotify_modify_pending = true ;
        }
    }
    if ((ino_event->mask & (IN_MOVED_FROM | IN_DELETE | IN_DELETE_SELF)) && (fbase != nullptr)) {
        // fbase is already gone, when remove_from_disk() was called
        auto event = new filesystem_host_event_c(filesystem_event_c::op_delete, path, is_dir, nullptr) ;
        if (is_dir) {
            dir = dynamic_cast<directory_host_c*>(fbase) ;
            assert(dir->file_count() == 0) ; // files and subdir must have been deleted before by other events
            assert(dir != nullptr) ; // existing file must be directory
            remove_directory(dir) ;
        } else {
            file = dynamic_cast<file_host_c*>(fbase) ;
            assert(file != nullptr) ;
            parentdir->remove_file(file) ;
        }
        // send event
        event_queue.push(event) ;
    }

    // CLOSE_WRITE terminates CREATE or MODIFY operations
    // delay event production until now.
    // MOVED_TO is like a CREATE without further events
    //
    // !!! Rename of a file via sftp is a pair CREATE <newfile>, DELETE <oldfile>
    // !!! (instead of MOVED_FROM, MOVED_TO)
	// !!! => WILL NOT WORK, as a CREATE requires a closing CLOSE_WRITE.
    if (ino_event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {

        // 1. Which objects?
        if (is_dir) {
            dir = dynamic_cast<directory_host_c*>(fbase) ;
            assert(dir != nullptr) ; // existing file must be directory
        } else {
            file = dynamic_cast<file_host_c*>(fbase) ;
            assert(file != nullptr) ; // existing file must be directory
        }

        // 2. Which operation?
        bool do_create_dir = false ;
        bool do_create_file = false ;
        bool do_modify_dir = false ;
        bool do_modify_file = false ;

        if (ino_event->mask & IN_MOVED_TO)  {
            if (is_dir)
                do_create_dir = true ;
            else do_create_file = true ;
        }
        if (ino_event->mask & IN_CLOSE_WRITE) {
            if (is_dir) {
                if (dir->inotify_create_pending) // create beats modify
                    do_create_dir = true ;
                else if (dir->inotify_modify_pending)
                    do_modify_dir = true ;
                dir->inotify_create_pending = dir->inotify_modify_pending = false ; // clear state
            } else {
                if (file->inotify_create_pending)
                    do_create_file = true ;
                else if (file->inotify_modify_pending)
                    do_modify_file = true ;
                file->inotify_create_pending = file->inotify_modify_pending = false ;
            }
        }

// rename over sftp: only create without "writeclose", then delete old file

        // 3. Execute
        if (do_create_dir) {
            // single event for created directory or file
            auto event = new filesystem_host_event_c(filesystem_event_c::op_create, path, true, dir) ;
            event_queue.push(event) ;
            dir->load_disk_attributes() ; // refresh properties from disk
        }
        if (do_modify_dir) {
            auto event = new filesystem_host_event_c(filesystem_event_c::op_modify, path, true, dir) ;
            // TODO: if dir its most difficult:
            // erase all subdirs and files, create delete events,
            // rescan all subdirs and files, create "create" events
            event_queue.push(event) ;
            dir->load_disk_attributes() ; // refresh properties from disk
        }
        if (do_create_file) {
            // single event for created directory or file
            auto event = new filesystem_host_event_c(filesystem_event_c::op_create, path, false, file) ;
            event_queue.push(event) ;
            file->load_disk_attributes() ; // refresh properties from disk
        }
        if (do_modify_file) {
            auto event = new filesystem_host_event_c(filesystem_event_c::op_modify, path, false, file) ;
            event_queue.push(event) ;
            file->load_disk_attributes() ; // refresh properties from disk
        }


    }
#ifdef ORG
    // file/dir already created, with *_pending flags
    if (ino_event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO) {
    assert(fbase != nullptr) ; // sync fs<->disk: writted must have been created or exist
        if (is_dir) {
            auto dir = dynamic_cast<directory_host_c*>(fbase) ;
            assert(dir != nullptr) ; // existing file must be directory
            if (dir->inotify_create_pending) {
                // single event for created directory. associated IN_MODIFY is ignored
                auto event = new filesystem_host_event_c(filesystem_event_c::op_create, path, true, dir) ;
                event_queue.push(event) ;
            } else if (dir->inotify_modify_pending) {
                // TODO: most difficult:
                // erase all subdirs and files, create delete events,
                // rescan all subdirs and files, create "create" events
                auto event = new filesystem_host_event_c(filesystem_event_c::op_modify, path, true, dir) ;
                event_queue.push(event) ;
            }
            dir->inotify_create_pending = dir->inotify_modify_pending = false ; // clear state
            dir->load_disk_attributes() ; // refresh dir properties from disk
        } else {
            auto file = dynamic_cast<file_host_c*>(fbase) ;
            assert(file != nullptr) ; // existing file must be directory
            if (file->inotify_create_pending) {
                // single event for created file. associated IN_MODIFY is ignored
                auto event = new filesystem_host_event_c(filesystem_event_c::op_create, path, false, file) ;
                event_queue.push(event) ;
            } else if (file->inotify_modify_pending) {
                // delete/create event pair for modified file
                auto event = new filesystem_host_event_c(filesystem_event_c::op_modify, path, false, file) ;
                event_queue.push(event) ;
            }
            file->inotify_create_pending = file->inotify_modify_pending = false ; // clear state
            file->load_disk_attributes() ; // refresh dir properties from disk
        }
    }
#endif
}


// return static buffer
char *filesystem_host_c::inotify_event_as_text(struct inotify_event *ino_event)
{
    const char *sobj;
    static char buffer[1024];
    char sevents[1024];

    if (ino_event->mask & IN_ISDIR)
        sobj = "directory";
    else
        sobj = "file";

    sevents[0] = 0;

    if (ino_event->mask & IN_IGNORED)
        strcat(sevents, "IN_IGNORED ");
    if (ino_event->mask & IN_UNMOUNT)
        strcat(sevents, "IN_UNMOUNT ");
    if (ino_event->mask & IN_Q_OVERFLOW)
        strcat(sevents, "IN_Q_OVERFLOW ");

    if (ino_event->mask & IN_ACCESS)
        strcat(sevents, "IN_ACCESS");
    if (ino_event->mask & IN_ATTRIB)
        strcat(sevents, "IN_ATTRIB");
    if (ino_event->mask & IN_CLOSE_WRITE)
        strcat(sevents, "IN_CLOSE_WRITE");
    if (ino_event->mask & IN_CLOSE_NOWRITE)
        strcat(sevents, "IN_CLOSE_NOWRITE");
    if (ino_event->mask & IN_CREATE)
        strcat(sevents, "IN_CREATE");
    if (ino_event->mask & IN_DELETE)
        strcat(sevents, "IN_DELETE");
    if (ino_event->mask & IN_DELETE_SELF)
        strcat(sevents, "IN_DELETE_SELF");
    if (ino_event->mask & IN_MODIFY)
        strcat(sevents, "IN_MODIFY");
    if (ino_event->mask & IN_MOVE_SELF)
        strcat(sevents, "IN_MOVE_SELF");
    if (ino_event->mask & IN_MOVED_FROM)
        strcat(sevents, "IN_MOVED_FROM");
    if (ino_event->mask & IN_MOVED_TO)
        strcat(sevents, "IN_MOVED_TO");
    if (ino_event->mask & IN_OPEN)
        strcat(sevents, "IN_OPEN");

    auto it = inotify_watch_map.find(ino_event->wd) ;
    assert (it != inotify_watch_map.end()) ; // must be found
    auto dir = it->second;
    //    auto dir = inotify_watch_map[ino_event->wd] ; does insert if not found!
    sprintf(buffer, "inotify event: wd=%d, mask=%-16s, cookie=%u, %-4s \"dir/name\" = \"%s/%s\".\n",
            ino_event->wd, sevents, ino_event->cookie, sobj,
            dir->path.c_str(),
            ino_event->len ? ino_event->name : "NULL"
           );
    return buffer ;
}


void filesystem_host_c::inotify_test()
{
    int fd;
    int wd1,wd2;
    char buffer[EVENT_BUF_LEN];
//std::string hostdir = "/tmp" ;
    std::string hostdir1 = "/root/10.02_devices/3_test/sharedfilesystem/synthetic";
    std::string hostdir2 = "/root/10.02_devices/3_test/sharedfilesystem/synthetic/dir1";

    /*creating the INOTIFY instance*/
    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    /*checking for error*/
    if (fd < 0) {
        perror("inotify_init1()");
    }

    /* monitor all events on host dir. Must exists.*/

    unsigned mask =
        IN_ACCESS // (+) File was accessed (e.g., read(2), execve(2)).
        | IN_ATTRIB // (*)	Metadata changed�for example, permissions (e.g.,
        //	chmod(2)), timestamps (e.g., utimensat(2)), extended
        // attributes (setxattr(2)), link count (since Linux
        // 2.6.25; e.g., for the target of link(2) and for
        // unlink(2)), and user/group ID (e.g., chown(2)).
        | IN_CLOSE_WRITE // (+) File opened for writing was closed.
        | IN_CLOSE_NOWRITE // (*) File or directory not opened for writing was closed.
        | IN_CREATE // (+) File/directory created in watched directory (e.g.,
        // open(2) O_CREAT, mkdir(2), link(2), symlink(2),
        // bind(2) on a UNIX domain socket).
        | IN_DELETE // (+) File/directory deleted from watched directory.
        | IN_DELETE_SELF // Watched file/directory was itself deleted.	(This
        // event also occurs if an object is moved to another
        // filesystem, since mv(1) in effect copies the file to
        // the other filesystem and then deletes it from the
        // original filesystem.) In addition, an IN_IGNORED
        // event will subsequently be generated for the watch
        // descriptor.
        | IN_MODIFY // (+) File was modified (e.g., write(2), truncate(2)).
        | IN_MOVE_SELF // Watched file/directory was itself moved.
        | IN_MOVED_FROM // (+) Generated for the directory containing the old
        // filename when a file is renamed.
        | IN_MOVED_TO // (+) Generated for the directory containing the new
        // filename when a file is renamed.
        | IN_OPEN // (*) File or directory was opened
        ;
    //	* the events marked above with an asterisk (*) can occur both
    //	  for the directory itself and for objects inside the directory;
    //	* the events marked with a plus sign (+) occur only for objects
    //	  inside the directory (not for the directory itself).
    const char *hostdir_realpath1 = realpath(hostdir1.c_str(), NULL);
    printf("inotify_add_watch(%s)\n", hostdir_realpath1);
    wd1 = ::inotify_add_watch(fd, hostdir_realpath1, mask);

    const char *hostdir_realpath2 = realpath(hostdir2.c_str(), NULL);
    printf("inotify_add_watch(%s)\n", hostdir_realpath2);
    wd2 = ::inotify_add_watch(fd, hostdir_realpath2, mask);



    /*read to determine the event change happens on host directory. Actually this read blocks until the change event occurs*/
    while (true) {
        int length ;
        sleep(1); // wait 1 second, then report events

        length = read(fd, buffer, EVENT_BUF_LEN);

        /*checking for error. EAGAIN means: no data, "Resource temporarily unavailable" */
        if (length < 0 && errno != EAGAIN) {
            fprintf(stderr, "%d = read(), ", length) ;
            perror("read");
        }

        /*actually read return the list of change events happens.
         Here, read the change event one by one and process it accordingly.*/
        int i = 0 ;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            printf("%s\n", inotify_event_as_text(event)) ;

            i += EVENT_SIZE + event->len;
        }
    }
    // never reached
    /*removing the �/tmp� directory from the watch list.*/
    inotify_rm_watch(fd, wd1);
    inotify_rm_watch(fd, wd2);

    /*closing the INOTIFY instance*/
    close(fd);

}


void filesystem_host_c::import_dec_stream(file_dec_stream_c *dec_stream)
{
    std::string dir_path, file_name ;
    assert(dynamic_cast<file_dec_stream_c *>(dec_stream)) ;
    split_path(dec_stream->host_path, &dir_path, &file_name, nullptr, nullptr);
    if (!file_exists(&dir_path)) {
        WARNING("Host: can not import DEC file %s, target dir %s does not exist", dec_stream->host_path.c_str(), dir_path.c_str()) ;
        return ;
    }

    directory_host_c *dir = dynamic_cast<directory_host_c *>(file_by_path.get(dir_path)) ;
    if (dir == nullptr) {
        ERROR("filesystem_host_c::import_dec_stream(): directory %s not found in map", dir_path.c_str()) ;
        return ;
    }

    // create event for existing file/dec_stream? Is acknowledge from DEC, ignore.
    if (file_exists(&dir_path, &file_name)) {
        DEBUG(printf_to_cstr("Host: Ignore \"create\" event for existing file %s", dec_stream->host_path.c_str())) ;
        return ;
    }

    // 1. write to file system
    file_host_c *newfile = new file_host_c( file_name) ;
    newfile->readonly = dec_stream->file->readonly ;
    newfile->file_size = dec_stream->file->file_size ;
    newfile->modification_time = dec_stream->file->modification_time ;
    dir->add_file(newfile) ; // now has a path
    // write file to disk
    newfile->render_to_disk(dec_stream->data_ptr(), dec_stream->size()) ;
    // creates inotify events, which are loop back to decfilesytem and ignored there,
    // because not changing anything.

    newfile->load_disk_attributes() ; // resync
}


void filesystem_host_c::consume_event_do_create(filesystem_dec_event_c *event)
{
    // first: look for existing file by path
    auto f = dynamic_cast<file_host_c*>(file_by_path.get(event->host_path)) ;
    if (f != nullptr) {
        DEBUG("filesystem_host_c::consume_event(): file to be create already there ... DEC ack event.") ;
        return ;
    }

    // event->dec_stream
    if (event->is_dir)	{
        FATAL("%s: consume_event(): Directory import not yet implemented", get_label().c_str()) ;
    } else {
        import_dec_stream(event->dec_stream) ;
    }
    ack_event_filter.add(event->host_path) ;
}

void filesystem_host_c::consume_event_do_delete(filesystem_dec_event_c *event)
{
    // first: look for existing file by path
    auto f = dynamic_cast<file_host_c*>(file_by_path.get(event->host_path)) ;
    if (f == nullptr) {
        DEBUG("filesystem_host_c::consume_event(): f to be deleted not found ... DEC ack event.") ;
        return ;
    }
    if (event->is_dir)	{
        auto dir = dynamic_cast<directory_host_c*>(f) ;
        assert(dir != nullptr) ;
        remove_directory(dir) ;
    } else {
        // get directory
        assert(f->parentdir) ; // not root
        f->remove_from_disk();
        f->parentdir->remove_file(f) ;
    }
    ack_event_filter.add(event->host_path) ;
    //		  delete_dec_file(event->path)
}


// create or delete host files according to DEC change events
void filesystem_host_c::consume_event(filesystem_dec_event_c *event)
{
    DEBUG(printf_to_cstr("filesystem_host_c::consume_event(): %s", event->as_text().c_str())) ;

    if (event->operation == filesystem_event_c::op_create) {
        consume_event_do_create(event) ;
    } else if (event->operation == filesystem_event_c::op_modify) {
        consume_event_do_delete(event) ;
        consume_event_do_create(event) ;
    } else if (event->operation == filesystem_event_c::op_delete) {
        consume_event_do_delete(event) ;
    }
    delete event ;
}



} // namespace

