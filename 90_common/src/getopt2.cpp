/* getopt2.c:  advanced commandline parsing

 Copyright (c) 2016, Joerg Hoppe
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

 20-Jul-2016  JH	added defaults for options
 17-Mar-2016  JH	allow "/" option marker only #ifdef WIN32
 01-Feb-2016  JH   created


 Adavanced getopt(), parses command lines,

 Argument pattern

 commandline = [option, option, ...]  args ....
 option = ( "-" | "/"  ) ( short_option_name | long_option_name )
 [fix_arg fix_arg .... [ var_arg var_arg ]]


 API
 getopt_init()  - init data after start, only once
 getopt_def(id, short, long, fix_args, opt_args, info)
 define a possible option
 fix_args, opt_args: comma separated names


 getopt_first(argc, argv) parse and return arg of first option
 result = id
 value for args in static "argval" (NULL temriantd)

 getopt_next()	- ge



 Example
 Cmdline syntax: "-send id len [data .. data]   \
					   -flag \
					   -logfile logfile \
					   myfile
 getopt_init() ;
 getopt_def("s", "send", "id,len", "data0,data1,data2,data3,data4,data5,data6,data7") ;
 getopt_def("flag", NULL, NULL, NULL) ;
 getopt_def("l", "logfile", "logfile", NULL) ;

 res = getopt_first(argc, argv) ;
 while (res > 0) {
 if (getopt_is("send")) {
 // process "send" option with argval[0]= id, argval[1] = len, argval[2]= data0 ...
 } else if (getopt_is("flag")) {
 // process flag
 } else if (getopt_is("logfile")) {
 // process logfile, name = argval[0]
 } else if (getopt_is(NULL)) {
 // non-option commandline arguments in argval[]
 }
 }
 if (res < 0) {
 printf("Cmdline syntax error at ", curtoken) ;
 getopt_help(stdout) ;
 exit(1) ;
 }

 // res == 0: all OK, go on
 ...

 */

/*
 #include <stdlib.h>
 #include <string.h>
 #include <stdio.h>
 #include <limits.h>
 #include <assert.h>
 #include "getopt2.hpp"

 #ifdef WIN32
 #include <windows.h>
 #define strcasecmp _stricmp	// grmbl
 #define snprintf  sprintf_s
 #else
 #include <ctype.h>
 #include <unistd.h>
 #endif
 */

#include <string.h>
#include <assert.h>
#include <iostream>
#include <sstream>
#include <climits>

#include "getopt2.hpp"

/*
 * first intialize, only once!
 * NO FREE() HERE !
 */

getopt_c::getopt_c() {
	nonoption_descr.valid = false;
}

//
getopt_c::~getopt_c() {
	this->ignore_case = false;

}

// restart for new parse
void getopt_c::init(bool _ignore_case) {
	ignore_case = _ignore_case;
	// clear lists
	nonoption_descr.valid = false;
	option_descrs.clear();
	cur_option_argval.clear();
	curerror = GETOPT_STATUS_OK;
	curerrortext[0] = 0;
}

/* compare string with regard to "ignore_case*/
int getopt_c::stringcmp(string s1, string s2) {
	if (this->ignore_case)
		return strcasecmp(s1.c_str(), s2.c_str());
	else
		return strcmp(s1.c_str(), s2.c_str());
}

/*
 fix_args, opt_args: like  "name1,name2,name2"
 if short/long optionname = NULL
 its the definition of the non-option commandline arguments
 */
