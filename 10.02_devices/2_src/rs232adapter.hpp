/* rs232adapter.hpp: route byte xmt/rcv interface to stream and RS232

 Copyright (c) 2019, Joerg Hoppe
 j_hoppe@t-online.de, www.retrocmp.com

 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 JOERG HOPPE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 8-aug-2019	JH      edit start
 */

#ifndef _RS232ADAPTER_HPP_
#define _RS232ADAPTER_HPP_

#include <ostream>
#include <istream>
#include <sstream>
#include <deque>
#include "utils.hpp"
#include "timeout.hpp"
#include "logsource.hpp"
#include "rs232.hpp"

// a character with additional transmission status
typedef struct {
	uint8_t c; // 5/6/7/8 bit character
	// framing and parity errors combined
	bool format_error;
} rs232byte_t;

class rs232adapter_c: public logsource_c {
private:
	// for loopback and to decode 0xff to 0xff,0xff
	std::deque<rs232byte_t> rcvbuffer;
//	std::stringstream rcv_decoder;

// last sequence of xmt data for pattern matching
	static const int pattern_max_len = 256;
	char pattern[pattern_max_len + 1]; // if != "", this is search for
	char pattern_stream_data[pattern_max_len + 1];

	// deliver rcv chars delayed by this "baudrate"
	timeout_c rcv_baudrate_delay;

public:

	rs232adapter_c();

	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	unsigned baudrate; // deliver rcv chars throttled by this "baudrate"

	// if true, an inject 0xff is delivered as 0xff,0xff.
	// this is compatible with termios(3) encoding of error flags
	// If IGNPAR=0, PARMRK=1: error on <char> received as \377 \0 <char> 
	// \377 received as \377 \377
	bool rcv_termios_error_encoding;

	/*** RS232 interface ***/
	rs232_c *rs232; // if assigned, routing to initialized RS232 port

	/*** BYTE interface ***/
	bool rs232byte_rcv_poll(rs232byte_t *rcvbyte);
	void rs232byte_xmt_send(rs232byte_t xmtbyte);
	void rs232byte_loopback(rs232byte_t xmtbyte);

	/*** STREAM interface ***/
	std::istream *stream_rcv; // users sets this to a stream which producess chars
	// may be a stringstream to inject characters

	std::ostream *stream_xmt; // users sets this to a stream in which
	// chars are written to be transferred.
	// may be "cout", or an stringstream 

	/*** PATTERN detection ***/
	void set_pattern(char *pattern);
	bool pattern_found; // switches true on match, user must clear

};

#endif // _RS232ADAPTER_HPP_

