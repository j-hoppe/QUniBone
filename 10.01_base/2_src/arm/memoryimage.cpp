/* memoryimage.cpp - reading and saving several memory file formats

 Copyright (c) 2017-2020, Joerg Hoppe, j_hoppe@t-online.de, www.retrocmp.com

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

 6-feb-2020   JH      added code label map
 12-nov-2018  JH      entered beta phase
 18-Jun-2017  JH      created

 */
#define	_MEMORYIMAGE_CPP_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "utils.hpp"
#include "logger.hpp"
#include "memoryimage.hpp" // own

// single multi purpose memory image buffer
memoryimage_c *membuffer;

// add integer to all addresses
void codelabel_map_c::relocate(int delta) 
{
	for (codelabel_map_c::iterator it = begin(); it != end(); ++it) {
		it->second += delta;
	}
}

// print tabular listing of labels
void codelabel_map_c::print(FILE *f) 
{
	unsigned i = 1;
	const char *sep = "Code labels:       "; // 15+4 long
	for (codelabel_map_c::iterator it = begin(); it != end(); ++it) {
		i++;
		// assume format is max abcdefgh=123456 => 4 per row
		fprintf(f, "%s%-8s=%06o", sep, it->first.c_str(), it->second);
		sep = (i % 4) ? "    " : "\n";
	}
	fprintf(f, "\n");
}

void memoryimage_c::init() 
{
	unsigned wordidx;
	for (wordidx = 0; wordidx < MEMORY_WORD_COUNT; wordidx++) {
		data.words[wordidx] = 0;
		valid[wordidx] = false;
	}
}
// fill valid address range with constant word
void memoryimage_c::fill(uint16_t fillword) 
{
	unsigned wordidx;
	for (wordidx = 0; wordidx < MEMORY_WORD_COUNT; wordidx++)
		if (valid[wordidx])
			data.words[wordidx] = fillword;
}

// return # of valid words
unsigned memoryimage_c::get_word_count(void) 
{
	unsigned result = 0;
	unsigned wordidx;
	for (wordidx = 0; wordidx < MEMORY_WORD_COUNT; wordidx++)
		if (valid[wordidx])
			result++;
	return result;
}

// return first and last valid adddres
// first > last: no data set
void memoryimage_c::get_addr_range(unsigned *first, unsigned* last) 
{
	unsigned _first = 0x3ffff;
	unsigned _last = 0;
	unsigned wordidx;
	for (wordidx = 0; wordidx < MEMORY_WORD_COUNT; wordidx++)
		if (valid[wordidx]) {
			unsigned addr = 2 * wordidx;
			if (addr < _first)
				_first = addr;
			if (addr > _last)
				_last = addr;
		}

	if (first)
		*first = _first;
	if (last)
		*last = _last;
}

// set valid address range to [first..last]
void memoryimage_c::set_addr_range(unsigned first, unsigned last) 
{
	unsigned wordidx;
	assert(first <= last);
	assert(last < 2*MEMORY_WORD_COUNT);
	for (wordidx = 0; wordidx < MEMORY_WORD_COUNT; wordidx++) {
		unsigned addr = 2 * wordidx;
		if (addr < first || last < addr)
			valid[wordidx] = false;
		else
			valid[wordidx] = true;
	}
}

/*
 * can set bytes at odd addresses
 */
void memoryimage_c::put_byte(unsigned addr, unsigned b) 
{
	unsigned w;
	unsigned baseaddr = addr & ~1; // clear bit 0
	assert_address(addr);
	w = get_word(baseaddr);
	if (baseaddr == addr) // set lsb
		w = (w & 0xff00) | b;
	else
		w = (w & 0xff) | (b << 8);
	put_word(addr, w);
}

/* load a binary memory image into mem[0] upwards
 * file format: LSB of a word first.
 * all relevant platforms (x64, ARM) are little endian, so
 * the file can just be read into words[]
 * result: false = error, true = OK
 * caller must call init() first !
 */
