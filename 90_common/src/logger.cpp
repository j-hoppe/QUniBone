/* logger.cpp: global error & info handling

 Copyright (c) 2018, Joerg Hoppe
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


 12-nov-2018  JH      entered beta phase
 09-Jul-2018  JH      created

 - Can route messages to console and into a file
 - loglevels Error, Warning, Info and Debug
 - user sees Info and Debug if "verbose"
 "Debug" messages are marked with "log channel" bitmask, to
 enable only selected channels
 */

#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include <unistd.h>		// mark DEBUG with thread ID
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>

#include "logger.hpp"  // own

/**** singleton ***/
logger_c *logger;

#define RENDER_STYLE_NONE	0
#define RENDER_STYLE_CONSOLE	1
#define RENDER_CSV_TITLES	2
#define RENDER_CSV_DATA	3

// constructor
logger_c::logger_c() 
{
//	mutex = PTHREAD_MUTEX_INITIALIZER ;
	fifo = NULL;
	fifo_init(LOG_FIFO_DEFAULT_SIZE);
	messagecount = 0;
	life_level = default_level;
	logsources.clear();
//	pthread_mutex_destroy(&mutex);
}

logger_c::~logger_c() 
{
	fifo_init(0); // free buffer
}

// register a source, set its id
void logger_c::add_source(logsource_c *logsource) 
{
	unsigned id;
	// initialize each object log level with global default
	*(logsource->log_level_ptr) = default_level;

	for (id = 0; id < logsources.size(); id++)
		if (logsources[id] == NULL) { // found unused place
			logsources[id] = logsource;
			logsource->log_id = id;
			return;
		}

	// no free entry found:
	logsources.push_back(logsource);
	logsource->log_id = logsources.size() - 1;
}

// just set the pointer to NULL
void logger_c::remove_source(logsource_c *logsource) 
{
	unsigned id = logsource->log_id;
	logsources[id] = NULL;
}

// set log levels of all soruces back to "default"
void logger_c::reset_log_levels() 
{
	unsigned id;
	for (id = 0; id < logsources.size(); id++) {
		logsource_c *logsource = logsources[id];
		if (logsource != NULL) // found unused place
			*(logsource->log_level_ptr) = default_level;
	}
}

// set new size
// 0 <=> fifo NULL
void logger_c::fifo_init(unsigned size) 
{
	unsigned idx;
	if (fifo) {
		free(fifo);
		fifo = NULL;
		fifo_capacity = 0;
	}
	fifo_capacity = size;
	if (fifo_capacity > 0) {
		fifo = (logmessage_t *) malloc(fifo_capacity * sizeof(logmessage_t));
		assert(fifo);
	}
	for (idx = 0; idx < fifo_capacity; idx++)
		fifo[idx].valid = false;

	fifo_clear();
}

void logger_c::fifo_clear(void) 
{
	messagecount = 0;
	fifo_fill = fifo_readidx = fifo_writeidx = 0;
}

// get an entry. 0 = oldest. NULL if idx >= fill
logmessage_t *logger_c::fifo_get(unsigned idx_rel) 
{
	logmessage_t *result;
	unsigned idx_abs;

	if (idx_rel >= fifo_fill)
		return NULL;
	// calc absolute idx in ring buffer
	idx_abs = (fifo_readidx + idx_rel) % fifo_capacity;
	result = &(fifo[idx_abs]);
//	printf("get(): msg %u = %p\n", idx_abs, result) ;
	return result;
}

// new msg into fifo. oldest may be deleted
void logger_c::fifo_push(logmessage_t * msg) 
{
	if (fifo_fill >= (fifo_capacity - 1))
		// fifo full: delete oldest
		fifo_pop();
	fifo[fifo_writeidx] = *msg; // copy into
//printf("psh(): msg %u = %p", fifo_writeidx, &(fifo[fifo_writeidx])) ;
			// inc write pointer, roll around
	fifo_writeidx = (fifo_writeidx + 1) % fifo_capacity;
	fifo_fill++;
//printf(" => idx=%u, fill=%u\n", fifo_writeidx,	fifo_fill) ;
}

// erase oldest message
void logger_c::fifo_pop(void) 
{
	if (fifo_fill == 0)
		return;
	// inc read pointer, roll around
	fifo_readidx = (fifo_readidx + 1) % fifo_capacity;
	fifo_fill--;
	// emtpy: readptr == writeptr
	assert(fifo_fill != 0 || fifo_readidx == fifo_writeidx);
}

const char * logger_c::level_text(unsigned level) 
{
	switch (level) {
	case LL_FATAL:
		return "FATAL";
	case LL_ERROR:
		return "ERR";
	case LL_WARNING:
		return "WRN";
	case LL_INFO:
		return "Inf";
	case LL_DEBUG:
		return "Dbg";
	default:
		return "ILLEGAL_LEVEL";
	}
}

// is output with verbosity "level" active for logsource?
bool logger_c::ignored(logsource_c *logsource, unsigned msglevel) 
{
	if (msglevel == LL_FATAL)
		return false; // never ignored
	if (msglevel > *(logsource->log_level_ptr))
		return true;
	return false;
}

