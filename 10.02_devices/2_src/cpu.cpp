/* cpu.cpp: PDP-11/05 CPU

 Copyright (c) 2018, Angelo Papenhoff, Joerg Hoppe

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


 16-oct-2020  JH     merged VBIT changes by github jks-prv
 23-nov-2018  JH      created

 In worker() Angelos 11/05 CPU is  running.
 Can do bus amster DAGTIDATO.
 */

#include <string.h>
#include <stdarg.h>

#include "logger.hpp"
#include "mailbox.h"
#include "gpios.hpp"	// ARM_DEBUG_PIN*

#include "qunibus.h"

#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"	// definition of class device_c
#include "cpu.hpp"

/* If CPU_CONTROLLED_TIME,
   then time base for all devices, also KW11 50Hz, are
   derived from emulated CPU cycles.

   Otherwise time is Linux world time, proceding even if execution of emulation is stopped due to
   task scheduling.
 */
#undef CPU_CONTROLLED_TIME

int dbg = 0;



/*** functions to be used by Angelos CPU emulator ***/

/* Adapter procs to Angelos CPU are not members of cpu_c class
 and need one global reference.
 */
static cpu_c *unibone_cpu = NULL;

// route "trace()" to unibone_cpu->logger
void unibone_log(unsigned msglevel, const char *srcfilename, unsigned srcline, const char *fmt,
                 ...) 
{
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    //vprintf(fmt, arg_ptr) ;
    //va_end(arg_ptr); va_start(arg_ptr, fmt);
    logger->vlog(unibone_cpu, msglevel, /*late_evaluation*/true, srcfilename, srcline, fmt, arg_ptr);
    va_end(arg_ptr);
}

void unibone_logdump(void) 
{
//	logger->dump(logger->default_filepath);
    logger->dump(); // stdout
}

// called before opcode fetch of next instruction
// This is the point in time were INTR requests are checked and GRANTed
// (PRU implementation may limit NPR GRANTs also to this time)
void unibone_grant_interrupts(void) 
{
    // after that the CPU should check for received INTR vectors
    // in its microcode service() step.c
    // allow PRU do to produce GRANT for device requests
    mailbox_execute (ARM2PRU_ARB_GRANT_INTR_REQUESTS);
    // Block CPU thread
    while (mailbox->arbitrator.ifs_intr_arbitration_pending) {
// often 60-80 us, So just idle loop the CPU thread
//		timeout_c::wait_us(1);
    }
}

// Access to UNIBUS, helper for foreign CPU emulation.
// Result: 1 = OK, 0 = bus timeout
// When "PMI": memory is not accessed via UNIBUS cycles,
// but by DDRAM is accessed directly.
// Then UNIBUS accesses only the IOPage.
// Motivation:
// - Fix for slow CPU execution time because of UNIBUS delays.
// - Option to implement CPUs with local 22bit memory later.
// - DEC also had separate IO and MEMORY Busses. See 11/44,60,70,84 and others

#define UNIBUS_ACCESS_NS	1000
// "real world" time for bus access. emulated timeout is stepped by this on every cycle.

int unibone_dato(unsigned addr, unsigned data) 
{
    bool success ;

    unibone_cpu->trigger.probe(addr, QUNIBUS_CYCLE_DATO) ; // register access for trigger system

    uint16_t wordbuffer = (uint16_t) data;
    the_flexi_timeout_controller->emu_step_ns(UNIBUS_ACCESS_NS);
    if (unibone_cpu->direct_memory.value && addr < qunibus->iopage_start_addr) {
        // Direct access Non-IOPage memory.
        ddrmem->pmi_deposit(addr, data);
        success = true;
    } else {
        dbg = 1;
        qunibusadapter->cpu_DATA_transfer(unibone_cpu->data_transfer_request,
                                          QUNIBUS_CYCLE_DATO, addr, &wordbuffer);
        dbg = 0;
        success = unibone_cpu->data_transfer_request.success;
        //printf("DATO; ba=%o, data=%o, success=%u\n", addr, data, (int)success) ;
    }

    // trace bus access
    if (unibone_cpu->cycle_trace_buffer.active)
        unibone_cpu->cycle_trace_buffer.add(qunibus_cycle_trace_entry_c(unibone_cpu->cycle_trace_entry_id++, addr >= qunibus->iopage_start_addr, addr, QUNIBUS_CYCLE_DATO, data, !success)) ;

    return success;
}