bool memoryimage_c::load_binary(const char *fname) 
{
	FILE *fin;
	unsigned wordidx, n;
	fin = fopen(fname, "rb");
	if (!fin) {
		printf("%s\n", fileErrorText("Error opening file %s for read", fname));
		return false;
	}
	// try to read max address range, shorter files are OK
	n = fread((void *) data.words, 2, MEMORY_WORD_COUNT, fin);
	for (wordidx = 0; wordidx < n; wordidx++)
		valid[wordidx] = true;
	fclose(fin);
	return true;
}

void memoryimage_c::save_binary(const char *fname, unsigned bytecount) 
{
	FILE *fout;
	unsigned wordcount = (bytecount + 1) / 2;
	unsigned n;
	fout = fopen(fname, "wb");
	if (!fout) {
		printf("%s\n", fileErrorText("Error opening file %s for write", fname));
		return;
	}
	// try to read max address range, shorter files are OK
	n = fwrite((void *) data.words, 2, wordcount, fout);
	fclose(fout);

	if (n != wordcount) {
		WARNING("mem_save_binary(): tried to write %u words, only %u successful.", wordcount,
				n);
	}
}

/*
 * textfile with words separated by white string.
 * 1st word = address, following words data.
 * every char other than [0-7] interpreted as white space
 * Prints "hello world":
 | 002000: 012702 177564 012701 002032 112100 001405 110062 000002
 | 002020: 105712 100376 000771 000000 000763 062510 066154 026157
 | 002040: 073440 071157 062154 006441
 * Or:
 | deposit 002000 012702
 | deposit 002002 177564
 | deposit 002004 012701
 | deposit 002006 002032
 | deposit 002010 112100
 | deposit 002012 001405
 | deposit 002014 110062
 | deposit 002016 000002
 | deposit 002020 105712
 | deposit 002022 100376
 | deposit 002024 000771
 | deposit 002026 000000
 | deposit 002030 000763
 | deposit 002032 062510
 | deposit 002034 066154
 | deposit 002036 026157
 | deposit 002040 073440
 | deposit 002042 071157
 | deposit 002044 062154
 | deposit 002046 006441
 *
 * result: false = error, true = OK
 */
bool memoryimage_c::load_addr_value_text(const char *fname) 
{
	FILE *fin;
	char linebuff[1024];
	char *s, *token, *endptr;
	int n;
	unsigned val;
	unsigned addr;

	fin = fopen(fname, "r");
	if (!fin) {
		printf("%s\n", fileErrorText("Error opening file %s for write", fname));
		return false;
	}
	while (fgets(linebuff, sizeof(linebuff), fin)) {
		// make everything ( including \t, \n ,..) but octal digits a whitespace
		for (s = linebuff; *s; s++)
			if (*s < '0' || *s > '7')
				*s = ' ';
		// parse all worlds, n = word index
		n = 0;
		// parse first token = addr
		token = strtok(linebuff, " ");
		/* walk through data tokens */
		while (token != NULL) {
			val = strtol(token, &endptr, 8);
			if (n == 0) // first token = address
				addr = val;
			else { // following tokens are data
				assert_address(addr);
				put_word(addr, val);
				addr += 2; // inc by word
			}
			n++;
			token = strtok(NULL, " "); // get/test next
		}
	}
	fclose(fin);
	return true;
}