getopt_option_descr_c *getopt_c::define(string short_option_name, string long_option_name,
		string fix_args_csv, string opt_args_csv, string default_args, string info,
		string example_simple_cline, string example_simple_info, string example_complex_cline,
		string example_complex_info) {
	getopt_option_descr_c *res;

	if (short_option_name.empty() && long_option_name.empty()) {
		// add the single non-option commandline arg
		nonoption_descr.valid = true;
		res = &nonoption_descr;
	} else {
		// instantiate new option descr
		option_descrs.push_back(getopt_option_descr_c());
		res = &option_descrs[option_descrs.size() - 1];
	}
	res->valid = true;
	res->short_name = short_option_name;
	res->long_name = long_option_name;
	res->default_args = default_args;
	res->info = info;
	res->example_simple_cline_args = example_simple_cline;
	res->example_simple_info = example_simple_info;
	res->example_complex_cline_args = example_complex_cline;
	res->example_complex_info = example_complex_info;

	res->fix_args.clear();
	res->var_args.clear();
	res->fix_arg_count = 0;
	res->max_arg_count = 0;

	// separate name lists

	// get own copies of argument name list
	//res->fix_args_name_buff = fix_args_csv; // obsolete ?
	//res->var_args_name_buff = opt_args_csv; // obsolete ?

	// separate names at commas
	{
		std::istringstream ss(fix_args_csv);
		string token;
		while (std::getline(ss, token, ','))
			res->fix_args.push_back(token);
		res->fix_arg_count = res->fix_args.size(); // obsolete
	}
	{
		std::istringstream ss(opt_args_csv);
		string token;
		while (std::getline(ss, token, ','))
			res->var_args.push_back(token);
		res->max_arg_count = res->fix_args.size() + res->var_args.size(); // obsolete
	}

	return res;
}

/*
 is the last parsed option the one with short or longname "name" ?
 Use for switch-processing
 */
bool getopt_c::isoption(string name) {
	if (name.empty() && cur_option == &nonoption_descr)
		return 1; // we're parsing the non-option args
	if (!cur_option)
		return false; // not equal
	if (name.empty()) // nonoption args already tested
		return false;
	// names NULL for nonoption-args
	if (!stringcmp(name, cur_option->short_name))
		return true;
	if (!stringcmp(name, cur_option->long_name))
		return true;
	return 0;
}

/*
 * getopt_first()	initialize commandline parser and return 1st option with args
 * getopt_next()	returns netx option from command line with its arguments

 *			list of parsed args in _this->argval

 *	result: > 0: OK
 *		 0 = GETOPT_STATUS_EOF		 cline processed
 *		other status codes <= 0
 *		GETOPT_STATUS_ARGS		no option returned, but non-option cline args
 *		GETOPT_STATUS_UNDEFINED	undefined -option
 *		GETOPT_STATUS_ARGS		not enough args for -option
 *
 */

// if clinearg is -name or --name or /name: return name, else NULL
string get_dashed_option_name(string clinearg) {
	if (clinearg.substr(0, 2) == "--")
		return clinearg.substr(2, string::npos);
	else if (clinearg.substr(0, 1) == "-")
		return clinearg.substr(1, string::npos);
#ifdef WIN32
	else if ( clinearg.substr(0, 1) == "/")
	return clinearg.substr(1, maxint);
#endif
	else
		return "";
}

int getopt_c::parse_error(int error) {
	stringstream ss;
	curerror = error;
	switch (error) {
	case GETOPT_STATUS_ILLEGALOPTION:
		ss << "Undefined option at \"" << curtoken << "\"";
		break;
	case GETOPT_STATUS_MINARGCOUNT:
		if (cur_option == &nonoption_descr)
			ss << "Less than " << cur_option->fix_arg_count << " non-option arguments at \""
					<< curtoken << "\"";
		else
			ss << "Less than " << cur_option->fix_arg_count << " arguments for option \""
					<< cur_option->long_name << "\" at \"" << curtoken << "\"";
		break;
	case GETOPT_STATUS_MAXARGCOUNT:
		if (cur_option == &nonoption_descr)
			ss << "More than " << cur_option->max_arg_count << " non-option arguments at \""
					<< curtoken << "\"";
		else
			ss << "More than " << cur_option->max_arg_count << " arguments for option \""
					<< cur_option->long_name << "\" at \"" << curtoken << "\"";
		break;
	}
	curerrortext = ss.str();
	return error;
}