int unibone_datob(unsigned addr, unsigned data) 
{
    bool success ;
    unibone_cpu->trigger.probe(addr, QUNIBUS_CYCLE_DATO) ; // register access for trigger system
    the_flexi_timeout_controller->emu_step_ns(UNIBUS_ACCESS_NS);
    if (unibone_cpu->direct_memory.value && addr < qunibus->iopage_start_addr) {
        // read-modify-write
        unsigned word_address = addr & ~1; // lower even address
        uint16_t w;
        ddrmem->pmi_exam(word_address, &w);
        if (addr & 1) // odd addr: set bits <8:15>
//			w = (w & 0xff) | (data << 8);
            w = (w & 0xff) | (data & 0xff00);
        else
            // even addr: set bits <0:7>
            w = (w & 0xff00) | (data & 0xff);
        ddrmem->pmi_deposit(word_address, w);
        success = true;
    } else {
        // TODO DATOB als 1 byte-DMA !
        dbg = 1;
        uint16_t w = (uint16_t) data;
        qunibusadapter->cpu_DATA_transfer(unibone_cpu->data_transfer_request,
                                          QUNIBUS_CYCLE_DATOB, addr, &w);
        dbg = 0;
        success = unibone_cpu->data_transfer_request.success;
        //printf("DATOB; ba=%o, data=%o, success=%u\n", addr, data, (int)success) ;
    }

    // trace bus access
    if (unibone_cpu->cycle_trace_buffer.active)
        unibone_cpu->cycle_trace_buffer.add(qunibus_cycle_trace_entry_c(unibone_cpu->cycle_trace_entry_id++, addr >= qunibus->iopage_start_addr, addr, QUNIBUS_CYCLE_DATOB, data, !success)) ;

    return success;
}

int unibone_dati(unsigned addr, unsigned *data) 
 {
    bool success ;
    uint16_t w;
    unibone_cpu->trigger.probe(addr, QUNIBUS_CYCLE_DATI) ; // register access for trigger system

    the_flexi_timeout_controller->emu_step_ns(UNIBUS_ACCESS_NS);
    if (unibone_cpu->direct_memory.value && addr < qunibus->iopage_start_addr) {
        // boot address redirection by M9312? addrs 24/26 now in M9312 IOpage
        addr |= ddrmem->pmi_address_overlay;
    }
    if (unibone_cpu->direct_memory.value
            && (addr < qunibus->iopage_start_addr || qunibusadapter->is_rom(addr))) {
        // Direct access Non-IOPage memory, or to emulated ROM
        ddrmem->pmi_exam(addr, &w);
        *data = w;
        success = true;
    } else {
        dbg = 1;
        qunibusadapter->cpu_DATA_transfer(unibone_cpu->data_transfer_request,
                                          QUNIBUS_CYCLE_DATI, addr, &w);
        *data = w;
        dbg = 0;
        success = unibone_cpu->data_transfer_request.success;
        //printf("DATI; ba=%o, data=%o, success=%u\n", addr, *data, (int)success) ;
    }

    // trace bus access
    if (unibone_cpu->cycle_trace_buffer.active)
        unibone_cpu->cycle_trace_buffer.add(qunibus_cycle_trace_entry_c(unibone_cpu->cycle_trace_entry_id++, addr >= qunibus->iopage_start_addr, addr, QUNIBUS_CYCLE_DATI, *data, !success)) ;

    return success;
}

