/* error.hpp: global error & info handling

  Copyright (c) 2017, Joerg Hoppe
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

  9-Jul-2018  JH  created
*/

#ifndef _LOGGER_HPP_
#define _LOGGER_HPP_

//#include <pthread.h>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include <mutex>
using namespace std;

#include "logsource.hpp"

// LL = LOG LEVEL
#define LL_FATAL	1
#define LL_ERROR	2
#define LL_WARNING	3
#define LL_INFO		4
#define LL_DEBUG	5

// fifo starts with this size
#define LOG_FIFO_DEFAULT_SIZE	5000
//#define LOG_FIFO_DEFAULT_SIZE	1000

#define LOGMESSAGE_FORMAT_SIZE	1024
// max size of one rendered log message
#define LOGMESSAGE_TEXT_SIZE	10240

// argument for printf in logmessage_t
#define LOGMESSAGE_ARGTYPE	uint32_t
// should be uint64_t on 64 bit platforms

// max # of variable arguments
#define LOGMESSAGE_ARGCOUNT	10

/* many plain C code - because of speed */

// saves a message, for rendering, oro saving in circular buffer
typedef struct {
	bool valid;
//	bool	continuation ; // is it part of message before?
	unsigned id; // unique number, always increments

	unsigned thread_id;
	timeval timestamp; // gettimeofday();
	// format string for printf. Defines interpretation of arg list
	char printf_format[LOGMESSAGE_FORMAT_SIZE]; // max chunk of text which can be output
	// list of args
	LOGMESSAGE_ARGTYPE printf_args[LOGMESSAGE_ARGCOUNT];
	// va_list	print_args ; better, but would need correct tracing of va_end() calls

	logsource_c *logsource; // who generated this message?
	unsigned level; // was generated with this severity. One of LL_*

	const char *source_filename; // C++ source which generated the message
	unsigned source_line; // line # in source file
} logmessage_t;

class logger_c {
private:
	std::mutex fifo_mutex ;
//	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER ;
	// list of registered logsources
	// may have mepty places: NULL
	// index = logsource.log_id
	vector<logsource_c *> logsources;

	unsigned messagecount; // total # of logged messages

	const char *timestamp_text(timeval *tv);
	const char *level_text(unsigned level);

	logmessage_t *fifo; // logging into a ring buffer
	unsigned fifo_capacity; // max # of entries
	unsigned fifo_readidx; // next entry to read
	unsigned fifo_writeidx; // next entry to write
	unsigned fifo_fill; // # of entries between read and write

	void fifo_init(unsigned size);
	void fifo_clear(void);
	// get an entry. 0 = oldest. NULL if idx >= fill
	logmessage_t *fifo_get(unsigned idx);
	// new msg into fifo. oldest may be delted
	void fifo_push(logmessage_t * msg);
	// erase oldest message
	void fifo_pop(void);

	void message_render(char *buffer, unsigned buffer_size, logmessage_t *msg, unsigned style);

public:
	// where to log
	// global last raised error
	// int error_code = ERROR_OK;

	logger_c();
	~logger_c();

	void add_source(logsource_c *logsource);
	void remove_source(logsource_c *logsource);

	unsigned default_level = LL_WARNING;
	string default_filepath; // caller may save a file name here
	void reset_log_levels(void);

	// show messages up to this level immediately on console
	unsigned life_level;

	bool ignored(logsource_c *logsource, unsigned level);

	void	set_fifo_size(unsigned size) ;

	// single portal for all messages
	// message is of verbosity "level", will be output depending on the settings of "logsource"
	void vlog(logsource_c *logsource, unsigned msglevel, const char *srcfilename,
			unsigned srcline, const char *fmt, va_list args);
	void log(logsource_c *logsource, unsigned msglevel, const char *srcfilename,
			unsigned srcline, const char *fmt, ...);

	void debug_hexdump(logsource_c *logsource, const char *info, uint8_t *databuff,
			unsigned databuffsize, void *markptr);

	// buffer interface
	void dump(ostream *stream, unsigned style_title, unsigned style_data); // dump all messages in fifo to stream
	void dump(void); // dump all messages in fifo to console
	void dump(string filepath); // dump all messages into a file
	void clear(void); // clear fifo

};

// the global logger
extern logger_c *logger;

#endif