int getopt_c::arg_error(getopt_option_descr_c& odesc, int error, string& argname,
		string argval) {
	stringstream ss;
	curerror = error;
	switch (error) {
	case GETOPT_STATUS_ILLEGALARG:
		ss << "Option \"" << odesc.long_name << "\" has no argument \"" << argname << "\"";
		break;
	case GETOPT_STATUS_ARGNOTSET:
		ss << "Optional argument \"" << argname << "\" for option \"" << odesc.long_name
				<< " not set";
		break;
	case GETOPT_STATUS_ARGFORMATINT:
		ss << "Argument \"" << argname << "\" of option \"" << odesc.long_name
				<< "\" has value \"" << argval << "\", which is no integer";
		break;
	case GETOPT_STATUS_ARGFORMATHEX:
		ss << "Argument \"" << argname << "\" of option \"" << odesc.long_name
				<< "\" has value \"" << argval << "\", which is no hex integer";
		break;
	}
	curerrortext = ss.str();
	return error;
}

int getopt_c::next() {
	string s, oname;
	int i;
	int max_scan_arg_count;

	// is it an option?
	if (cur_cline_arg_idx >= cline_argcount)
		return GETOPT_STATUS_EOF;

	curtoken = s = cline_args[cur_cline_arg_idx];
	assert(!s.empty());

	oname = get_dashed_option_name(s);
	if (!oname.empty()) {
		vector<getopt_option_descr_c>::iterator odesc;
		// odesc is an "-option": search options by name
		cur_option = NULL;
		for (odesc = option_descrs.begin(); odesc != option_descrs.end(); odesc++)
			if (!stringcmp(oname, odesc->short_name) || !stringcmp(oname, odesc->long_name))
				cur_option = &*odesc; // found by name
		if (cur_option == NULL) {
			// its an option, but not found in the definitions
			return parse_error(GETOPT_STATUS_ILLEGALOPTION);
		}
		cur_cline_arg_idx++; // skip -option

		// if an option has no optional arguments, prevent it from
		// parsing into
	} else
		// its not an '-option: so its the "nonoption" rest of cline
		// TODO: may not be defined?
		cur_option = &nonoption_descr;

	// find the arg at wich to stop parsing args for this option
	// in case of nonoption-args: end of cmdlineor
	// else for options:
	//	it's either the next -option,
	//  or if no -option: collision with trailing non-option args.
	//	   if no variable: amount of fix args
	//		else: line end. So in case
	//		-option fix0 fix1 var0 var 1 nonopt0 nonopt1
	//			all also nonopt0/1 is read, resulting in an error
	//			this is intended to force unambiguous syntax declaration!
	if (cur_option == &nonoption_descr)
		max_scan_arg_count = INT_MAX;
	else {
		// search next -option
		i = cur_cline_arg_idx;
		while (i < cline_argcount && get_dashed_option_name(cline_args[i]).empty())
			i++;
		if (i < cline_argcount) // terminating -option found
			max_scan_arg_count = i - cur_cline_arg_idx;
		else if (cur_option->fix_arg_count == cur_option->max_arg_count)
			max_scan_arg_count = cur_option->fix_arg_count;
		else
			max_scan_arg_count = INT_MAX;
	}

	// option (or rest of cline) valid, parse args
	// parse until
	// - end of commandline
	// - max argument count for option reached
	// - another "-option" is found
	cur_option_argval.clear();
	i = 0;
	while (cur_cline_arg_idx < cline_argcount && i < max_scan_arg_count) {
		curtoken = cline_args[cur_cline_arg_idx];
		cur_option_argval.push_back(cline_args[cur_cline_arg_idx]);
		i++;
		cur_cline_arg_idx++;
	}

	if (cur_option_argval.size() < cur_option->fix_arg_count)
		return parse_error(GETOPT_STATUS_MINARGCOUNT);
	if (cur_option_argval.size() > cur_option->max_arg_count)
		return parse_error(GETOPT_STATUS_MAXARGCOUNT);
	return GETOPT_STATUS_OK;
}

