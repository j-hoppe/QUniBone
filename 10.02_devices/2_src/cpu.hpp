/* cpu.hpp: PDP-11/05 CPU

 Copyright (c) 2018, Angelo Papenhoff, Joerg Hoppe
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


 23-nov-2018  JH      created
 */
#ifndef _CPU_HPP_
#define _CPU_HPP_

using namespace std;

#include "utils.hpp"
#include "timeout.hpp"
//#include "qunibusadapter.hpp"
//#include "qunibusdevice.hpp"
#include "unibuscpu.hpp"
#include "qunibus_tracer.hpp"
#include "ringbuffer.hpp"
#include "cpu20/11.h"
#include "cpu20/ka11.h"

// on etraces QUNIBUS access
class qunibus_cycle_trace_entry_c {
public:
    uint64_t	id ;
    uint64_t	timestamp_ns ;
    bool iopage ;
    unsigned address ;
    uint8_t	cycle ; // DTAI
    uint16_t	data ;
    bool nxm ; // timeout, not existing memory
    qunibus_cycle_trace_entry_c() { }
    qunibus_cycle_trace_entry_c(uint64_t _id, bool _iopage, unsigned _address, uint8_t _cycle, uint16_t _data, bool _nxm) {
        id = _id ;
        this->timestamp_ns = timeout_c::abstime_ns() ;
        iopage = _iopage ;
        address = _address ;
        cycle = _cycle ;
        data = _data ;
        nxm = _nxm ;
    }
} ;

#include <fstream>
#include <sys/time.h>

#define qunibus_cycle_trace_buffer_size 16384
//#define qunibus_cycle_trace_buffer_size 4096
class qunibus_cycle_trace_buffer_c: public jnk0le::Ringbuffer<qunibus_cycle_trace_entry_c, qunibus_cycle_trace_buffer_size, false, 8> {
public:
    bool active = false ;
    // my insert() erases oldest entry
    void add(qunibus_cycle_trace_entry_c qcte) {
        if (isFull()) remove(100) ; // remove a large chunk, to speed up.
        assert( insert(qcte)) ;
    }

    // readout non-destructive. to clear, use "clearConsumer()"
    void dump(std::ostream *stream) {
        qunibus_cycle_trace_entry_c *cte ;
        char buffer[256] ;
        timeval now ;
        gettimeofday(&now, NULL);
        strftime(buffer, 26, "%F %T", localtime(&now.tv_sec));
        *stream << "// Sampled QUNIBUS cycles, saved at " << buffer << "\n";
        *stream << "id, timestamp, iopage, address, cycle, data, nxm\n" ;
        for (unsigned i=0 ; i < readAvailable() ; i++) {
            cte = at(i) ;
//        while (remove(cte)) {
            sprintf(buffer, "%llu, %llu, %d, %06o, %s, %06o, %d",
                    cte->id, cte->timestamp_ns, cte->iopage, cte->address, qunibus_c::control2text(cte->cycle), cte->data, cte->nxm) ;
            *stream << buffer << "\n";
        }
    }


    void dump(std::string filepath) {
        std::ofstream file_stream;
        file_stream.open(filepath, std::ofstream::out | std::ofstream::trunc);
        if (!file_stream.is_open()) {
            cout << "Can not open log file \"" << filepath << "\"! Aborting!\n";
            exit(2);
        }
        size_t fill = readAvailable() ;
        dump(&file_stream);

        file_stream.close();
        cout << "Dumped " << fill << " messages to file \"" << filepath << "\".\n";
    }
} ;


class cpu_c: public unibuscpu_c {
private:
    //qunibusdevice_register_t *switch_reg;
    //qunibusdevice_register_t *display_reg;

	// bitwise options what state info to show on HALT
	static const int show_none = 0 ;
	static const int show_pc = 1 ;
	static const int show_trigger =2 ;
	static const int show_state = 4 ;
	static const int show_cycletrace = 8 ;

public:

    cpu_c();
    ~cpu_c();

    bool on_before_install(void) override ;
    void on_after_uninstall(void) override ;

    // used for DATI/DATO, operated by qunibusadapter
    dma_request_c data_transfer_request = dma_request_c(this);

    bool on_param_changed(parameter_c *param) override;  // must implement

    parameter_bool_c runmode = parameter_bool_c(this, "run_led", "r",/*readonly*/
                               true, "RUN LED: 1 = CPU running, 0 = halted.");
    parameter_bool_c halt_switch = parameter_bool_c(this, "halt_switch", "h",/*readonly*/
                                   false, "HALT switch: 1 = CPU stopped, 0 = CPU may run.");
    parameter_bool_c continue_switch = parameter_bool_c(this, "continue_switch", "c",/*readonly*/
                                       false, "CONT action switch: 1 = CPU restart after HALT. CONT+HALT = single step.");
    parameter_bool_c start_switch = parameter_bool_c(this, "start_switch", "s",/*readonly*/
                                    false, "START action switch: 1 = reset & start CPU from PC. START+HALT: reset.");

    parameter_bool_c direct_memory = parameter_bool_c(this, "pmi", "pmi",/*readonly*/
                                     false, "Private Memory Interconnect: CPU accesses memory internally, not over UNIBUS.");

    parameter_bool_c swab_vbit = parameter_bool_c(this, "swab_vbit", "swab",/*readonly*/
                                 false, "SWAB instruction does not(=0) or does(=1) modify psw v-bit (=0 is standard 11/20 behavior)");

    parameter_unsigned_c pc = parameter_unsigned_c(this, "PC", "pc",/*readonly*/
                              false, "", "%06o", "program counter helper register.", 16, 8);

    parameter_unsigned_c swreg = parameter_unsigned_c(this, "switch_reg", "swr",/*readonly*/
                                 false, "", "%06o", "Console switch register.", 16, 8);

    parameter_unsigned64_c cycle_count = parameter_unsigned64_c(this, "cycle_count", "cc",/*readonly*/
                                         true, "", "%u", "CPU opcodes executed since last HALT", 63, 10);

    parameter_unsigned_c breakpoint = parameter_unsigned_c(this, "breakpoint", "bp",/*readonly*/
                                      false, "", "%06o", "Stop when CPU fetches opcode from octal address. 0 = disable", 16, 8);

    parameter_string_c cycle_tracefilepath = parameter_string_c(this, "cycle_tracefilepath", "ctf",/*readonly*/false,
            "If set, CPU cycle trace is active and dumped to file on HALT.") ;


    struct Bus bus; // UNIBUS interface of CPU
    struct KA11 ka11; // Angelos CPU state

    void start(void);
    void stop(const char * info, int show_options=show_none);

    // background worker function
    void worker(unsigned instance) override;

    // called by qunibusadapter on emulated register access
    void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control)
    override;

    void on_interrupt(uint16_t vector);

    //diagnostic
    trigger_c	trigger ;
    tracer_c	tracer ;

    // ring buffer for bus DATI/DATO accesses
    // need to set cachelinesize, 0 causes error. 8?
    uint64_t cycle_trace_entry_id = 0 ; // enumerate samples
    qunibus_cycle_trace_buffer_c cycle_trace_buffer;


};

#endif
