/* application.cpp:  QUniBone "demo" application, global resources

 Copyright (c) 2018, Joerg Hoppe, j_hoppe@t-online.de, www.retrocmp.com

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
 14-May-2018 	JH      created

 */

#define _MAIN_C_

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
//#include <string.h>
//#include <stdint.h>
#include <unistd.h>
#include <limits.h>
//#include <errno.h>
//#include <ctype.h>
#include <stdarg.h>
#include <strings.h>
#include <string>
#include <iostream>

using namespace std;

#include "logsource.hpp"
#include "logger.hpp"
#include "timeout.hpp"
#include "getopt2.hpp"
#include "kbhit.h"
#include "inputline.hpp"
#include "pru.hpp"
#include "mailbox.h"
#include "gpios.hpp"
#include "buslatches.hpp"
#include "qunibussignals.hpp"
#include "memoryimage.hpp"
#include "iopageregister.h"
#include "panel.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"

#include "logger.hpp"
#include "application.hpp"   // own

// Singleton
application_c *app;


application_c::application_c() {
	log_label = "APP";
}

/*
 * help()
 */
void application_c::help() {
	cout << "\n";
	cout << "NAME\n";
	cout << "\n";
	cout << version << "\n";
	cout << copyright << "\n";
	cout << "\n";
	cout << "SYNOPSIS\n";
	cout << "\n";
	cout << "TEST SETUP\n";
	cout << "  - UniBone must be plugged into SPC slots C-F on DD11-CK backplane.\n";
	cout << "  - 2 passive M903 terminators plugged into backplane.\n";
	cout << "  - Short cut BR4,5,6,NPR IN/OUT with jumpers.\n";
	cout << "  - Install the \"PRU\" device tree overlay:\n";
	cout << "    - cp UniBone-00B0.dtbo /lib/firmware UniBone-00B0.dtbo\n";
	cout << "    - reboot\n";
	cout << "\n";
//	cout << "Command line options are processed strictly left-to-right. \n\n");
	// getopt must be initialized to print the syntax
	getopt_parser.help(cout, opt_linewidth, 10, PROGNAME);
	cout << "\n";
	cout << "EXAMPLES\n";
	cout << "\n";
	cout << "sudo ./" PROGNAME "\n";
	cout << "    Show interactive menus.\n";
	cout << "\n";

	exit(1);
}

// show error for one option
void application_c::commandline_error() {
	cerr << "Error while parsing command line:\n";
	cerr << "  " << getopt_parser.curerrortext.c_str() << "\n";
	exit(1);
}

// parameter wrong for currently parsed option
void application_c::commandline_option_error(char *errtext, ...) {
	char buffer[1024];
	va_list args;
	cerr << "Error while parsing commandline option:\n";
	if (errtext) {
		va_start(args, errtext);
		vsprintf(buffer, errtext, args);
		cerr << buffer << "\nSyntax:  ";
		va_end(args);
	} else
		cerr << "  " << getopt_parser.curerrortext << "\nSyntax:  ";
	getopt_parser.help_option(cerr, 96, 10);
	exit(1);
}

/* check whether the given device parameter configuration
 * my cause problems.
 */

/*
 * read commandline parameters into global "param_" vars
 * result: 0 = OK, 1 = error
 */
void application_c::parse_commandline(int argc, char **argv) {
	int res;

	// define commandline syntax
	getopt_parser.init(/*ignore_case*/1);

//	getopt_def(&getopt_parser, NULL, NULL, "hostname", NULL, NULL, "Connect to the Blinkenlight API server on <hostname>\n"
//		"<hostname> may be numerical or ar DNS name",
//		"127.0.0.1", "connect to the server running on the same machine.",
//		"raspberrypi", "connected to a RaspberryPi with default name.");

	// !!!1 Do not define any defaults... else these will be set very time!!!

	getopt_parser.ignore_case = 1;
	getopt_parser.define("?", "help", "", "", "", "Print help.", "", "", "", "");
	getopt_parser.define("v", "verbose", "", "", "", "Print info about operation.", "", "", "",
			"");
	getopt_parser.define("dbg", "debug", "", "", "", "Print debug messages.\n"
	// getopt_parser.define("dbg", "debug", "", "channelmask", "",
	//		"Print debug messages. Optional reduces to channels wit <channelmask>.\n"
					"Outputfile is \"unibone.log\"", "", "", "", "");
	getopt_parser.define("cf", "cmdfile", "cmdfilename", "", "",
			"File from which commands are read.\n"
					"Lines are processed as if typed in.", "testseq",
			"read commands from file \"testseq\" and execute line by line", "", "");
#if defined(QBUS)	
	getopt_parser.define("aw", "addresswidth", "addresswidth", "", "",
			"Force address width of QBUS: 16, 18, 22.\nUse if auto-probing not desired.", "",
			"", "", "");
#endif
	getopt_parser.define("leds", "leds", "ledcode", "", "",
			"Display decimal number 0..15 on 4 binary LEDs.", "",
			"", "", "");
	// test options

	getopt_parser.define("t", "test", "iarg1,iarg2", "soptarg", "8 15",
			"Tests the new c++ getop2.cpp\n"
					"Multiline info, fix and optional args, short and long examples", "1,2",
			"simple sets both mandatory int args", "1 2 hello",
			"Sets integer args and option string arg");
//	if (argc < 2)
//		help(); // at least 1 required
	logger->default_level = LL_WARNING;
	res = getopt_parser.first(argc, argv);
	while (res > 0) {
		if (getopt_parser.isoption("help")) {
			help();
		} else if (getopt_parser.isoption("verbose")) {
			logger->default_level = LL_INFO;
		} else if (getopt_parser.isoption("debug")) {
			logger->default_level = LL_DEBUG;
		} else if (getopt_parser.isoption("cmdfile")) {
			if (getopt_parser.arg_s("cmdfilename", opt_cmdfilename) < 0)
				commandline_option_error(NULL);
#if defined(QBUS)			
		} else if (getopt_parser.isoption("addresswidth")) {
			unsigned aw ;
			if (getopt_parser.arg_u("addresswidth", &aw) < 0)
				commandline_option_error(NULL);
			if (aw != 16 && aw != 18 && aw != 22)
				commandline_option_error((char *)"Number of address bits must ne 16, 18 or 22");
			qunibus->set_addr_width(aw) ;
			// now iopageregisters_init() possible
#endif		
		} else if (getopt_parser.isoption("leds")) {
			unsigned n ;
			if (getopt_parser.arg_u("ledcode", &n) < 0)
				commandline_option_error(NULL);
			if (n > 15)
				commandline_option_error((char *)"4 LEDs can only display values 0..15");
			gpios->cmdline_leds = n ;
		} else if (getopt_parser.isoption("test")) {
			int i1, i2;
			string s;
			if (getopt_parser.arg_i("iarg1", &i1) < 0)
				commandline_option_error(NULL);
			if (getopt_parser.arg_i("iarg2", &i2) < 0)
				commandline_option_error(NULL);
			cout << "iarg1=" << i1 << ", iarg2=" << i2;
			if (getopt_parser.arg_s("soptarg", s))
				cout << ", soptarg=" << s;
			cout << "\n";
		}
		res = getopt_parser.next();
	}
	if (res == GETOPT_STATUS_MINARGCOUNT || res == GETOPT_STATUS_MAXARGCOUNT)
		// known option, but wrong number of arguments
		commandline_option_error(NULL);
	else if (res < 0)
		commandline_error();
}