int getopt_c::first(int argc, char **argv) {
	int i;
	cline_argcount = 0;
	cline_args.clear();

	// for all options with "default" args:
	// put strings <option> <args> in front of commandline,
	// so they are parsed first and overwritten later by actual values
	{
		// 1) build own cmdline
		stringstream ss;
		vector<getopt_option_descr_c>::iterator odesc;
		for (odesc = option_descrs.begin(); odesc != option_descrs.end(); odesc++)
			if (!odesc->default_args.empty())
				ss << "--" << odesc->long_name << " " << odesc->default_args << " ";
		default_cmdline_buff = ss.str();
	}

	// 2) separate default cmdline buff into words and add to argv
	{
		stringstream ss(default_cmdline_buff);
		string token;
		while (ss >> token) {
			cline_args.push_back(token);
			cline_argcount++;
		}
	}

	// 3) append user commandline tokens, so they are processed after defaults
	for (i = 1; i < argc; i++) { // skip program name of
		string arg(argv[i]); // convert char * to string
		cline_args.push_back(arg);
		cline_argcount++;
	}
	cur_cline_arg_idx = 0;
	cur_option = NULL;
	cur_option_argval.clear();

	// return first argument
	return next();
}

// find argument position in list by name.
// optargs[] are numbered behind fixargs[]
// < 0: not found
int getopt_c::optionargidx(getopt_option_descr_c& odesc, string& argname) {
	unsigned i;
	for (i = 0; i < odesc.fix_arg_count; i++)
		if (!stringcmp(argname, odesc.fix_args[i]))
			return i;
	for (i = odesc.fix_arg_count; i < odesc.max_arg_count; i++)
		if (!stringcmp(argname, odesc.var_args[i - odesc.fix_arg_count]))
			return i;
	return arg_error(odesc, GETOPT_STATUS_ARGNOTSET, argname, "");
}

// argument of current option by name as string
// Only to be used after first() or next()
// result: < 0 = error, 0 = arg not set
int getopt_c::arg_s(string argname, string& res) {
	int argidx;
	if (cur_option == NULL)
		return parse_error(GETOPT_STATUS_ILLEGALOPTION);
	argidx = optionargidx(*cur_option, argname);
	if (argidx < 0)
		return arg_error(*cur_option, GETOPT_STATUS_ILLEGALARG, argname, "");
	if (argidx >= (int) cur_option_argval.size())
		// only n args specified, but this has list place > n
		// the optional argument [argidx] is not given in the arguument list
		return GETOPT_STATUS_EOF;
	//		return getopt_arg_error(_this, odesc, GETOPT_STATUS_ARGNOTSET, argname, NULL);

	// does that return a value??
	res = cur_option_argval[argidx];
	return GETOPT_STATUS_OK;
}

// argument of current option by name as  integer, with optional prefixes "0x" or "0".
// result: < 0 = error, 0 = arg not set
int getopt_c::arg_i(string argname, int *val) {
	int res;
	char *endptr;
	string buff;
	res = arg_s(argname, buff);
	if (res <= 0) // error or EOF
		return res;
	*val = strtol(buff.c_str(), &endptr, 0);
	if (*endptr) // stop char: error
		return arg_error(*cur_option, GETOPT_STATUS_ARGFORMATINT, argname, buff);
	return GETOPT_STATUS_OK;
}

// argument of current option by name as unsigned integer, with optional prefixes "0x" or "0".
// result: < 0 = error, 0 = arg not set
int getopt_c::arg_u(string argname, unsigned *val) {
	int res;
	char *endptr;
	string buff;
	res = arg_s(argname, buff);
	if (res <= 0) // error or EOF
		return res;
	*val = strtoul(buff.c_str(), &endptr, 0);
	if (*endptr) // stop char: error
		return arg_error(*cur_option, GETOPT_STATUS_ARGFORMATINT, argname, buff);
	return GETOPT_STATUS_OK;
}