const char *logger_c::timestamp_text(timeval *tv) 
{
	static char result[80], millibuff[10];
//	int millis = tv->tv_usec / 1000;
	strftime(result, 26, "%H:%M:%S", localtime(&tv->tv_sec));
	sprintf(millibuff, ".%06ld", tv->tv_usec);
//	sprintf(millibuff, ".%03d", millis);
	strcat(result, millibuff);
	return result;
}

/* convert to text
 possible enhancements:
 - select which fields to show
 -select between "nice" format and CSV
 Format is:
 CONSOLE: " [timestamp level logsource thread@srcfilename:line] <printf()>"
 CSV_TITLE
 CSV_LINE
 */

void logger_c::message_render(char *buffer, unsigned buffer_size, logmessage_t *msg,
		unsigned style) 
{

	char fmtbuffer[LOGMESSAGE_TEXT_SIZE];

	assert(buffer_size >= LOGMESSAGE_TEXT_SIZE);

	if (style == RENDER_CSV_TITLES) {
		strcpy(buffer, "id;timestamp;level;source;thread;file;line;message");
	} else {
		// data!
		char *wp = buffer; // write pointer
		int chars_written = 0;

		// very long text? 10000 = reserve for % place holder expansion
		assert(buffer_size > (strlen(msg->printf_format) + 1000));
		assert(strlen(msg->logsource->log_label .c_str())); // forgotten?
		switch (style) {
		case RENDER_STYLE_CONSOLE:

			if (msg->level >= LL_DEBUG && msg->source_filename != NULL) {
				// full format with source file: assemble format
				strcpy(fmtbuffer, "[%s %s %6s %05u@%s:%04u] ");
				chars_written = sprintf(wp, fmtbuffer, timestamp_text(&msg->timestamp),
						level_text(msg->level), msg->logsource->log_label.c_str(),
						(unsigned) msg->thread_id, msg->source_filename, msg->source_line);
			} else {
				// without source_file and line
				strcpy(fmtbuffer, "[%s %s %6s] ");
				chars_written = sprintf(wp, fmtbuffer, timestamp_text(&msg->timestamp),
						level_text(msg->level), msg->logsource->log_label.c_str());
			}
			break;
		case RENDER_CSV_DATA:
			// full format with source file: assemble format
			strcpy(fmtbuffer, "%u;%s;%s;%s;%u;%s;%u;");
			chars_written = sprintf(wp, fmtbuffer, msg->id, timestamp_text(&msg->timestamp),
					level_text(msg->level), msg->logsource->log_label.c_str(),
					(unsigned) msg->thread_id, msg->source_filename, msg->source_line);
			break;
		}
		// print actual message behind header, at wp
		wp += chars_written;
		chars_written = snprintf(wp, buffer_size - chars_written, msg->printf_format,
				msg->printf_args[0], msg->printf_args[1], msg->printf_args[2],
				msg->printf_args[3], msg->printf_args[4], msg->printf_args[5],
				msg->printf_args[6], msg->printf_args[7], msg->printf_args[8],
				msg->printf_args[9]);
		/*
		 chars_written = vsnprintf(wp, buffer_size - chars_written, msg->printf_format,
		 msg->print_args) ;
		 va_end(msg->print_args) ; // free parameter list after use
		 */

		wp += chars_written;
		*wp = 0; // necessary for snprintf() ?
		// strip of optional trailing \n, will be added in dump()
		if (wp[-1] == '\n')
			wp[-1] = 0;
	}
}

void logger_c::set_fifo_size(unsigned size) 
{
	fifo_init(size);
}

// single portal for all messages

// See http://c-faq.com/varargs/handoff.html
// for log/vlog or printf/vprintf)

volatile int m1 = 0;
void logger_c::vlog(logsource_c *logsource, unsigned msglevel, const char *srcfilename,
		unsigned srcline, const char *fmt, va_list args) 
{
	logmessage_t msg;
	if (ignored(logsource, msglevel))
		return; // don't output

	fifo_mutex.lock();
//	pthread_mutex_lock (&mutex);
	assert(!m1);
	m1++;

	gettimeofday(&msg.timestamp, NULL);
	msg.id = messagecount++;
	msg.logsource = logsource;
	msg.level = msglevel;
	msg.thread_id = syscall(SYS_gettid);
	msg.source_filename = basename(srcfilename); // may be full path
	msg.source_line = srcline;

	assert(sizeof(msg.printf_format) > strlen(fmt) + 1);
	strcpy(msg.printf_format, fmt);

	assert(LOGMESSAGE_ARGCOUNT >= 10);
	/*
	 va_copy(msg.print_args, args) ; // same arguments
	 */
	msg.printf_args[0] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[1] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[2] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[3] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[4] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[5] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[6] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[7] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[8] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.printf_args[9] = va_arg(args, LOGMESSAGE_ARGTYPE);
	msg.valid = true;

	fifo_push(&msg); // always into ring buffer

	// print message immediately
	if (msglevel <= life_level) {
		char msgtext[LOGMESSAGE_TEXT_SIZE];
		message_render(msgtext, sizeof(msgtext), &msg, RENDER_STYLE_CONSOLE);
		std::cout << msgtext << "\n";
		// cout << string(msgtext) << "\n"; // not thread safe???
	}

	m1--;
	fifo_mutex.unlock();
//	pthread_mutex_unlock (&mutex);

	// stop program
	if (msglevel == LL_FATAL) {
		exit(1);
	}
}