// configure all hardware related subsystems:
// PRU, shard memory, GPIOs
void application_c::hardware_startup(enum pru_c::prucode_enum prucode_id) {
	INFO("Connecting to PRU.");
	/* initialize the library, PRU and interrupt; launch our PRU program */

	pru->start(prucode_id);
	mailbox_connect();

	INFO("Registering non-PRU pins.");
	gpios->init();
	INFO("Disable DS8641 drivers.");
	buslatches.output_enable(0);
	INFO("Leave SYSBOOT mode.");
	GPIO_SETVAL(gpios->reg_enable, 1);
	// input registers can now be read

	INFO("Registering multiplex bus latches, initialized later by PRU code.");
	// INFO("Setup bus multiplex latches.");
	buslatches.setup();

	//Todo:  iopageregisters_init() only after bus width known, and only in emulation-menus
	INFO("Initializing device register maps.");
	iopageregisters_init();
}


// disable all hardware related subsystems:
void application_c::hardware_shutdown() {
	pru->stop();
}

int application_c::run(int argc, char *argv[]) {
	void error_clear(void);

	opt_linewidth = 80;
	/* Intializes random number generator */
	{
		time_t t;
		srand((unsigned) time(&t));
	}

	// returns only if everything is OK
	// Std options already executed
	parse_commandline(argc, argv);

	logger->reset_log_levels(); // logger.default_level maybe info or debug
	logger->life_level = LL_INFO; // show message up to this level immediately on console
	//logger->life_level = LL_DEBUG; // show message up to this level immediately on console
	logger->default_filepath = "qunibone.log.csv";

	// Test messages: visible if -verbose, -debug set.
	INFO("Printing verbose output.");
	DEBUG("Printing DEBUG output. Log file = \"%s\"", logger->default_filepath.c_str());

	/* prussdrv_init() will segfault if called with EUID != 0 */
	if (geteuid()) {
		FATAL("%s must be run as root to use prussdrv\n", argv[0]);
	}

	inputline.init();
	if (!opt_cmdfilename.empty()) {
		// read commands from file
		if (!inputline.openfile((char*) opt_cmdfilename.c_str())) {
			printf("%s\n",
					fileErrorText("Could not open command file \"%s\"",
							opt_cmdfilename.c_str()));
			return -1;
		}
	}

	cout << version << "\n";

	// Multiplex latches are intialized by PRU code after each code download
	INFO("Registering Non-PRU GPIO pins.");
	gpios->init();
	INFO("Disable DS8641 drivers.");
	buslatches.output_enable(0);
	INFO("Leave SYSBOOT mode.");
	GPIO_SETVAL(gpios->reg_enable, 1);
	// input registers can now be read

	menu_main();

//	hardware_shutdown();

	return 0;
}


/* construct all singletons in proper order
 */
static void factory() {
	// logger first, all logsource_c connect to it.
	logger = new logger_c();

	the_flexi_timeout_controller = new flexi_timeout_controller_c() ;

	pru = new pru_c();
	gpios = new gpios_c();
	//qunibus_signals = new qunibus_signals_c();
	ddrmem = new ddrmem_c();

	// paneldriver before all devices who use lamps or buttons
	paneldriver = new paneldriver_c();

	membuffer = new memoryimage_c();

	qunibus = new qunibus_c();
	// qunibusadapter.worker() needs initialized mailbox
	qunibusadapter = new qunibusadapter_c();

	app = new application_c();
}

int main(int argc, char *argv[]) {

	// flush stuff on stdin. (Eclipse remote debugging)
	while (os_kbhit())
		;

	factory();
	return app->run(argc, argv);
}