// argument of current option by name as octal integer.
// result: < 0 = error, 0 = arg not set
int getopt_c::arg_o(string argname, int *val) {
	int res;
	char *endptr;
	string buff;
	res = arg_s(argname, buff);
	if (res <= 0) // error or EOF
		return res;
	*val = strtol(buff.c_str(), &endptr, 8);
	if (*endptr) // stop char: error
		return arg_error(*cur_option, GETOPT_STATUS_ARGFORMATHEX, argname, buff);
	return GETOPT_STATUS_OK;
}

// argument of current option by name as hex integer. No prefix "0x" allowed!
// result: < 0 = error, 0 = arg not set
int getopt_c::arg_h(string argname, int *val) {
	int res;
	char *endptr;
	string buff;
	res = arg_s(argname, buff);
	if (res <= 0) // error or EOF
		return res;
	*val = strtol(buff.c_str(), &endptr, 16);
	if (*endptr) // stop char: error
		return arg_error(*cur_option, GETOPT_STATUS_ARGFORMATHEX, argname, buff);
	return GETOPT_STATUS_OK;
}

/* printhelp()
 * write the syntax and explanation out
 */

// generate a string like "-option <arg1> <args> [<optarg>]"
// style 0: only --long_name, or (shortname)
// style 1: "-short, --long .... "
string getopt_c::getoptionsyntax(getopt_option_descr_c& odesc, int style)
//	getopt_t *_this, getopt_option_descr_c *odesc)
		{
	stringstream buffer;
	unsigned i;

	// mount a single "-option arg arg [arg arg]"
	if (style == 0) {
		if (!odesc.long_name.empty())
			buffer << "--" << odesc.long_name;
		else if (!odesc.short_name.empty())
			buffer << "-" << odesc.short_name;
		else {
		}  // no option name: non-optopn commandline arguments
	} else { // both names comma separated: "-short, --long"
		if (!odesc.short_name.empty())
			buffer << "-" << odesc.short_name;
		if (!odesc.long_name.empty()) {
			if (!odesc.short_name.empty())
				buffer << " | ";
			buffer << "--" << odesc.long_name;
		}
	}

	{
		vector<string>::iterator arg;
		for (arg = odesc.fix_args.begin(); arg != odesc.fix_args.end(); arg++)
			buffer << " <" << *arg << ">";
		i = 0;
		for (arg = odesc.var_args.begin(); arg != odesc.var_args.end(); arg++, i++) {
			buffer << " ";
			if (i == 0)
				buffer << "[";
			buffer << " <" << *arg << ">";
		}
		if (i > 0)
			buffer << "]";
	}

	return buffer.str();
}

getopt_printer_c::getopt_printer_c(ostream& stream, unsigned linelen, unsigned indent) {
	this->stream = &stream;
	this->linelen = linelen;
	this->indent = indent;
}

// add as string to outline. if overflow, flush and continue with indent
// "stream": must be defined in call context
void getopt_printer_c::append(string s, bool linebreak) {
	if (linebreak || (curline.length() > indent && (curline.length() + s.length()) > linelen)) {
		// prevent the indent from prev line to be accounted for another line break
		*stream << curline << "\n";
		curline.clear(); // new indent
		curline.append(indent, ' ');
	}
	curline.append(s);
}

/* print a multine string separated by \n, with indent and line break
 * linebuff may already contain some text */
void getopt_printer_c::append_multilinestring(string text) {
	std::istringstream ss(text);
	string line;
	unsigned i = 0;
	// split multi lines into string at \n.
	while (std::getline(ss, line, '\n')) {
		append(line, /*linebreak*/i > 0);
		i++;
	}
}

void getopt_printer_c::flush() {
	*stream << curline << "\n";
	curline.clear();
}

