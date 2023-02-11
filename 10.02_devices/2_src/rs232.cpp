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

/* 2019, June: made C++, added parity/frame/BREAK option, Joerg Hoppe */
/* Last revision Teunis van Beelen: November 22, 2017 */

/* For more info and how to use this library, visit: http://www.teuniz.net/RS-232/ */

#include "rs232.hpp"

rs232_c::rs232_c() 
{
	CharTransmissionTime_us = 0;
}

// devname without leading "/dev/"
// returns 0 on success, else error
int rs232_c::OpenComport(const char *devname, int baudrate, const char *mode,
		bool par_and_break) 
{
	char full_devname[256];

	int baudr;
	int status;

	strcpy(full_devname, "/dev/");
	strncat(full_devname, devname, 255);
	full_devname[255] = 0;

	switch (baudrate) {
	case 50:
		baudr = B50;
		break;
	case 75:
		baudr = B75;
		break;
	case 110:
		baudr = B110;
		break;
	case 134:
		baudr = B134;
		break;
	case 150:
		baudr = B150;
		break;
	case 200:
		baudr = B200;
		break;
	case 300:
		baudr = B300;
		break;
	case 600:
		baudr = B600;
		break;
	case 1200:
		baudr = B1200;
		break;
	case 1800:
		baudr = B1800;
		break;
	case 2400:
		baudr = B2400;
		break;
	case 4800:
		baudr = B4800;
		break;
	case 9600:
		baudr = B9600;
		break;
	case 19200:
		baudr = B19200;
		break;
	case 38400:
		baudr = B38400;
		break;
	case 57600:
		baudr = B57600;
		break;
	case 115200:
		baudr = B115200;
		break;
	case 230400:
		baudr = B230400;
		break;
	case 460800:
		baudr = B460800;
		break;
	case 500000:
		baudr = B500000;
		break;
	case 576000:
		baudr = B576000;
		break;
	case 921600:
		baudr = B921600;
		break;
	case 1000000:
		baudr = B1000000;
		break;
	case 1152000:
		baudr = B1152000;
		break;
	case 1500000:
		baudr = B1500000;
		break;
	case 2000000:
		baudr = B2000000;
		break;
	case 2500000:
		baudr = B2500000;
		break;
	case 3000000:
		baudr = B3000000;
		break;
	case 3500000:
		baudr = B3500000;
		break;
	case 4000000:
		baudr = B4000000;
		break;
	default:
		printf("invalid baudrate\n");
		return (1);
		break;
	}

	int cbits = CS8;
	int cpar = 0;
	int ipar = IGNPAR;
	int bstop = 0;

	if (strlen(mode) != 3) {
		printf("invalid mode \"%s\"\n", mode);
		return (1);
	}
	unsigned bitcount = 1; // start bit
	switch (mode[0]) {
	case '8':
		cbits = CS8;
		bitcount += 8;
		break;
	case '7':
		cbits = CS7;
		bitcount += 7;
		break;
	case '6':
		cbits = CS6;
		bitcount += 6;
		break;
	case '5':
		cbits = CS5;
		bitcount += 5;
		break;
	default:
		printf("invalid number of data-bits '%c'\n", mode[0]);
		return (1);
		break;
	}

	switch (mode[1]) {
	case 'N':
	case 'n':
		cpar = 0;
		ipar = IGNPAR;
		break;
	case 'E':
	case 'e':
		cpar = PARENB;
		ipar = INPCK;
		bitcount += 1;
		break;
	case 'O':
	case 'o':
		cpar = (PARENB | PARODD);
		ipar = INPCK;
		bitcount += 1;
		break;
	default:
		printf("invalid parity '%c'\n", mode[1]);
		return (1);
		break;
	}

	switch (mode[2]) {
	case '1':
		bstop = 0;
		bitcount += 1;
		break;
	case '2':
		bstop = CSTOPB;
		bitcount += 2;
		break;
	default:
		printf("invalid number of stop bits '%c'\n", mode[2]);
		return (1);
		break;
	}
	// bit count is 10 for 8N1
	// Calc time to transmit on character
	CharTransmissionTime_us = (1000000 * bitcount) / baudrate;

	/* scan for BREAK and frame/parity errors?
	 To read BREAK not as \0:
	 PARMRK=1 and parity checking -> BREAK violates frame pattern -> is recieved as \377 \0 \0
	 */
	int iflag;
	if (par_and_break)
		iflag = PARMRK | INPCK;
	else
		iflag = ipar;

	/*
	 http://pubs.opengroup.org/onlinepubs/7908799/xsh/termios.h.html

	 http://man7.org/linux/man-pages/man3/termios.3.html
	 */

	Cport = open(full_devname, O_RDWR | O_NOCTTY | O_NDELAY);
	if (Cport == -1) {
		perror("unable to open comport ");
		return (1);
	}

	/* lock access so that another process can't also use the port */
	if (flock(Cport, LOCK_EX | LOCK_NB) != 0) {
		close(Cport);
		perror("Another process has locked the comport.");
		return (1);
	}

	error = tcgetattr(Cport, &old_port_settings);
	if (error == -1) {
		close(Cport);
		flock(Cport, LOCK_UN); /* free the port so that others can use it. */
		perror("unable to read portsettings ");
		return (1);
	}
	memset(&new_port_settings, 0, sizeof(new_port_settings)); /* clear the new struct */

	new_port_settings.c_cflag = cbits | cpar | bstop | CLOCAL | CREAD;
	new_port_settings.c_iflag = iflag;
	new_port_settings.c_oflag = 0;
	new_port_settings.c_lflag = 0;
	new_port_settings.c_cc[VMIN] = 0; /* block until n bytes are received */
	new_port_settings.c_cc[VTIME] = 0; /* block until a timer expires (n * 100 mSec.) */

	cfsetispeed(&new_port_settings, baudr);
	cfsetospeed(&new_port_settings, baudr);

	error = tcsetattr(Cport, TCSANOW, &new_port_settings);
	if (error == -1) {
		tcsetattr(Cport, TCSANOW, &old_port_settings);
		close(Cport);
		flock(Cport, LOCK_UN); /* free the port so that others can use it. */
		perror("unable to adjust portsettings ");
		return (1);
	}

	/* http://man7.org/linux/man-pages/man4/tty_ioctl.4.html */

	if (ioctl(Cport, TIOCMGET, &status) == -1) {
		tcsetattr(Cport, TCSANOW, &old_port_settings);
		flock(Cport, LOCK_UN); /* free the port so that others can use it. */
		perror("unable to get portstatus");
		return (1);
	}

	status |= TIOCM_DTR; /* turn on DTR */
	status |= TIOCM_RTS; /* turn on RTS */

	if (ioctl(Cport, TIOCMSET, &status) == -1) {
		tcsetattr(Cport, TCSANOW, &old_port_settings);
		flock(Cport, LOCK_UN); /* free the port so that others can use it. */
		perror("unable to set portstatus");
		return (1);
	}

	return (0);
}