// CPU has changed the arbitration level, just forward
// if this is called as result of INTR fector PC and PSW fetch,
// mailbox->arbitrator.cpu_priority_level was CPU_PRIORITY_LEVEL_FETCHING
// In that case, PRU is allowed now to grant BGs again.
void unibone_prioritylevelchange(uint8_t level) 
{
    mailbox->arbitrator.ifs_priority_level = level;
}

// CPU executes RESET opcode -> pulses INIT line
void unibone_bus_init() 
{
    qunibus->init();
}


// selective tracing of EXEC cycles
bool unibone_trace_enabled() 
{
    return unibone_cpu->tracer.enabled ;
}

// shell and address be traced?
bool unibone_trace_addr(uint16_t a) 
{
    return !unibone_cpu->tracer.enabled || unibone_cpu->tracer.addr[a] ;
}



cpu_c::cpu_c() :
    unibuscpu_c()  // super class constructor
{
    // static config
    name.value = "CPU20";
    type_name.value = "PDP-11/20";
    log_label = "cpu";
    default_base_addr = 0;  // none
    default_intr_vector = 0;
    default_intr_level = 0;
    priority_slot.value = 0;  // not used

    // init parameters
    emulation_speed.readonly = true ; // displays only speed of emulation
    runmode.value = false;
    start_switch.value = false;
    halt_switch.value = false;
    continue_switch.value = false;
    direct_memory.value = false;
    emulation_speed.value = 0.1 ; // non-PMI speed,  see on_param_changed() also


    // current CPU does not publish registers to the bus
    // must be qunibusdevice_c then!
    register_count = 0;
    swab_vbit.value = false;

    memset(&bus, 0, sizeof(bus));
    memset(&ka11, 0, sizeof(ka11));
    ka11.bus = &bus;

    // link to global instance ptr
    assert(unibone_cpu == NULL);// only one possible
    unibone_cpu = this;	// Singleton
}

cpu_c::~cpu_c() 
{
    // restore
    the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);
    unibone_cpu = NULL;
}

// called when "enabled" goes true, before registers plugged to UNIBUS
// result false: configuration error, do not install
bool cpu_c::on_before_install(void) 
{
    halt_switch.value = false;
// all other switches parsed synchronically in worker()
    start_switch.value = false;
    continue_switch.value = false;
// enable active: assert CPU starts stopped
    stop("CPU stopped", show_none);
    return true;
}

void cpu_c::on_after_uninstall(void) 
{
// all other switches parsed synchronically in worker()
    start_switch.value = false;
    halt_switch.value = true;
    // HALT disabled CPU
    stop(NULL, show_none);
}

bool cpu_c::on_param_changed(parameter_c *param) 
{
    if (param == &direct_memory) {
        // speed feedback, as measured
        // see cpu_c() also
        emulation_speed.value = direct_memory.new_value ? 0.5 : 0.1 ;
    } else if (param == &cycle_tracefilepath) {
	    cycle_trace_buffer.active = ! cycle_tracefilepath.new_value.empty() ;
    }
    return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}

// start CPU logic on PRU and switch arbitration mode
void cpu_c::start() 
{
// stop on an ZRXB test before error output starts, to watch CPU trace
    trigger.conditions_clear() ;
    /* Earlier use cases left as example: *
    trigger.condition_add(trigger_condition_c(0777170, TRIGGER_DATO)) ; // ZRXF, start test by write into RXCS
    trigger.condition_add(trigger_condition_c(0003576, TRIGGER_DATI)) ; // ZRXA, SEEKER

    cycle_trace_buffer.active = true ;

// now: trace on
//trigger.condition_add(trigger_condition_c(0034500, TRIGGER_DATI)) ; // ZRXF, test 36 end
//trigger.condition_add(trigger_condition_c(0010560, TRIGGER_DATI)) ; // for mov wc,rxdb
//trigger.print(stdout) ;
*/
#ifdef ENABLE_TRIGGERS
    	// code flow tracing: only trace EXEC in this range, when trace_exec_addr_from > 0
    //	tracer.enable(0177170,0177173) ; // RX01/02
    	tracer.enable(026276,026400) ; // ZRXF main level of test 17
    	tracer.enable(034246,034500) ; // ZRXF main level of test 36
    	tracer.enable(010626,010742) ; // ZRXF EMPBUF subr
    	tracer.enable(011610,011632) ; // ZRXF WAIT
    //	tracer.enable(012032,012106) ; // ZRXF AWDN wait for done
#endif

    runmode.value = true;
    mailbox->param = 1;
    mailbox_execute(ARM2PRU_CPU_ENABLE);
    qunibus->set_arbitrator_active(true);
    pc.readonly = true; // can only be set on stopped CPU
    ka11.state = KA11_STATE_RUNNING;
    // time base of all device emulators now based on CPU opcode execution
#ifdef CPU_CONTROLLED_TIME
    // only point to switch
    the_flexi_timeout_controller->set_mode(flexi_timeout_c::emulated_time);
#else
    the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);
