/* utils.cpp: misc. utilities

 Copyright (c) 2018, Joerg Hoppe
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


 12-nov-2018  JH      entered beta phase
 20-may-2018  JH      created
 */

#define _UTILS_CPP_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <algorithm>

//#include "main.h" // linewidth
#include "logger.hpp"
#include "utils.hpp"

using namespace std;


// singleton
rolling_text_buffers_c rolling_text_buffers ; 


/*********************************
 * strcpy without buffer overlfow
 */
void strcpy_s(char *dest, int len, const char *src) {
	strncpy(dest, src, len - 1);
	dest[len - 1] = 0; // termiante if truncated
}

/*********************************
 *  catching ^C
 */
volatile int SIGINTreceived = 0;

static void SIGINThandler(int dummy __attribute__((unused))) {
	SIGINTreceived++;
	// detach signal handler, only one ^C is caught
	signal(SIGINT, NULL);
}

// catch the next SIGINT = ^C
void SIGINTcatchnext() {
	signal(SIGINT, SIGINThandler); // setup handler for ^C
	SIGINTreceived = 0;
}

void break_here(void) {
}

// progress: print progress text, break lines
progress_c::progress_c(unsigned linewidth) {
	this->linewidth = linewidth;
	cur_col = 0;
}

void progress_c::init(unsigned linewidth) {
	this->linewidth = linewidth;
	cur_col = 0;
}

void progress_c::put(const char *info) {
	cur_col += strlen(info);
	if (cur_col >= linewidth) {
		printf("\n");
		cur_col = strlen(info);
	}
	printf("%s", info);
	fflush(stdout);
}
void progress_c::putf(const char *fmt, ...) {
	static char buffer[256];
	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	vsprintf(buffer, fmt, arg_ptr);
	va_end(arg_ptr);

	put(buffer);
}

/* random number with 24 valid bits
 * RAND_MAX is only guaranteed 15 bits
 */
unsigned random24() {
	unsigned val;
	assert(RAND_MAX >= 0x3fff);
	val = rand() ^ (rand() << 9);
	return val & 0xffffff;
}

/* random numbers, distributed logarithmically 
 * returns 0..limit-1
 */
uint32_t random32_log(uint32_t limit) {
	uint32_t result, mantissa;
	int rand_exponent, limit_exp;
	assert(limit > 0);
	assert(RAND_MAX >= 0x3fff); // 15 bits
	// generate normalized mantissa, bit 31 set
	mantissa = rand();
	mantissa ^= (rand() << 9);
	mantissa ^= (rand() << 18);
	while ((mantissa & (1 << 31)) == 0)
		mantissa <<= 1;
	// rand_exponent of limit: 2^limit_exp <= limit
	// ctz = "Count Leading Zeros"
	// limit = 1 -> exp=0, limit = 0xffffffff -> exp = 31
	limit_exp = 31 - __builtin_clz(limit);
	limit_exp++; // 2^limit_exp >= limit

	// random rand_exponent 0..limit-1
	rand_exponent = rand() % limit_exp;
	// 2^rand_exponent <= limit
	result = mantissa >> (31 - rand_exponent);
	// mantissa has bit 31 set, is never shifted more then 31
	assert(result);

	// final masking
	if (limit > 1)
		result %= limit;
	return result;
}

char *cur_time_text() {
	static char result[80], millibuff[10];
	timeval cur_time;
	gettimeofday(&cur_time, NULL);
	int millis = cur_time.tv_usec / 1000;
	strftime(result, 26, "%H:%M:%S", localtime(&cur_time.tv_sec));
	sprintf(millibuff, ".%03d", millis);
	strcat(result, millibuff);
	return result;
}

bool fileExists(const std::string& filename) {
	struct stat buf;
	if (stat(filename.c_str(), &buf) != -1) {
		return true;
	}
	return false;
}

