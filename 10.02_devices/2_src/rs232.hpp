/*
 ***************************************************************************
 *
 * Author: Teunis van Beelen
 *
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017 Teunis van Beelen
 *
 * Email: teuniz@gmail.com
 *
 ***************************************************************************
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***************************************************************************
 */

/* Last revision: August 5, 2017 */

/* For more info and how to use this library, visit: http://www.teuniz.net/RS-232/ */

#ifndef rs232_INCLUDED
#define rs232_INCLUDED

#include <stdio.h>
#include <string.h>

#if defined(__linux__) || defined(__FreeBSD__)

#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/file.h>
#include <errno.h>

#else

#include <windows.h>

#endif

#define RS232_PORTNR  38

class rs232_c {
private:

	int Cport; // file handle of COM port
	int error;

	struct termios new_port_settings, old_port_settings;

public:
	rs232_c();
	unsigned CharTransmissionTime_us;
	int OpenComport(const char *devname, int baudrate, const char *mode, bool par_and_break);
	int PollComport(unsigned char *buf, int size);
	int SendByte(unsigned char byte);
	void LoopbackByte(unsigned char byte);
	int SendBuf(unsigned char *buf, int size);
	void SetBreak(int break_state);
	void CloseComport(void);
	void cputs(const char *);
	int IsDCDEnabled(void);
	int IsCTSEnabled(void);
	int IsDSREnabled(void);
	void enableDTR(void);
	void disableDTR(void);
	void enableRTS(void);
	void disableRTS(void);
	void flushRX(void);
	void flushTX(void);
	void flushRXTX(void);
};

#endif