void getopt_c::help_option_intern(getopt_option_descr_c& odesc, ostream& stream,
		unsigned linelen, unsigned indent) {
	getopt_printer_c printer(stream, linelen, indent);
	string phrase;

	// print syntax
	phrase = getoptionsyntax(odesc, 1);
	printer.append(phrase, /*linebreak*/false);
	printer.append("", /*linebreak*/true); // newline
	if (!odesc.info.empty())
		printer.append_multilinestring(odesc.info);
	if (!odesc.default_args.empty()) {
		printer.append("Default: \"", false);
		printer.append(odesc.default_args, false);
		printer.append("\"", false);
	}

	// print examples:
	if (!odesc.example_simple_cline_args.empty()) {
		printer.append("Simple example:  ", true);
		if (!odesc.short_name.empty()) {
			printer.append("-", false);
			printer.append(odesc.short_name, false);
			printer.append(" ", false);
		}
		printer.indent += 4;
		printer.append_multilinestring(odesc.example_simple_cline_args);
		printer.indent -= 4;
		printer.append("    ", /*linebreak*/1); // newline + extra indent
		//does not work printer.append("", /*linebreak*/1); // newline + extra indent
		printer.indent += 4;
		printer.append_multilinestring(odesc.example_simple_info);
		printer.indent -= 4;
	}
	if (!odesc.example_complex_cline_args.empty()) {
		printer.append("Complex example:  ", true);
		if (!odesc.long_name.empty()) {
			printer.append("--", false);
			printer.append(odesc.long_name, false);
			printer.append(" ", false);
		}
		printer.indent += 4;
		printer.append_multilinestring(odesc.example_complex_cline_args);
		printer.indent -= 4;
		//		printer.append"", /*linebreak*/1); // newline + extra indent
		printer.append("    ", /*linebreak*/1); // newline + extra indent
		printer.indent += 4;
		printer.append_multilinestring(odesc.example_complex_info);
		printer.indent -= 4;
	}

	// print remaining text (last open line) ;
	printer.flush();
}

// print cmdline syntax and help for all options
void getopt_c::help(ostream& stream, unsigned linelen, unsigned indent, string commandname) {
	getopt_printer_c printer(stream, linelen, indent);
	stringstream ss;
	vector<getopt_option_descr_c>::iterator odesc;
	/*
	 unsigned i;
	 char linebuff[2 * GETOPT_MAX_LINELEN];
	 char phrase[2 * GETOPT_MAX_LINELEN];
	 getopt_option_descr_c *odesc;
	 assert(linelen < GETOPT_MAX_LINELEN);
	 */
	// 1. print commandline summary
	printer.append(commandname + " ", false);
	// 1.1. print options
	//fprintf(stream, "Command line summary:\n");
	for (odesc = option_descrs.begin(); odesc != option_descrs.end(); odesc++) {
		// mount a single "-option arg arg [arg arg]"
		printer.append(getoptionsyntax(*odesc, 0) + " ", false);
	}
	// 1.2. print non-option cline arguments
	if (nonoption_descr.valid) {
		printer.append(getoptionsyntax(nonoption_descr, 0) + " ", false);
	}
	printer.flush();

	// 2. print option info
	stream << "\n";
	// fprintf(stream, "Command line options:\n");

	// first: nonoption arguments
	if (nonoption_descr.valid)
		help_option_intern(nonoption_descr, stream, linelen, indent);
	// now options
	for (odesc = option_descrs.begin(); odesc != option_descrs.end(); odesc++)
		help_option_intern(*odesc, stream, linelen, indent);

	if (ignore_case)
		stream << "\nOption names are case insensitive.\n";
	else
		stream << "\nOption names are case sensitive.\n";
}

// display evaluated commandline (defaults and user)
void getopt_c::help_commandline(ostream& stream, unsigned linelen, unsigned indent) {
	// use printer for automatic linebreaks
	getopt_printer_c printer(stream, linelen, indent);
	unsigned i;
	for (i = 0; i < cline_args.size(); i++) {
		if (i == 0)
			printer.append("\"", /*linebreak*/false);
		else
			printer.append(" \"", false);
		printer.append(cline_args[i], false);
		printer.append("\"", /*linebreak*/0);
	}
	printer.flush();
}

// print help for current option
void getopt_c::help_option(ostream& stream, unsigned linelen, unsigned indent) {
	if (cur_option)
		help_option_intern(*cur_option, stream, linelen, indent);
}