#endif
    cycle_count.value = 0;

    // 	what if CONT while WAITING??
}

// stop CPU logic on PRU and switch arbitration mode
void cpu_c::stop(const char * info, int show_options) 
{
    // time base of all device emulators now based on "real world" time
    the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);

    ka11.state = KA11_STATE_HALTED;
    pc.readonly = false;
    pc.value = ka11.r[7]; // update for editing

    runmode.value = false;
    mailbox->param = 0;
    mailbox_execute(ARM2PRU_CPU_ENABLE);
    qunibus->set_arbitrator_active(false);

    if (info && strlen(info)) {
        if (show_options & show_pc) {
            char buff[256];
            strcpy(buff, info);
            strcat(buff, " at %06o");
            INFO(buff, ka11.r[7]);
        } else
            INFO(info);
    }
	if (show_options & show_trigger) {
		trigger.print(stdout) ;
	}
	if (show_options & show_state) {
		ka11_printstate(&ka11) ;
		ka11_tracestate(&ka11) ; // DEBUG_FAST log
	}
	if ((show_options & show_cycletrace) && !cycle_tracefilepath.value.empty()) {
		cycle_trace_buffer.dump(cycle_tracefilepath.value) ;
	}
	
}

// background worker.
// Started/stopped on param "enable"
void cpu_c::worker(unsigned instance) 
{
    UNUSED(instance); // only one
    timeout_c timeout;
// bool nxm;
// unsigned pc = 0;
//unsigned dr = 0760102;
    unsigned opcode = 0;
    (void) opcode;

    power_event_ACLO_active = power_event_ACLO_inactive = power_event_DCLO_active = false;

// run with lowest priority, but without wait()
// => get all remaining CPU power
    worker_init_realtime_priority(none_rt);
//worker_init_realtime_priority(device_rt);

    timeout.wait_us(1);

    while (!workers_terminate) {
        // speed control is difficult, force to use more ARM cycles
//			if (runmode.value != (ka11.state != 0))
//				ka11.state = runmode.value;

        // RUN led
        runmode.value = (ka11.state > 0); // RUNNING, WAITING
        if (runmode.value)
            pc.value = ka11.r[7]; // update for display

        // CONT starts
        // if HALT+CONT: only one single step
        if (continue_switch.value && !runmode.value) {
            start(); // HALTED -> RUNNING
        }
        continue_switch.value = false; // momentary action

        ka11.sw = swreg.value & 0xffff;

        if (!runmode.value && start_switch.value) {
            // START, or HALT+START: reset system
            ka11.r[7] = pc.value & 0xffff;
//            ka11.sw = swreg.value & 0xffff;
            qunibus->init();
            ka11_reset(&ka11);
            if (!halt_switch.value) {
                // START without HALT
                start(); // HALTED -> RUNNING
            }
        }
        start_switch.value = false; // momentary action

        int prev_ka11_state = ka11.state;
        // ARM_DEBUG_PIN(0,1) ; // measure pmi gain
        ka11_condstep(&ka11);
        // ARM_DEBUG_PIN(0,0) ;
        if (ka11.state != KA11_STATE_HALTED && trigger.has_triggered()) {
            stop("Halted by trigger conditions:", show_pc+show_trigger+show_state+show_cycletrace);
        } else  if (breakpoint.value && ka11.state != KA11_STATE_HALTED && breakpoint.value == ka11.r[7]) {
            stop("CPU HALT by breakpoint", show_pc+show_state+show_cycletrace);
        } else  if (prev_ka11_state > 0 && ka11.state == KA11_STATE_HALTED) {
            // CPU run on HALT, sync runmode
            stop("CPU HALT by opcode", show_pc+show_state+show_cycletrace);
        }
        // running CPU: produce emulated time for all devices
        if (ka11.state == KA11_STATE_RUNNING) {
            cycle_count.value++;
        } else if (ka11.state == KA11_STATE_WAITING)
            // we should us "world" time here, but want to avoid permanent time-source switching
            // so just assume this here is called every 500ns (estimated average worker loop time)
            the_flexi_timeout_controller->emu_step_ns(500);
        // if KA11_STATE_HALTED: world time is used, see start() / stop()

        // serialize asynchronous power events
        // ACLO inactive & no HALT: reboot
        // ACLO inactive & HALT: boot on CONT
//if (power_event)	DEBUG_FAST("power_event=%d", power_event) ;
        // ACLO: power fail trap, if running.
        if (runmode.value && power_event_ACLO_active) {
            ka11_pwrfail_trap(&unibone_cpu->ka11);
        }
        power_event_ACLO_active = false; // processed

        // DCLO: halt, like "enable = 0"
        if (runmode.value && power_event_DCLO_active) {
            stop("CPU HALT by DCLO", show_pc);
//			ka11_reset(&ka11);
        }
        power_event_DCLO_active = false; // processed
        if (power_event_ACLO_inactive) {
            // Reboot
            // if HALT switch active: no action, event remains pending
//			if (!halt_switch.value) {
            stop("ACLO", show_pc);
            // execute this with real-world time, else lock (CPU not step() ing here)
            qunibus->init();		// reset devices
            start();		// start CPU logic on PRU, is bus master now
            INFO("ACLO inactive: fetch vector");
            ka11_reset(&unibone_cpu->ka11);
            ka11_pwrup_vector_fetch(&unibone_cpu->ka11);
            // M9312 logic here: vectror redirection for 300ms
//			}
            power_event_ACLO_inactive = false;		// processed
        }

        // HALT: activating stops
        // Must be last, to undo power-up and CONT
        // after HALT+power-up: only vector fecth executed
        // after CONT+HALT: one step executed
        if (halt_switch.value && runmode.value) {
            // HALT position inside instructions !!!
            stop("CPU HALT by switch", show_pc+show_state+show_cycletrace);
        }

        ka11.swab_vbit = (swab_vbit.value == true);

    }
}

// process DATI/DATO access to one of my "active" registers
// !! called asynchronuously by PRU, with SSYN asserted and blocking UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void cpu_c::on_after_register_access(qunibusdevice_register_t *device_reg,
                                     uint8_t unibus_control) 
 {
// nothing todo
    UNUSED(device_reg);
    UNUSED(unibus_control);
}

// CPU received interrupt vector from UNIBUS
// PRU triggers this via qunibusadapter worker thread,
// mailbox->arbitrator.cpu_priority_level is CPU_PRIORITY_LEVEL_FETCHING
// CPU fetches PSW and calls unibone_prioritylevelchange(), which
// sets mailbox->arbitrator.cpu_priority_level and
// PRU is allowed now to grant BGs again.
void cpu_c::on_interrupt(uint16_t vector) {
// CPU sequence:
// push PSW to stack
// push PC to stack
// PC := *vector
// PSW := *(vector+2)
    ka11_setintr(&unibone_cpu->ka11, vector);
}

