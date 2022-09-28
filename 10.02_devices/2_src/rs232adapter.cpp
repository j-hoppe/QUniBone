/* rs232adapter.cpp: route byte xmt/rcv interface to stream and RS232

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

 This is a stream router.
 - Main interface is a byte-port with sent/poll functions
 - the bytestream can be routed to a RS232 object for TxD and RxD
 - the bytestream can be routed to two streams: rcv/xmt
 - for the xmt stream a pattern matcher is implemented, which search for strigns in the stream

 To be used to router DL11 RCV/XMT ports to RS232 and/or program functions


 .     stream_rcv     stream_xmt                 upper end "STREAM"        .
 .        \ /            / \                                               .
 .         |              |                                                .
 .         |              +---> ringbuffer       "PATTERN"                 .
 .         |              |                                                .
 .         |    loopback  |                                                .
 .        rcv <-----------|---< char_loopback()                            .
 .       buffer           |                                                .
 .         |              |                                                .
 .         +-----<--------|---< rs232.Poll()---< RxD "RS232"               .
 .         |              |                                                .
 .         |              +---> rs232.Send()---> TxD "RS232"               .
 .         |              |                                                .
 .        \ /            / \                                               .
 .   byte_rcv_poll()   byte_xmt_send()           lower end "BYTE"          .
 .                                                                         .
 .      DL11 RCVR         DL11 XMT               DL11                      .
 .         DATI            DATO                  UNIBUS                    .



 */
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "logger.hpp"
#include "rs232adapter.hpp"

rs232adapter_c::rs232adapter_c() {
	log_label = "ADP232";

	rs232 = NULL;
	stream_rcv = NULL;
	stream_xmt = NULL;
	rcv_termios_error_encoding = false;
	rcvbuffer.clear();
	pattern_stream_data[0] = 0;
	pattern[0] = 0;
	pattern_found = false;
	baudrate = 0; // default: no delay
	rcv_baudrate_delay.start_us(0); // start with elapsed() == true"
	*log_level_ptr = LL_DEBUG ; // temporary: log all
}

// BYTE interface: check for received char (from stream or RS232)
// Attention: must produce 0xff 0 sequences for termios encoded byte errors
//	and 0xff 0xff for \ff
// If IGNPAR=0, PARMRK=1: error on <char> received as \377 \0 <char>
// \377 received as \377 \377
// result: true if data received
bool rs232adapter_c::rs232byte_rcv_poll(rs232byte_t *rcvbyte) {

	bool result = false;
	// mixing input streams, with RS232 priority

//	if (baudrate != 0 && !rcv_baudrate_delay.reached())
//		return result; // limit character rate

	pthread_mutex_lock(&mutex);

	// loopback or part of previous 0xff,0xff sequence ?
	result = !rcvbuffer.empty();
	if (result) {
		*rcvbyte = rcvbuffer.front();
		rcvbuffer.pop_front();
	}
	if (!result && rs232) {
		// rs232 must be programmed to generate 0xff 0xff sequences
		/* How to receive framing and parity errors:  see termios(3)
		 If IGNPAR=0, PARMRK=1: error on <char> received as \377 \0 <char> 
		 \377 received as \377 \377
		 */
		uint8_t c_raw;
		rcvbyte->format_error = false; // default: no error info
		int n = rs232->PollComport(&c_raw, 1);
		if (n > 0) {
		//DEBUG("rcv 0x%02x", (unsigned)c_raw) ;
		if (rcv_termios_error_encoding && c_raw == 0xff) {
			n = rs232->PollComport(&c_raw, 1);
			assert(n);	// next char after 0xff escape immediately available

			if (c_raw == 0) { // error flags
				rcvbyte->format_error = true;
				n = rs232->PollComport(&c_raw, 1);
				assert(n); // next char after 0xff 0 seq is data"
				rcvbyte->c = c_raw;
			} else if (c_raw == 0xff) { // encoded 0xff
				rcvbyte->c = 0xff;
			} else {
				WARNING("Received 0xff <stray> sequence");
				rcvbyte->c = c_raw;
			}
		} else
			// received non-escaped data byte
			rcvbyte->c = c_raw;
			}
		result = (n > 0);
	}

	if (!result && stream_rcv) {
		// deliver next char from stream delayed, with simulated baudrate
//		if (baudrate == 0 || rcv_baudrate_delay.reached()) {
		int c = stream_rcv->get();
		if (c != EOF) {
			rcvbyte->c = c;
			rcvbyte->format_error = false;
			result = true;
			 if (baudrate != 0)
				rcv_baudrate_delay.start_us(10 * MILLION / baudrate); // assume 10 bits per char
		}
//		}
	}
//	if (result && baudrate != 0)
//		rcv_baudrate_delay.start_us(10 * MILLION / baudrate); // assume 10 bits per char
//	rcv_baudrate_delay.start_us(MILLION); // 1 sec delay

	pthread_mutex_unlock(&mutex);
//	if (result)
//		printf("< %c\n", *rcvbyte) ;

	return result;
}

void rs232adapter_c::rs232byte_xmt_send(rs232byte_t xmtbyte) {
//	pthread_mutex_lock(&mutex);
//		printf("%c >\n", xmtbyte) ;

	if (rs232)
		rs232->SendByte(xmtbyte.c);
	if (stream_xmt)
		stream_xmt->put(xmtbyte.c);
	// pattern ring buffer
	unsigned n = strlen(pattern);
	if (n) {
		// put new chars at end of string
		unsigned m = strlen(pattern_stream_data);
		assert(m < pattern_max_len);
		pattern_stream_data[m] = xmtbyte.c;
		pattern_stream_data[m + 1] = 0;
		// only keep the last chars in buffer.
		while ((m = strlen(pattern_stream_data)) > n)
			// strip first char, should loop only once
			memmove(pattern_stream_data, pattern_stream_data + 1, m);
		if (strstr(pattern_stream_data, pattern))
			pattern_found = true; // user must clear
	}
	pthread_mutex_unlock(&mutex);
}

void rs232adapter_c::rs232byte_loopback(rs232byte_t xmtbyte) {
	pthread_mutex_lock(&mutex);
	// not a queue, only single char (DL11 loopback)
	// fill intermediate buffer with sequwnce to receive
	rcvbuffer.push_back(xmtbyte);
	pthread_mutex_unlock(&mutex);
}

void rs232adapter_c::set_pattern(char *_pattern) {
	pthread_mutex_lock(&mutex);
	strncpy(pattern, _pattern, pattern_max_len);
	pattern_found = false;
	pattern_stream_data[0] = 0;
	pthread_mutex_unlock(&mutex);
}