/*
 * Load from a macro 11 listing
 |
 |M9312 'DD' BOOT prom for TU58 D	MACRO V05.05  Tuesday 23-Mar-99 00:02  Page 1
 |
 |
 |      1
 |      2						.title	M9312 'DD' BOOT prom for TU58 DECtapeII serial tape controller (REVISED)
 |      3
 |      4						; Original edition AK6DN Don North,
 |      5					    ; Further processed JH
 |      6
 |      7						; This source code is an M9312 boot PROM for the TU58 version 23-765B9.
 |      8						;
 |      9						; This boot PROM is for the TU58 DECtapeII serial tape controller.
 |     10						;
 |     11						; Multiple units and/or CSR addresses are supported via different entry points.
 |     12						;
 |     13						; Standard devices are 82S131, Am27S13, 74S571 or other compatible bipolar
 |     14						; PROMs with a 512x4 TriState 16pin DIP architecture. This code resides in
 |     15						; the low half of the device; the top half is blank and unused.
 |     16						;
 |     17						; Alternatively, 82S129 compatible 256x4 TriState 16pin DIP devices can be
 |     18						; used, as the uppermost address line (hardwired low) is an active low chip
 |     19						; select (and will be correctly asserted low).
 |     20
 |     21
 |     22						;VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
 |     23						;
 |     24						; The original code in 23-765A9 is REALLY BROKEN when it comes to
 |     25						; supporting a non-std CSR other than 776500 in R1 on entry
 |     26						;
 |     27						; All the hard references to:  ddrbuf, ddxcsr, ddxbuf
 |     28						; have been changed to: 	2(R1),	4(R1),	6(R1)
 |     29						;
 |     30						; The one reference where 'ddrcsr' might have been used is '(R1)' instead
 |     31						; which is actually correct (but totally inconsistent with other usage).
 |     32						;
 |     33						;AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
 |     34
 |     35
 |     36		176500 			ddcsr	=176500 			; std TU58 csrbase
 |     37
 |     38		000000 			ddrcsr	=+0				; receive control
 |     39		000002 			ddrbuf	=+2				; receive data
 |     40		000004 			ddxcsr	=+4				; transmit control
 |     41		000006 			ddxbuf	=+6				; transmit data
 |     42
 |     43	000000					.asect
 |     44		010000 				.=10000
 |     45
 |     46						; --------------------------------------------------
 |     47
 |     48	010000				start:
 |     49
 |     50	010000	000261 			dd0n:	sec				; boot std csr, unit zero, no diags
 |     51	010002	012700 	000000 		dd0d:	mov	#0,r0			; boot std csr, unit zero, with diags
 |     52	010006	012701 	176500 		ddNr:	mov	#ddcsr,r1		; boot std csr, unit <R0>
 |     53
 |     54
 |     55					; mov #101,r3	; transmit A
 |     56					; mov r3,@#176506
 |     57					; halt
 |^L M9312 'DD' BOOT prom for TU58 D	MACRO V05.05  Tuesday 23-Mar-99 00:02  Page 1-1
 |
 |
 |     58					;	 call	 xmtch			; transmit unit number
 |     59					; halt
 |     60					; mov #132,r3	; transmit A
 |     61					;	 call	 xmtch			; transmit unit number
 |     62					; halt
 |     63	010012	012706 	002000 		go:	mov	#2000,sp		; setup a stack
 |     64	010016	005004 				clr	r4			; zap old return address
 |     65	010020	005261 	000004 			inc	ddxcsr(r1)		; set break bit
 |     66	010024	005003 				clr	r3			; data 000,000
 |     67	010026	004767 	000050 			call	xmtch8			; transmit 8 zero chars
 |     68	010032	005061 	000004 			clr	ddxcsr(r1)		; clear break bit
 |     69	010036	005761 	000002 			tst	ddrbuf(r1)		; read/flush any stale rx char
 |     70	010042	012703 	004004 			mov	#<010*400>+004,r3	; data 010,004
 |     71	010046	004767 	000034 			call	xmtch2			; transmit 004 (init) and 010 (boot)
 |     72	010052	010003 				mov	r0,r3			; get unit number
 |     73	010054	004767 	000030 			call	xmtch			; transmit unit number
 |     74
 |     75	010060	005003 				clr	r3			; clear rx buffer ptr
 |     76	010062	105711 			2$:	tstb	(r1)			; wait for rcv'd char available
 |     77	010064	100376 				bpl	2$			; br if not yet
 |     78	010066	116123 	000002 			movb	ddrbuf(r1),(r3)+	; store the char in buffer, bump ptr
 |     79	010072	022703 	001000 			cmp	#1000,r3		; hit end of buffer (512. bytes)?
 |     80	010076	101371 				bhi	2$			; br if not yet
 |     81
 |     82					;	 halt
 |     83	010100	005007 				clr	pc			; jump to bootstrap at zero
 |     84
 |     85	010102				xmtch8: ; transmit 4x the two chars in r3
 |     86	010102	004717 				jsr	pc,(pc) 		; recursive call for char replication
 |     87	010104				xmtch4:
 |     88	010104	004717 				jsr	pc,(pc) 		; recursive call for char replication
 |     89	010106				xmtch2: ; transmit 2 chars in lower r3, upper r3
 |     90	010106	004717 				jsr	pc,(pc) 		; recursive call for char replication
 |     91	010110				xmtch:	; xmt char in lower r3, then swap r3
 |     92	010110	105761 	000004 			tstb	ddxcsr(r1)		; wait for xmit buffer available
 |     93	010114	100375 				bpl	xmtch			; br if not yet
 |     94	010116	110361 	000006 			movb	r3,ddxbuf(r1)		; send the char
 |     95	010122	000303 				swab	r3			; swap to other char
 |     96	010124	000207 				return ; rts	pc			; now recurse or return
 |     97
 |     98		000001 				.end
 |^L M9312 'DD' BOOT prom for TU58 D	MACRO V05.05  Tuesday 23-Mar-99 00:02  Page 1-2
 |Symbol table
 |
 |DDCSR = 176500   	DDRCSR= 000000   	DD0D    010002   	START   010000   	XMTCH4  010104
 |DDNR    010006   	DDXBUF= 000006   	DD0N    010000   	XMTCH   010110   	XMTCH8  010102
 |DDRBUF= 000002   	DDXCSR= 000004   	GO      010012   	XMTCH2  010106
 |
 |. ABS.	010126    000	(RW,I,GBL,ABS,OVR)
 |      	000000    001	(RW,I,LCL,REL,CON)
 |Errors detected:  0
 |
 |*** Assembler statistics
 |
 |
 |Work  file  reads: 0
 |Work  file writes: 0
 |Size of work file: 48 Words  ( 1 Pages)
 |Size of core pool: 15104 Words  ( 59 Pages)
 |Operating  system: RT-11
 |
 |Elapsed time: 00:00:09.30
 |DK:DDBOOT,DK:DDBOOT.LST=DK:DDBOOT


 So:
 - in first line, determine width of line numbers
 - strip off line number in every line
 - 1st octal is address, must be 16 bit
 - following octals are data
 - data may be 3 digits, then 8bit, or 6 digits, then 16 bit.
 - multiple data per line allowed

 Labels can appear with or without octal data
 case 1: standalone
 |      68 007776                         stack	=	. - 2		; stack growns down from start
 |      69
 |      70                                start:
 |      71 010000 012706  007776          	mov	#stack,sp	; init stack
 |      72 010004 005037  177776          	clr	   @#psw	; clear priority, allow all interupts
 case 2: after octal data
 |     62					; xxx
 |     63	010012	012706 	002000 		go:	mov	#2000,sp		; setup a stack
 |     64	010016	005004 				clr	r4			; zap old return address
 |     65	010020	005261 	000004 			inc	ddxcsr(r1)		; set break bit
 *
 */