int rs232_c::PollComport(unsigned char *buf, int size) 
{
	int n;

	n = read(Cport, buf, size);

	if (n < 0) {
		if (errno == EAGAIN)
			return 0;
	}

	return (n);
}

int rs232_c::SendByte(unsigned char byte) 
{
	int n = write(Cport, &byte, 1);
	if (n < 0) {
		if (errno == EAGAIN) {
			return 0;
		} else {
			return 1;
		}
	}
	return (0);
}

int rs232_c::SendBuf(unsigned char *buf, int size) 
{
	int n = write(Cport, buf, size);
	if (n < 0) {
		if (errno == EAGAIN) {
			return 0;
		} else {
			return -1;
		}
	}

	return (n);
}

// put byte in to rcv queue
void rs232_c::LoopbackByte(unsigned char byte) 
{
	if (ioctl(Cport, TIOCSTI, &byte) == -1) {
		perror("unable to insert byte into input queue");
	}
}

void rs232_c::SetBreak(int break_state) 
{
	if (ioctl(Cport, break_state ? TIOCSBRK : TIOCCBRK) == -1) {
		perror("unable to set break status");
	}
}

void rs232_c::CloseComport(void) 
{
	int status;
	CharTransmissionTime_us = 0;

	if (ioctl(Cport, TIOCMGET, &status) == -1) {
		perror("unable to get portstatus");
	}

	status &= ~TIOCM_DTR; /* turn off DTR */
	status &= ~TIOCM_RTS; /* turn off RTS */

	if (ioctl(Cport, TIOCMSET, &status) == -1) {
		perror("unable to set portstatus");
	}

	tcsetattr(Cport, TCSANOW, &old_port_settings);
	close(Cport);

	flock(Cport, LOCK_UN); /* free the port so that others can use it. */
}