void logger_c::log(logsource_c *logsource, unsigned msglevel, const char *srcfilename,
		unsigned srcline, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	vlog(logsource, msglevel, srcfilename ? srcfilename:"", srcline, fmt, args);
	va_end(args);
}

/* dump buffer as hexdump at DEBUG level
 * "markptr" = position im buffer, where a >x< should be placed
 */
void logger_c::debug_hexdump(logsource_c *logsource, const char *info, uint8_t *databuff,
		unsigned databuffsize, void *markptr) 
{
	// whole dump is one big string
	char message_buffer[5000]; // 16 lines need about 1K
	char phrase_buffer[80];
	unsigned max_linelen = 80;
	char *wp;	// write pointer in outbuff
	char sep = ' ';  // separator char between bytes
	unsigned zero_count = 0;     // stop output after too many 0x00's
	bool early_end = false; // terminate dump?
	uint8_t *curaddr;
	unsigned i;

	wp = message_buffer;
	assert(info);	 // must give an info
	strcpy(wp, info);
	wp += strlen(info);
	sep = '\0'; // no

	for (i = 0; !early_end && i < databuffsize; i++) {
		if (i % 16 == 0) {
			// new line
			if (sep)
				*wp++ = sep; // pending < marker?
			// line with address prefix: +0x123
			sprintf(phrase_buffer, "\n+0x%03x: ", i);
			*wp = 0;
			strcat(wp, phrase_buffer);
			wp += strlen(phrase_buffer);
			sep = ' ';
		} else if (i % 8 == 0) {
			*wp++ = sep; // marker?
			sep = ' ';
			*wp++ = '-';
		}
		curaddr = databuff + i;

		if (curaddr == markptr) {
			// before uint8_t a ">", after that a "<"
			*wp++ = '>';
			sep = '<';
		} else {
			*wp++ = sep;            // Space vor jeder Zahl
			sep = ' ';
		}
		// *curaddr = current uint8_t from buffer
#define HEXSYM(n) ( (n) < 10 ? '0' + (n) : 'a' + (n) - 10 )
		*wp++ = HEXSYM(*curaddr / 16);
		*wp++ = HEXSYM(*curaddr % 16);
		if (*curaddr == 0x00)
			zero_count++;
		else
			zero_count = 0;
		// buffer full ??
		if (((unsigned) (wp - message_buffer) + max_linelen) > sizeof(message_buffer))
			// buffer almost filled
			early_end = true;

		// do not suppress multiple zeros:  would set early_end
	}

	if (early_end) {
		*wp++ = ' ';
		*wp++ = '.';
		*wp++ = '.';
		*wp++ = '.';
	}
	*wp = '\0';
	// do not output "%" chars!
	log(logsource, LL_DEBUG, NULL, 0, message_buffer);
//	DEBUG(channelmask, message_buffer);
}

// buffer interface

void logger_c::dump(std::ostream *stream, unsigned style_title, unsigned style_data) 
{
	logmessage_t *msg;
	unsigned idx = 0;

	//pthread_mutex_lock(&mutex);
	fifo_mutex.lock();

	// optional title row
	if (style_title) {
		char msgtext[LOGMESSAGE_TEXT_SIZE];
		message_render(msgtext, sizeof(msgtext), NULL, style_title);
		*stream << std::string(msgtext) << "\n";
	}

	// dump all message in fifo in sequential order
	while ((msg = fifo_get(idx++))) {
		assert(msg->valid);
		char msgtext[LOGMESSAGE_TEXT_SIZE];
		message_render(msgtext, sizeof(msgtext), msg, style_data);
		*stream << std::string(msgtext) << "\n";
	}
	fifo_mutex.unlock();
//	pthread_mutex_unlock(&mutex);
}

// dump all messages in fifo to console
void logger_c::dump(void) 
{
	dump(&std::cout, RENDER_STYLE_NONE, RENDER_STYLE_CONSOLE);
}

// dump all messages into a file
void logger_c::dump(std::string filepath) 
{
	std::ofstream file_stream;
	file_stream.open(filepath, std::ofstream::out | std::ofstream::trunc);
	if (!file_stream.is_open()) {
		std::cout << "Can not open log file \"" << filepath << "\"! Aborting!\n";
		exit(2);
	}
	dump(&file_stream, RENDER_CSV_TITLES, RENDER_CSV_DATA);

	file_stream.close();
	std::cout << "Dumped " << fifo_fill << " log messages to file \"" << filepath << "\".\n";
}

// clear fifo
void logger_c::clear(void) 
{
	fifo_clear();
}