// remove trailing white space by setting a new \0
static void trim_trail(char *line) 
{
	char *s = line + strlen(line) - 1;

	while (s >= line && isspace(*s)) {
		*s = 0;
		s--;
	}
}

static int calc_lineno_width(char *line) 
{
	// determine end of
	char *s = line;
	while (*s && isspace(*s))
		s++;
	while (*s && isdigit(*s))
		s++;
	// s now on first char after first number
	return s - line;
}

// return the next literal, opcode or label: alphanum, ".", "$"
// tp "token pointer" is moved on
static char *nxt_token(char **tp) 
{
	static char buff[256];
	char *w = buff; // write pointer
	char *r = *tp; // read
	while (*r && strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890$.", toupper(*r)))
		*w++ = *r++;
	*w = 0;
	*tp = r; // return new parse pos
	return buff; // terminate
}

// codelabels: filled with label/address pairs on parse
// result: false = error, true = OK
// caller must call init() first !

bool memoryimage_c::load_macro11_listing(const char *fname, codelabel_map_c *codelabels) 
{
	char map_label_pending[256]; // found label, wait for address in later lines
	unsigned map_address_pending;
	int lineno_fieldwidth = 0;
	FILE *fin;
	char linebuff[1024];
	char *tp; // token pointer, start of cur number
	int tokenidx;
	bool ready;
	unsigned val;
	unsigned addr;
	char *endptr;
	int wlen;

	fin = fopen(fname, "r");
	if (!fin) {
		printf("%s\n", fileErrorText("Error opening file %s", fname));
		return false;
	}
	if (codelabels)
		codelabels->clear();

	map_label_pending[0] = 0;
	while (fgets(linebuff, sizeof(linebuff), fin)) {
		// remove trailing spaces
		trim_trail(linebuff);

		if (strlen(linebuff) == 0)
			continue; // empty line: next line

		// parse only until "Symbol table" in column 1"
		if (!strncasecmp(linebuff, "Symbol table", 12))
			break; // stop parsing

		if (linebuff[0] != ' ' && linebuff[0] != 9)
			continue; // line starts not with white space: header, no numbered line

		if (lineno_fieldwidth == 0)
			lineno_fieldwidth = calc_lineno_width(linebuff); // only once
		tp = linebuff + lineno_fieldwidth; // skip line number, ptr to cur pos

		// process all octal digit strings
		tokenidx = 0; // # of number processed
		ready = false;
		addr = MEMORY_ADDRESS_INVALID;
		map_address_pending = MEMORY_ADDRESS_INVALID;
		while (!ready) {
			// scan line word-wise
			// label - address detctionb: 2 cases, see above
			// case 1: label without addr in smae line, assing first following label
			// case 2: label in same line with <addr> <code...>
			while (*tp && isspace(*tp))
				tp++; // skip white space
			// parse word. check for 8bit octal, 16bit octal, or string
			val = strtol(tp, &endptr, 8);
			wlen = endptr - tp; // len if the numeric word
			if (wlen == 0) {
				// is string. no number => label case 2: label in row with addr/data?
				char *s = nxt_token(&tp);
				if (*tp != ':' || *s == '8' || *s == '9') {
					// exculde non-label strings.
					// labels must be followed by ':'
					// local labels beginning with 0..7 already interpeted numeric
				} else if (map_address_pending != MEMORY_ADDRESS_INVALID) {
					// case 2: address left of label in same line
					if (codelabels) {
						codelabels->add(s, map_address_pending);
						map_address_pending = MEMORY_ADDRESS_INVALID; // address assignedto label
					}
				} else {
					// case 1: label without addr in same line
					strcpy(map_label_pending, s);
				}
				ready = true; // do only process first non-number in line
			} else if (wlen == 6) { // 16 bit value
				if (tokenidx == 0) {
					map_address_pending = addr = val; // 1st number in line : address
					if (map_label_pending[0] && codelabels) {
						// entry label case 1: we hit the entry label before
						codelabels->add(map_label_pending, addr);
						map_label_pending[0] = 0; // pending label resolved
					}
				} else {
					assert_address(addr);
					// if marked with ' (like 165442'): calc offset to PC
					if (*endptr == '\'')
						val = pc_relative_relocation(addr, val) ;

					put_word(addr, val);
					addr += 2; // inc by word
				}
				tokenidx++;
				tp = endptr; // skip this number

			} else if (wlen == 3) { // 8 bit value
				assert_address(addr);
				if (*endptr == '\'')  // see above
					val = pc_relative_relocation(addr, val) ;
				put_byte(addr, val);
				addr++; // inc by byte
				tokenidx++;
				tp = endptr; // skip this number
			} else
				ready = true; // terminate on any unexpected
		}
	}

	fclose(fin);
	return true;
}

/*
 * Load a DEC Standard Absolute Papertape Image file
 * result: false = error, true = OK
 * caller must call init() first !
 * codelabels: contains single "entry" label, if given. or remains empty
 */
bool memoryimage_c::load_papertape(const char *fname, codelabel_map_c *codelabels) 
{
	FILE *fin;
	int b;
	int state = 0;
	int block_byte_idx = 0;
	int block_databyte_count = 0;
	int block_byte_size, sum = 0, addr;
	int stream_byte_index; // absolute index of byte in file

	fin = fopen(fname, "r");
	if (!fin) {
		printf("%s\n", fileErrorText("Error opening file %s for read", fname));
		return false;
	}

	if (codelabels)
		codelabels->clear();

	stream_byte_index = 0;
	block_byte_size = addr = 0; // -Wmaybe-uninitialized
	while (!feof(fin)) {
		b = fgetc(fin);
		// ERROR("[0x%04x] state=%d b=0x%02x sum=0x%02x block_byte_idx=%d",
		// 		stream_byte_index, state, b, sum, block_byte_idx);
		stream_byte_index++;
		switch (state) {
		case 0: // skip bytes until 0x01 is found
			sum = 0;
			if (b == 1) {
				state = 1; // valid header: initialize counter and checksum
				block_byte_idx = 1;
				sum += b;
				sum &= 0xff;
			}
			break;
		case 1: // 0x01 is found, check for following 0x00
			if (b != 0)
				state = 0; // invalid start, skip bytes
			else {
				state = 2;
				block_byte_idx++;
				sum += b;
				sum &= 0xff;
			}
			break;
		case 2:
			// read low count byte
			block_byte_size = b;
			state = 3;
			sum += b;
			sum &= 0xff;
			block_byte_idx++;
			break;
		case 3:
			// read high count byte
			block_byte_size = block_byte_size | (b << 8);
			// calculate data word count
			block_databyte_count = (block_byte_size - 6);
			state = 4;
			sum += b;
			sum &= 0xff;
			block_byte_idx++;
			break;
		case 4:
			// read addr low
			addr = b;
			sum += b;
			sum &= 0xff;
			state = 5;
			block_byte_idx++;
			break;
		case 5:
			// read addr high
			addr = addr | (b << 8);
			state = 6;
			sum += b;
			sum &= 0xff;
			block_byte_idx++;
			if (block_byte_idx > block_byte_size) {
				fprintf(stderr, "Skipping mis-sized block with addr = %06o, size=%d", addr,
						block_byte_size);
				state = 0;
			} else {
				// block range now known
				// if block size = 0: entry addr
				if (block_databyte_count == 0) {
					// only save latest record
					if (codelabels) {
						codelabels->clear();
						codelabels->add("entry", addr);
						// DEBUG(xxx, "Empty block with addr = %06o, block_byte_size=%d", addr, block_byte_size);
					}
					state = 0; // empty block. no chksum ?
				}
				// else INFO("Papertape: Starting data block with addr = %06o, block_byte_size=%d, databyte_count=%d",
				// 			addr, block_byte_size, block_databyte_count);
			}
			break;
		case 6: // read data byte
			assert_address(addr);
			put_byte(addr, b);
			sum += b;
			sum &= 0xff;
			addr += 1;
			block_byte_idx++;
			if (block_byte_idx >= block_byte_size)
				state = 7; // block end
			break;
		case 7:
			// all words of block read, eval checksum
			sum += b;
			sum &= 0xff;
			if (sum != 0) {
				ERROR("Papertape Checksum error chksum = %02X", sum);
				return false;
			}
			// else
			//   INFO("Papertape block end with correct checksum");
			sum = 0;
			state = 0;
		}
	}
	// if (_this->entry_address != -1)
	//	INFO("Papertape entry address = %06o", _this->entry_address);

	fclose(fin);
	return true;
}

// show range, count ,start
// and symbols, if code labels are given
void memoryimage_c::info(FILE *f) 
{
	if (!f)
		return;
	unsigned first_addr, last_addr, wordcount;
	unsigned addr;

	first_addr = MEMORY_WORD_COUNT + 1; // invalid
	last_addr = 0;
	wordcount = 0;
	for (addr = 0; addr < 2 * MEMORY_WORD_COUNT; addr += 2)
		if (is_valid(addr)) {
			if (first_addr > MEMORY_WORD_COUNT)
				first_addr = addr;
			last_addr = addr;
			wordcount++;
		}
	if (wordcount == 0)
		fprintf(f, "memory empty\n");
	else {
		fprintf(f, "memory filled from %06o to %06o with %u words\n", first_addr, last_addr,
				wordcount);
	}
}

void memoryimage_c::dump(FILE *f) 
{
	unsigned addr;
	for (addr = 0; addr < 2 * MEMORY_WORD_COUNT; addr += 2)
		if (is_valid(addr))
			fprintf(f, "%06o %06o\n", addr, get_word(addr));
}