/*
 Constant  Description
 TIOCM_LE        DSR (data set ready/line enable)
 TIOCM_DTR       DTR (data terminal ready)
 TIOCM_RTS       RTS (request to send)
 TIOCM_ST        Secondary TXD (transmit)
 TIOCM_SR        Secondary RXD (receive)
 TIOCM_CTS       CTS (clear to send)
 TIOCM_CAR       DCD (data carrier detect)
 TIOCM_CD        see TIOCM_CAR
 TIOCM_RNG       RNG (ring)
 TIOCM_RI        see TIOCM_RNG
 TIOCM_DSR       DSR (data set ready)

 http://man7.org/linux/man-pages/man4/tty_ioctl.4.html
 */

int rs232_c::IsDCDEnabled(void) 
{
	int status;

	ioctl(Cport, TIOCMGET, &status);

	if (status & TIOCM_CAR)
		return (1);
	else
		return (0);
}

int rs232_c::IsCTSEnabled(void) 
{
	int status;

	ioctl(Cport, TIOCMGET, &status);

	if (status & TIOCM_CTS)
		return (1);
	else
		return (0);
}

int rs232_c::IsDSREnabled(void) 
{
	int status;

	ioctl(Cport, TIOCMGET, &status);

	if (status & TIOCM_DSR)
		return (1);
	else
		return (0);
}

void rs232_c::enableDTR(void) 
{
	int status;

	if (ioctl(Cport, TIOCMGET, &status) == -1) {
		perror("unable to get portstatus");
	}

	status |= TIOCM_DTR; /* turn on DTR */

	if (ioctl(Cport, TIOCMSET, &status) == -1) {
		perror("unable to set portstatus");
	}
}

void rs232_c::disableDTR(void) 
{
	int status;

	if (ioctl(Cport, TIOCMGET, &status) == -1) {
		perror("unable to get portstatus");
	}

	status &= ~TIOCM_DTR; /* turn off DTR */

	if (ioctl(Cport, TIOCMSET, &status) == -1) {
		perror("unable to set portstatus");
	}
}

void rs232_c::enableRTS(void) 
{
	int status;

	if (ioctl(Cport, TIOCMGET, &status) == -1) 
		{
		perror("unable to get portstatus");
	}

	status |= TIOCM_RTS; /* turn on RTS */

	if (ioctl(Cport, TIOCMSET, &status) == -1) 
		{
		perror("unable to set portstatus");
	}
}

void rs232_c::disableRTS(void) 
{
	int status;

	if (ioctl(Cport, TIOCMGET, &status) == -1) {
		perror("unable to get portstatus");
	}

	status &= ~TIOCM_RTS; /* turn off RTS */

	if (ioctl(Cport, TIOCMSET, &status) == -1) {
		perror("unable to set portstatus");
	}
}

void rs232_c::flushRX(void) 
{
	tcflush(Cport, TCIFLUSH);
}

void rs232_c::flushTX(void) 
{
	tcflush(Cport, TCOFLUSH);
}

void rs232_c::flushRXTX(void) 
{
	tcflush(Cport, TCIOFLUSH);
}

void rs232_c::cputs(const char *text) /* sends a string to serial port */
{
	while (*text != 0)
		SendByte(*(text++));
}