// Generates "perror()" printout, 
// msgfmt must have one "%s" field for absolute filename
char *fileErrorText(const char *msgfmt, const char *fname) {
	static char linebuff[PATH_MAX + 100];
	char abspath[PATH_MAX];
	realpath(fname, abspath);
	sprintf(linebuff, msgfmt, abspath);
	strcat(linebuff, ": ");
	strcat(linebuff, strerror(errno));
//	perror(linebuff);
	return linebuff;
}

// add a number of microseconds to a time
struct timespec timespec_add_us(struct timespec ts, unsigned us) {
	ts.tv_nsec += us * 1000;
	while (ts.tv_nsec >= BILLION) { // loops only once
		ts.tv_sec++;
		ts.tv_nsec -= BILLION;
	}
	return ts;
}

// add microseconds to current time
struct timespec timespec_future_us(unsigned offset_us) {
	struct timeval tv;
	struct timespec ts;
	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = 1000L * tv.tv_usec;
	return timespec_add_us(ts, offset_us);
}

/*
 bool caseInsCompare(const string& s1, const string& s2) {
 return((s1.size() == s2.size()) &&
 equal(s1.begin(), s1.end(), s2.begin(), caseInsCharCompareN));
 }
 */

// decodes C escape sequences \char, \nnn octal, \xnn hex
// result string is smaller or same as "encoded", must have at least "ncoded" size
// return: true of OK, else false
static int digitval(char c) {
	c = toupper(c);
	if (c < '0')
		return 0; // illegal
	else if (c <= '9')
		return c - '0';
	else if (c < 'A')
		return 0; // illegal
	else if (c <= 'F')
		return c - 'A' + 10;
	else
		return 0; // illegal
}

bool str_decode_escapes(char *result, unsigned result_size, char *encoded) {
	int c ;
	char *wp = result; // write pointer
	char *rp = encoded; // read pointer
	assert(result_size >= strlen(encoded));
	while (*rp) {
		if (*rp != '\\') {
			*wp++ = *rp++; // not escaped
			continue;
		}
		// decode escapes
		rp++; // eat backslash
		int n = strspn(rp, "01234567"); //
		if (n >= 1) { // \nnn given
			// use max 3 digits for octal literal
			c = digitval(*rp++) ;
			if (n >= 2)
				c = c * 8 + digitval(*rp++) ;
			if (n >= 3)
				c = c * 8 + digitval(*rp++) ;
			*wp++ = (char) c;
			continue;
		} 
		switch (*rp) {
		// literals allowed behind backslash
		case '\'': 
		case '"':
		case '?':
		case '\\':
			*wp++ = *rp++;
			continue;
		case 'a':
			*wp++ = 0x07; // audible bell
			rp++;
			continue;
		case 'b':
			*wp++ = 0x08; // backspace
			rp++;
			continue;
		case 'f':
			*wp++ = 0x0c; // form feed - new page
			rp++;
			continue;
		case 'n':
			*wp++ = 0x0a; // line feed - new line
			rp++;
			continue;
		case 'r':
			*wp++ = 0x0d; // carriage return
			rp++;
			continue;
		case 't':
			*wp++ = 0x09; // horizontal tab
			rp++;
			continue;
		case 'v':
			*wp++ = 0x0b; // vertical tab
			rp++;
			continue;
		case 'x': // hex: \xnn
			rp++; // eat "x"
			// in contrast to the standard, max 2 hex digits are evaualted, not arbitrary amount.
			// this makes it easy to write "L 200" as "L\x20200".
			// Else \xnnnn may eat following chars not meant as part of the hex sequence
			// convert and skip arbitrary count of hex characters
			n = strspn(rp, "0123456789aAbBcCdDeEfF"); 
			if (n < 1) 
				return false ; // no hexdigit after "x"
			// use max 2 digits for hex literal
			c = digitval(toupper(*rp++)) ;
			if (n >= 2)
				c = c * 16 + digitval(toupper(*rp++)) ;
			// c = strtol(rp, &rp, 16) ; if unlimited hex chars
			*wp++ = (char) c;
			continue;
		default:
			return false; // unknown char behind backslash
		}
	}
	*wp = 0;
	return true;
}
