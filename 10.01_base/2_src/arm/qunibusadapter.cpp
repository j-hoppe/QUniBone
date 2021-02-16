/* qunibusadapter.cpp: connects multiple "unibusdevices" to the PRU QUNIBUS interface

 Copyright (c) 2018-2020, Joerg Hoppe
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


 aug-2020	JH		adapted to QBUS
 jul-2019     JH      rewrite: multiple parallel arbitration levels
 12-nov-2018  JH      entered beta phase

 qunibusadapter
 connects multiple "devices" with qunibus-interface implmented by PRU

 - A thread waits for Register Interrupts and routes them to the
 correct controller
 ( QBUS/UNIBUS -> Controllers)
 - a scheduler accepts INTR and DMA request in a fifo
 and generates ordered cmds for the PRU
 (also simulates slot order)
 Controllers -> QBUS/UNIBUS
 - allows controllers and memory to register and deregister
 in the deviceregister tables  (devices.h)
 Distrubutes INIT to all registered controllers.

 recieves DMA and INTR request from many devices
 Issues only one INTR or one DMA to the PRU
 (INTR parallel with DMA not possible,
 multiple INTR levels in parallel not possible)
 */
#define _UNIBUSADAPTER_CPP_

//#include <iostream>
//#include <fstream>
#include <ios>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <queue>

// TEST
//#include <unistd.h> // sleep()
//#include <sys/time.h>

using namespace std;

#include "logsource.hpp"
#include "logger.hpp"
#include "mailbox.h"
#include "gpios.hpp"
#include "prussdrv.h"
#include "pruss_intc_mapping.h"
#include "iopageregister.h"
#include "priorityrequest.hpp"
#include "qunibusadapter.hpp"
#include "unibuscpu.hpp"

qunibusadapter_c *qunibusadapter; // another Singleton
// is registered in device_c.list<devices> ... order of static constructor calls ???

bool qunibusadapter_debug_flag = 0;

// encode signal bit for PRU from BR/NPR level.
// index is one of PRIORITY_LEVEL_INDEX_*

static uint8_t priority_level_idx_to_arbitration_bit[PRIORITY_LEVEL_COUNT] = {
    PRIORITY_ARBITRATION_BIT_B4,
    PRIORITY_ARBITRATION_BIT_B5,
    PRIORITY_ARBITRATION_BIT_B6,
    PRIORITY_ARBITRATION_BIT_B7,
    PRIORITY_ARBITRATION_BIT_NP
};

qunibusadapter_c::qunibusadapter_c() :
    device_c() {
    unsigned i;
    log_label = "QUNAPT";

    name.value = "QUNIBUSADAPTER";
    type_name.value = "qunibusadapter_c";

    for (i = 0; i <= MAX_DEVICE_HANDLE; i++)
        devices[i] = NULL;
    line_INIT = false;
    line_DCLO = false;
    line_ACLO = false;

    requests_mutex = PTHREAD_MUTEX_INITIALIZER;

    requests_init();

    registered_cpu = NULL;
}

bool qunibusadapter_c::on_param_changed(parameter_c *param) {
    // no own parameter or "enable" logic
    return device_c::on_param_changed(param); // more actions (for enable)
}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void qunibusadapter_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
    UNUSED(aclo_edge) ;
    UNUSED(dclo_edge) ;
}

void qunibusadapter_c::on_init_changed(void) {
    requests_init();
    // clear all pending BR and NPR lines on PRU
    mailbox->intr.priority_arbitration_bit = PRIORITY_ARBITRATION_BIT_MASK;
    mailbox_execute(ARM2PRU_INTR_CANCEL);
}

// register_device ... "plug" the device into QBUS/UNIBUSs backplane
// - assign handle
// - setup register map for a device
// uses device.handle, startaddress, "active" attribute of registers
// result: shared addressmap changed,
// device.register[] point into shared register descriptors.
// dumb and additive
// result: false = failure
bool qunibusadapter_c::register_device(qunibusdevice_c& device) {
    bool register_handle_used[MAX_IOPAGE_REGISTER_COUNT]; // index by handle
    unsigned i;
    unsigned register_handle;
    unsigned device_handle;

    INFO("QUnibusAdapter: Registering device %s", device.name.value.c_str());

    assert(device.handle == 0); // must not be installed already
    assert(device.register_count <= MAX_IOPAGE_REGISTERS_PER_DEVICE);

    // assign to "backplane position"
    // search next free "slot"
    device_handle = 1; // reserve 0 for special use
    while (device_handle <= MAX_DEVICE_HANDLE && devices[device_handle] != NULL)
        device_handle++;
    if (device_handle > MAX_DEVICE_HANDLE) {
        ERROR("register_device() Tried to register more than %u devices!", MAX_DEVICE_HANDLE);
        return false;
    }
    devices[device_handle] = &device;
    device.handle = device_handle; // tell the device its slots

    // lookup register_handles[]
    memset(register_handle_used, 0, sizeof(register_handle_used)); // all 0

    // verify: does the device implement a register address already
    // in use by another device? it happened!
    for (i = 0; i < device.register_count; i++) {
        qunibusdevice_register_t *device_reg = &(device.registers[i]);
        device_reg->addr = device.base_addr.value + 2 * i;
        uint8_t reghandle = IOPAGE_REGISTER_ENTRY(*pru_iopage_registers, device_reg->addr);
        if (reghandle != 0 && reghandle != IOPAGE_REGISTER_HANDLE_ROM)
            FATAL(
                "register_device() IO page address conflict: %s implements register at %s, belongs already to other device.",
                device.name.value.c_str(), qunibus->addr2text(device_reg->addr));
    }

    for (i = 0; i < 0x1000; i++) {
        // scan all addresses in IO page
        register_handle = pru_iopage_registers->register_handles[i];
        assert(register_handle < MAX_IOPAGE_REGISTER_COUNT || register_handle == IOPAGE_REGISTER_HANDLE_ROM);
        if (register_handle != 0 && register_handle != IOPAGE_REGISTER_HANDLE_ROM)
            // device registers may decdoe into ROM space, M9312
            register_handle_used[register_handle] = true;
    }
    // allocate new handles for registers of device
    // we could try to find a "hole" of size device->register_count, but simply add to the end
    // find highest handle uses so far.
    register_handle = 0; // biggest handle in use
    // handle#0 not to be used!
    for (i = 1; i < MAX_IOPAGE_REGISTER_COUNT; i++)
        if (register_handle_used[i])
            register_handle = i;
    unsigned free_handles = MAX_IOPAGE_REGISTER_COUNT - register_handle - 1;
    if (free_handles < device.register_count) {
        ERROR("register_device() can not register device %s, needs %d register, only %d left.",
              device.name.value.c_str(), device.register_count, free_handles);
        return false;
    }
    register_handle++; // first free handle

    // add registers of device (controller) to global shared register map

    for (i = 0; i < device.register_count; i++) {
        qunibusdevice_register_t *device_reg = &(device.registers[i]);
        volatile pru_iopage_register_t *pru_iopage_reg = &pru_iopage_registers->registers[register_handle];
        // complete link to device
        device_reg->device = &device;
        device_reg->index = i;
        device_reg->addr = device.base_addr.value + 2 * i;

        device_reg->pru_iopage_register = pru_iopage_reg; // link controller register to shared descriptor
        device_reg->register_handle = register_handle;
        pru_iopage_reg->value = device_reg->reset_value; // init
        pru_iopage_reg->writable_bits = device_reg->writable_bits;
        pru_iopage_reg->event_flags = 0;
        // "active" devices are marked with controller handle
        if (device_reg->active_on_dati || device_reg->active_on_dato) {
            if (device_reg->active_on_dati && !device_reg->active_on_dato && device_reg->writable_bits != 0x0000) {
                FATAL(
                    "register_device() Register configuration error for device %s, register idx %u:\n"
                    "A writable device register may not be passive on DATO and active on DATI.\n"
                    "Passive DATO -> value written only saved in shared " QUNIBUS_NAME " reg value\n"
                    "Active DATI: shared " QUNIBUS_NAME " reg value updated from flipflops -> DATO value overwritten\n"
                    "make DATO active too -> datao value saved in DATO flipflops",
                    device.name.value.c_str(), i);
            }
            pru_iopage_reg->event_device_handle = device.handle;
            pru_iopage_reg->event_device_register_idx = i; // # of register in device
            if (device_reg->active_on_dati)
                pru_iopage_reg->event_flags |= IOPAGEREGISTER_EVENT_FLAG_DATI;
            if (device_reg->active_on_dato)
                pru_iopage_reg->event_flags |= IOPAGEREGISTER_EVENT_FLAG_DATO;
        } else {
            pru_iopage_reg->event_device_handle = 0;
            pru_iopage_reg->event_device_register_idx = 0; // not used, PRU handles logic
        }
        // write register handle into IO page address map
        uint32_t addr = device.base_addr.value + 2 * i; // devices have always sequential address register range!
        IOPAGE_REGISTER_ENTRY(*pru_iopage_registers,addr)= register_handle;
// printf("!!! register @0%06o = reg 0x%x\n", addr, reghandle) ;
        register_handle++;
    }
    // if its a CPU, switch PRU to "with_CPU"
    unibuscpu_c *cpu = dynamic_cast<unibuscpu_c*>(&device);
    if (cpu) {
        assert(registered_cpu == NULL); // only one allowed!
        registered_cpu = cpu;
        // enable/disable will start/stop CPU arbitrator on PRU
//		mailbox->cpu_enable = 1;
//		mailbox_execute(ARM2PRU_CPU_ENABLE);
    }
    return true;
}

// unregister_device ... "plugout" the device from QBUS/UNIBUS backplane
// - clear handle
// - remove device registers from shared address maps
void qunibusadapter_c::unregister_device(qunibusdevice_c& device) {
    unsigned i;

    assert(device.handle > 0); // must have been installed

    INFO("QUnibusAdapter: UnRegistering device %s.", device.name.value.c_str());

    // if its a CPU, disable PRU to "with_CPU"
    unibuscpu_c *cpu = dynamic_cast<unibuscpu_c*>(&device);
    if (cpu) {
        mailbox->param = 0;
        mailbox_execute(ARM2PRU_CPU_ENABLE);
        registered_cpu = NULL;
    }

    // remove "from backplane"
    devices[device.handle] = NULL;
    device.handle = 0;

    for (i = 0; i < device.register_count; i++) {
        uint32_t addr = device.base_addr.value + 2 * i; // devices have always sequential address register range!
        IOPAGE_REGISTER_ENTRY(*pru_iopage_registers,addr)= 0;
        // register descriptor remain unchanged, also device->members
    }
}

// mark an IOpage address as "ROM"
// device register may overlay ROM space (M9312 7730024/26)
// result: false = failure
bool qunibusadapter_c::register_rom(uint32_t address) {
    // must be even, must be in IOpage
    assert((address & 1) == 0);
    assert(address >=  qunibus->iopage_start_addr && address <= qunibus->addr_space_byte_count-2);

    volatile uint8_t *iopage_cellhandle = &IOPAGE_REGISTER_ENTRY(*pru_iopage_registers, address);
    // assert: no other ROM installed here, proper nesting of install()/uninstall() required
    assert(*iopage_cellhandle != IOPAGE_REGISTER_HANDLE_ROM);

    // must not replace device register
    if (*iopage_cellhandle != 0 && *iopage_cellhandle != IOPAGE_REGISTER_HANDLE_ROM)
        return false; // address occupied by device register

    // mark address as ROM
    *iopage_cellhandle = IOPAGE_REGISTER_HANDLE_ROM;
    return true;
}
void qunibusadapter_c::unregister_rom(uint32_t address) {
    // must be even, must be in IOpage
    assert((address & 1) == 0);
    assert(address >= qunibus->iopage_start_addr && address <= qunibus->addr_space_byte_count-2);

    volatile uint8_t *iopage_cellhandle = &IOPAGE_REGISTER_ENTRY(*pru_iopage_registers, address);
    // assert: ROM (or device reg) installed here, proper nesting of install()/uninstall() required
    if (*iopage_cellhandle == 0)
        // may already be 0 if overlaying device register unregistered
        return ;
    // do not remove overlaying device registers
    if (*iopage_cellhandle != IOPAGE_REGISTER_HANDLE_ROM)
        return;

    // clear ROM marker
    assert(*iopage_cellhandle == IOPAGE_REGISTER_HANDLE_ROM);
    *iopage_cellhandle = 0;
}

// is address an emulated ROM?
bool qunibusadapter_c::is_rom(uint32_t address) {
    if (address & 1)
        return false ; // only even addr are ROM
    if (address <  qunibus->iopage_start_addr)
        return false ; // ROM only in IOPAGE
    assert(address <= qunibus->addr_space_byte_count-2);
    uint8_t iopage_cellhandle = IOPAGE_REGISTER_ENTRY(*pru_iopage_registers, address);
    return (iopage_cellhandle == IOPAGE_REGISTER_HANDLE_ROM) ;
}


/*** Access requests in [level,slot] table ***/

// initialize slot tables in empty state
void qunibusadapter_c::requests_init(void) {
    for (unsigned level_index = 0; level_index < PRIORITY_LEVEL_COUNT; level_index++) {
        priority_request_level_c *prl = &request_levels[level_index];
        for (unsigned slot = 0; slot < PRIORITY_SLOT_COUNT; slot++)
            prl->slot_request[slot] = NULL;
        prl->slot_request_mask = 0;
        prl->active = NULL;
    }
}

// put a request into the level/slot table
// do not yet activate!
void qunibusadapter_c::request_schedule(priority_request_c& request) {
    // Must run under  pthread_mutex_lock(&requests_mutex);
    priority_request_level_c *prl = &request_levels[request.level_index];
    // DEBUG("request_schedule") ;

    // a device may reraise on of its own interrupts, but not an DMA on same slot
    if (dynamic_cast<dma_request_c *>(&request)) {
        if (prl->slot_request[request.priority_slot] != NULL)
            FATAL("Concurrent DMA requested for slot %d.", (unsigned )request.priority_slot);
    } else if (dynamic_cast<intr_request_c *>(&request)) {
        if (prl->slot_request[request.priority_slot] != NULL) {
            qunibusdevice_c *slotdevice = prl->slot_request[request.priority_slot]->device;
            if (slotdevice != request.device)
                FATAL(
                    "Devices %s and %s share both slot %u for INTR request with priority index %u",
                    slotdevice ? slotdevice->name.value.c_str() : "NULL",
                    request.device->name.value.c_str(), (unsigned )request.priority_slot,
                    (unsigned )request.level_index);
            // DEBUG("request_schedule(): update request %p into level %u, slot %u",&request, request.level_index, request.slot);
        } else {
            //	DEBUG("request_schedule(): insert request %p into level %u, slot %u",&request, request.level_index, request.slot);
        }
    }

    prl->slot_request[request.priority_slot] = &request; // mark slot with request
    prl->slot_request_mask |= (1 << request.priority_slot);  // set slot bit
}

// Cancel all pending device_DMA and IRQ requests of every level.
// requests which are active on the PRU (->active) are left running,
// and the PRU terminates DMA sequences on INIT.
void qunibusadapter_c::requests_cancel_scheduled(void) {
    priority_request_c *req;

    // Must run under pthread_mutex_lock(&requests_mutex);
    for (unsigned level_index = 0; level_index < PRIORITY_LEVEL_COUNT; level_index++) {
        priority_request_level_c *prl = &request_levels[level_index];
        prl->slot_request_mask = 0; // clear alls slot from request
        prl->active = NULL;

        for (unsigned slot = 0; slot < PRIORITY_SLOT_COUNT; slot++)
            if ((req = prl->slot_request[slot])) {
                dma_request_c *dmareq;
                req->executing_on_PRU = false;
                if ((dmareq = dynamic_cast<dma_request_c *>(req)))
                    dmareq->success = false; // device gets an DMA error, but will not understand
                prl->slot_request[slot] = NULL;
                // signal to blocking DMA() or INTR()
                pthread_mutex_lock(&req->complete_mutex);
                req->complete = true;
                pthread_cond_signal(&req->complete_cond);
                pthread_mutex_unlock(&req->complete_mutex);
            }
    }
}

/*
 // is a request of given level active on the PRU?
 bool qunibusadapter_c::request_is_active(unsigned level_index) {
 // Must run under  pthread_mutex_lock(&requests_mutex);
 priority_request_level_c *prl = &request_levels[level_index];
 return (prl->active != NULL);
 }
 */

// find highest prioritized request for a given level, via slots
priority_request_c *qunibusadapter_c::request_activate_lowest_slot(unsigned level_index) {
    /* To find the lowest slot with an active request of priority 'level'
     gcc __builtin_ffs "Returns one plus the index of the least significant 1-bit of x, or if x is zero, returns zero. "
     (= slow with highest priority within 'level')
     Is implemented on ARM as just 2 opcodes: rbit (bit reverse), clz (count number of leading zeros)
     VERY FAST (without sorting list)
     */
    // Must run under  pthread_mutex_lock(&);
    priority_request_level_c *prl = &request_levels[level_index];
    priority_request_c *rq;

    assert(prl->active == NULL);
    // DEBUG("request_activate_lowest_slot") ;

    unsigned slot = __builtin_ffs(prl->slot_request_mask);
    if (slot == 0)
        rq = NULL; // no slot requesting on this level
    else {
        rq = prl->slot_request[slot - 1]; // slot 0 -> bit 0 -> slot=1
        assert(rq != NULL);
    }
    prl->active = rq;
    // if (prl->active)
    // 	DEBUG("request_activate_lowest_slot(): ->active = dma_request %p, level %u, slot %u",prl->active, prl->active->level_index, prl->active->slot);
    // else
    // 	DEBUG("request_activate_lowest_slot(): ->active = NULL");
    assert((prl->slot_request_mask == 0) == (prl->active == NULL));

    return rq;
}

// is any request of higher or same level executed? Is the next request executed delayed?
bool qunibusadapter_c::request_is_blocking_active(uint8_t level_index) {
    while (level_index < PRIORITY_LEVEL_COUNT) {
        priority_request_level_c *prl = &request_levels[level_index];
        if (prl->active)
            return true;
        if (prl->slot_request_mask)
            return true;
        level_index++;
    }
    return false;
}

// helper: push the active request to the PRU for execution
// VB: the next request to schedule already calculated and saved in priority_request_level_c.active
void qunibusadapter_c::request_execute_active_on_PRU(unsigned level_index) {
    priority_request_level_c *prl = &request_levels[level_index];
    assert(prl->active);
    // Must run under  pthread_mutex_lock(&requests_mutex);
    // DEBUG("request_execute_active_on_PRU(level_idx=%u)", level_index);
    if (level_index == PRIORITY_LEVEL_INDEX_NPR) {

        dma_request_c *dmareq = dynamic_cast<dma_request_c *>(prl->active);
        assert(dmareq);

        // We do the device_DMA transfer in chunks so we can handle arbitrary buffer sizes.
        // (the PRU mailbox has limited space available.)

        // Push the chunk to the PRU.
        unsigned wordcount_remaining = dmareq->wordcount - dmareq->wordcount_completed_chunks();
        //dmareq->chunk_max_words = 2; // TEST
        dmareq->chunk_words = std::min(dmareq->chunk_max_words, wordcount_remaining);

        assert(dmareq->chunk_words); // if complete, the dmareq should not be active anymore
        if (dmareq->chunk_qunibus_start_addr >= qunibus->iopage_start_addr) {
#if defined(UNIBUS)
            // UniBone PRU doesn't handle IOpage addresses marked with IOpage bit 22
            mailbox->dma.startaddr = dmareq->chunk_qunibus_start_addr ;
#elif defined(QBUS)
            mailbox->dma.startaddr = dmareq->chunk_qunibus_start_addr | QUNIBUS_IOPAGE_ADDR_BITMASK ;
#endif
        } else
            mailbox->dma.startaddr = dmareq->chunk_qunibus_start_addr ;
        mailbox->dma.buscycle = dmareq->qunibus_control;
        mailbox->dma.wordcount = dmareq->chunk_words;
        mailbox->dma.cpu_access = dmareq->is_cpu_access;

        // Copy outgoing data into mailbox device_DMA buffer
        if (QUNIBUS_CYCLE_IS_DATO(dmareq->qunibus_control)) {
            memcpy((void*) mailbox->dma.words, dmareq->chunk_buffer_start(),
                   2 * dmareq->chunk_words);
        }

        //
        // Start the PRU:
        // signal still not cleared in worker() while processing this
        // assert(mailbox->events.event_dma == 0); // previous signal must have been processed
        _DEBUG(
            "request_execute_active_on_PRU() DMA: dev %s, ->active = dma_request %p, start = %s, control=%u, wordcount=%u, data=%06o ...",
            dmareq->device ? dmareq->device->name.value.c_str() : "none", dmareq,
            qunibus->addr2text(mailbox->dma.startaddr), (unsigned) mailbox->dma.buscycle,
            (unsigned) mailbox->dma.wordcount, (unsigned) mailbox->dma.words[0]);
        mailbox->dma.cur_status = 0; // device DMA, not by CPU
        mailbox_execute(ARM2PRU_DMA);
        // scheduling is fast, on complete there's a signal.
        dmareq->executing_on_PRU = true;

        /* if DMA is done in multiple chunks,
         then after PRU is complete, we don not call "active_complete() to remove the request.
         Instead we leave it active, with transferred data clipped from buffer start.
         the "complete" signal will relaunch the remaining dma request automatically
         we need a new address "chunk_start_addr" in dma_request_c

         As side effect, a higher priorized device may be serviced before the next chunk is transmitted,
         This is intended and prevents data loss.
         */

    } else {
        // Not DMA? must be INTR
        intr_request_c *intrreq = dynamic_cast<intr_request_c *>(prl->active);
        assert(intrreq);

        // Handle interrupt request to PRU. Setup mailbox:
        mailbox->intr.level_index = intrreq->level_index;
        mailbox->intr.vector[intrreq->level_index] = intrreq->vector;
        if (intrreq->interrupt_register)
            mailbox->intr.iopage_register_handle =
                intrreq->interrupt_register->register_handle;
        else
            mailbox->intr.iopage_register_handle = 0; // none
        mailbox->intr.iopage_register_value = intrreq->interrupt_register_value;

        // decode index 0..3 = BR4..BR7 => PRU signal register bit
        assert(intrreq->level_index <= PRIORITY_LEVEL_INDEX_BR7);
        mailbox->intr.priority_arbitration_bit =
            priority_level_idx_to_arbitration_bit[intrreq->level_index];

        // start on PRU
        // PRU have got arbitration for an INTR of different level in the mean time:
        // assert(mailbox->events.event_intr == 0) would trigger
        mailbox_execute(ARM2PRU_INTR);
        intrreq->executing_on_PRU = true; // waiting for GRANT
        
        // PRU now changes state
    }

    /* when PRU is finished, the worker() gets a signal,
     then worker_device_dma_chunk_complete_event() or worker_intr_complete_event() is called.
     On INTR or last DMA chunk, the request is completed.
     It is removed from the slot schedule table and request->complete_conmd is signaled
     to DMA() or INTR()
     */
}

// remove request pointer currently handled by PRU from tables
// also called on INTR_CANCEL
void qunibusadapter_c::request_active_complete(unsigned level_index, bool signal_complete) {
    // Must run under  pthread_mutex_lock(&requests_mutex);

    priority_request_level_c *prl = &request_levels[level_index];
    if (!prl->active) // PRU completed after INIT cleared the tables
        return;
    // DEBUG("request_active_complete") ;

    unsigned slot = prl->active->priority_slot;
    //if (prl->slot_request[slot] != prl->active)
    //	mailbox_execute(ARM2PRU_HALT) ; // LA: trigger on timeout REG_WRITE
    // active not in table, if table cleared by  INIT	requests_cancel_scheduled()
    assert(prl->slot_request[slot] == prl->active); // must still be in table

    // mark as complete
    prl->active->executing_on_PRU = false;
//	prl->active->complete = true;
    // remove table entries
    prl->slot_request[slot] = NULL; // clear slot from request
    prl->slot_request_mask &= ~(1 << slot); // mask out slot bit

    priority_request_c *tmprq = prl->active;
    prl->active = NULL;

    if (signal_complete) {
        // signal to DMA() or INTR()
        pthread_mutex_lock(&tmprq->complete_mutex);
        tmprq->complete = true;
        pthread_cond_signal(&tmprq->complete_cond);
        pthread_mutex_unlock(&tmprq->complete_mutex);
    }

}

// Request a DMA cycle from Arbitrator.
// unibus_control = QUNIBUS_CYCLE_DATI or _DATO
// unibus_end_addr = last accessed address (success or timeout) and timeout condition
// result: false on QBUS/UNIBUS timeout
// Blocking == true: DMA() wait for request to complete
// Blocking == false: return immediately, the device logic should
//		 evaluate the request.complete flag or wait for the mutex

void qunibusadapter_c::DMA(dma_request_c& dma_request, bool blocking, uint8_t qunibus_cycle,
                           uint32_t unibus_addr, uint16_t *buffer, uint32_t wordcount) {
    assert(dma_request.priority_slot < PRIORITY_SLOT_COUNT);
    assert(dma_request.level_index == PRIORITY_LEVEL_INDEX_NPR);

    // setup device request
    assert(wordcount > 0);
    assert((unibus_addr + 2*wordcount) <= qunibus->addr_space_byte_count);
    // lowest priority reserved for CPU
    assert(!dma_request.is_cpu_access || dma_request.priority_slot == 31);

#if defined(UNIBUS)
    if (!dma_request.is_cpu_access && qunibus->is_address_overlay_active())
        ERROR("UNIBUS ADDR lines overlayed (for M9312 boot) @ %s. Only CPU 24/26 access intended!", qunibus->addr2text(unibus_addr)) ;
#endif

    // ignore calls if INIT condition
    if (line_INIT) {
        dma_request.complete = true;
        return;
    }
    pthread_mutex_lock(&requests_mutex); // lock schedule table operations

    // In contrast to re-raised INTR, overlapping DMA requests from same board
    // must not be ignored (different DATA situation) and are an device implementation error.
    // If a device indeed has multiple DMA channels, it must use different pseudo-slots.
    priority_request_level_c *prl = &request_levels[PRIORITY_LEVEL_INDEX_NPR];
    assert(prl->slot_request[dma_request.priority_slot] == NULL); // not scheduled or prev completed

    // 	dma_request.level-index, priority_slot in constructor
    dma_request.complete = false;
    dma_request.success = false;
    dma_request.executing_on_PRU = false;
    dma_request.qunibus_control = qunibus_cycle;
    dma_request.qunibus_start_addr = unibus_addr;
    dma_request.chunk_qunibus_start_addr = unibus_addr;
    dma_request.qunibus_end_addr = 0; // last transfered addr, or error position
    dma_request.buffer = buffer;
    dma_request.wordcount = wordcount;
    dma_request.chunk_max_words = PRU_MAX_DMA_WORDCOUNT; // PRU limit, maybe less
    _DEBUG("DMA() req: dev %s, %s @ %s, wordcount %d",
           dma_request.device ? dma_request.device->name.value.c_str() : "none",
           qunibus_c::control2text(qunibus_cycle), qunibus->addr2text(unibus_addr), wordcount);

    // put into schedule tables

    request_schedule(dma_request); // assertion, if twice for same slot
    if (!prl->active) {
//	if (!request_is_active(dma_request.level_index)) {
        // no device_DMA current performed: start immediately
        // else triggered by PRU signals
        request_activate_lowest_slot(dma_request.level_index);
        request_execute_active_on_PRU(dma_request.level_index);
    }
    pthread_mutex_unlock(&requests_mutex);

    // DEBUG("device DMA start: %s @ %06o, len=%d", qunibus->control2text(qunibus_cycle), unibus_addr, wordcount);

    if (dma_request.is_cpu_access) {
        // NO wait for PRU signal, instead busy waiting. CPU thread blocked.
        // Reason: SPEED. CPU does high frequency single word accesses.
        bool completed = false;
// ARM_DEBUG_PIN1(1); // CPU20 performace
        do {
            // CPU thread is now spinning
            // wait until CPU access scheduled and processed on PRU
            // in parallel, other device threads call DMA()
            pthread_mutex_lock(&requests_mutex);
            dma_request_c *activereq = dynamic_cast<dma_request_c *>(prl->active);
//if (activereq == &dma_request)
//	printf("a\n") ;
//if (DMA_STATE_IS_COMPLETE(mailbox->dma.cur_status))
//	printf("b\n") ;
            if ((activereq == &dma_request) && !EVENT_IS_ACKED(*mailbox, dma)) {
                assert(activereq->is_cpu_access);
                // transfer DATI data to buffer, set success flag, schedule next request
                worker_device_dma_chunk_complete_event(); // do not signal, uses complete_mutex
                EVENT_ACK(*mailbox, dma);
                completed = true;
            } else if (activereq == NULL)
                // request aborted by worker_power_event()
                completed = true;
            pthread_mutex_unlock(&requests_mutex); //&dma_request.complete_mutex);
        } while (!completed);
//ARM_DEBUG_PIN1(0); // CPU20 performace

    } else if (blocking) {
        pthread_mutex_lock(&dma_request.complete_mutex);
        // DMA() is blocking: Wait for request to finish.
        //	pthread_mutex_lock(&dma_request.mutex);
        while (!dma_request.complete) {
            int res = pthread_cond_wait(&dma_request.complete_cond,
                                        &dma_request.complete_mutex);
            assert(!res);
        }
        pthread_mutex_unlock(&dma_request.complete_mutex);
    }
}

// do DATO/DATI as master CPU.
// result: success, else BUS TIMEOUT
void qunibusadapter_c::cpu_DATA_transfer(dma_request_c& cpu_data_transfer_request,
        uint8_t unibus_control, uint32_t qunibus_cycle, uint16_t *buffer) {
    // no NPR/NPG/SACK arbitration
    // no PRU->ARM signal on complete
    cpu_data_transfer_request.is_cpu_access = true;
    // CPU memory access is serialized with DMA,
    // but less then all other device DMA requests
    // so set "priority_slot to max=31, despite a CPU plugs into left most slot 0
    // Also less then INTR, thats implementend in PRU statemachine_arbitration_master()
    cpu_data_transfer_request.priority_slot = 31;
    // "blocking" flag not used
    DMA(cpu_data_transfer_request, true, unibus_control, qunibus_cycle, buffer, 1);
}

// A device raises an interrupt and simultaneously changes a value in
// one of its registers, the "interrupt register".
// interrupt_register' may be NULL if none.
// INTR() is NOT BLOCKING: it returns immediately.
// the actual interrupt vector is transfered when CPU interrupt level is lowered enough and
// other arbitration rules apply, which may never be the case.
// While pending, device may call INTR() again, causing waiting PRU requests to be modified.

void qunibusadapter_c::INTR(intr_request_c& intr_request,
                            qunibusdevice_register_t *interrupt_register, uint16_t interrupt_register_value) {
    assert(intr_request.priority_slot < PRIORITY_SLOT_COUNT);
    assert(intr_request.level_index <= 3);
    assert((intr_request.vector & 3) == 0); // multiple of 2 words

    // ignore calls if INIT condition
    if (line_INIT) {
        intr_request.complete = true;
        return;
    }

    priority_request_level_c *prl = &request_levels[intr_request.level_index];
    pthread_mutex_lock(&requests_mutex); // lock schedule table operations
//if (intr_request.device->log_level == LL_DEBUG)
    DEBUG("INTR() req: dev %s, slot/level/vector= %d/%d/%03o",
          intr_request.device->name.value.c_str(), (unsigned ) intr_request.priority_slot,
          intr_request.level_index + 4, intr_request.vector);
    // Is an INTR with same slot and level already executed on PRU
    // or waiting in the schedule table?
    // If yes: do not re-raise, will be completed at some time later.
    if (prl->slot_request[intr_request.priority_slot] != NULL) {
        intr_request_c *scheduled_intr_req =
            dynamic_cast<intr_request_c *>(prl->slot_request[intr_request.priority_slot]);
        assert(scheduled_intr_req);
        // A device may re-raised a pending INTR again
        // (quite normal situation when other ISRs block, CPU overload)
        // A re-raise will be ignored.
        // ! Another device MAY NOT reraise an INTR with same slot/level
        // ! (else complete signals may be routed to wrong device)
        assert(scheduled_intr_req->device == intr_request.device);
        assert(scheduled_intr_req->vector == intr_request.vector);
        // if different vector, it may not be ignored -> change in program flow

        // If device uses multiple INTRs with different vectors (DL11 rcv+xmt),
        // it must use different pseudo-slots.

        // scheduled and request_active_complete() not called
        pthread_mutex_unlock(&requests_mutex);
        if (interrupt_register) {
            DEBUG("INTR() delayed with IR");
            // if device re-raises a blocked INTR, CSR must complete immediately
            intr_request.device->set_register_dati_value(interrupt_register,
                    interrupt_register_value, __func__);
        } else {
            DEBUG("INTR() delayed without IR");
        }

        return; // do not schedule a 2nd time
    }

    intr_request.complete = false;
    intr_request.executing_on_PRU = false;

    if (interrupt_register)
        assert(intr_request.device == interrupt_register->device);

    // The associated device interrupt register (if any) should be updated
    // atomically with raising the INTR signal line by PRU.
    if (interrupt_register && request_is_blocking_active(intr_request.level_index)) {
        DEBUG("INTR() delayed, IR now");
        //	one or more another requests are handled by PRU: INTR signal delayed by Arbitrator,
        // write intr register asynchronically here.
        intr_request.device->set_register_dati_value(interrupt_register,
                interrupt_register_value, __func__);
        intr_request.interrupt_register = NULL; // don't do a 2nd  time
    } else { // forward to PRU
        DEBUG("INTR() IR forward to PRU");
        // 	intr_request.level_index, priority_slot, vector in constructor
        intr_request.interrupt_register = interrupt_register;
        intr_request.interrupt_register_value = interrupt_register_value;
    }

    // put into schedule tables
    request_schedule(intr_request); // assertion, if twice for same slot

    if (!prl->active) {
        // INTR of this level can be raised immediately
        // If other level active, let PRU atomically set the interrupt register value.
        request_activate_lowest_slot(intr_request.level_index);
        request_execute_active_on_PRU(intr_request.level_index);
        // else activation triggered by PRU signal in worker()
    }

    pthread_mutex_unlock(&requests_mutex);  // work on schedule table finished

    /*
     // If INTR() is blocking: Wait for request to finish.
     pthread_mutex_lock(&intr_request.mutex);
     while (!intr_request.complete) {
     pthread_cond_wait(&intr_request.complete_cond, &intr_request.mutex);
     }
     pthread_mutex_unlock(&intr_request.mutex);
     */
}

/* A device may cancel an INTR request, if not yet GRANTed by Arbitrator.
 Maybe useful if device sees INTR is not handeled by Arbitrator due to
 blocked interrupt of this level.
 Relevance of this usage pattern unclear, but used in KW11 diag, ZDLDI0 Test 17.
 After cancelation, ARM receives NO completion event from PRU.
 */
void qunibusadapter_c::cancel_INTR(intr_request_c& intr_request) {
    uint8_t level_index = intr_request.level_index; // alias
    priority_request_level_c *prl = &request_levels[level_index];
    if (prl->slot_request[intr_request.priority_slot] == NULL)
        return; // not scheduled or active

    pthread_mutex_lock(&requests_mutex); // lock schedule table operations
    if (&intr_request == prl->active) {
        // already on PRU
        assert(level_index <= PRIORITY_LEVEL_INDEX_BR7);
        mailbox->intr.priority_arbitration_bit =
            priority_level_idx_to_arbitration_bit[level_index];
        mailbox_execute(ARM2PRU_INTR_CANCEL);
        request_active_complete(level_index, true);

        // restart next request
        request_activate_lowest_slot(level_index);
        if (prl->active)
            request_execute_active_on_PRU(level_index);
    } else {
        // not active on PRU: just remove from schedule table
        prl->slot_request[intr_request.priority_slot] = NULL; // clear slot from request
        prl->slot_request_mask &= ~(1 << intr_request.priority_slot); // mask out slot bit
    }
    // both empty, or both filled
    assert((prl->slot_request_mask == 0) == (prl->active == NULL));

    pthread_mutex_lock(&intr_request.complete_mutex);
    pthread_cond_signal(&intr_request.complete_cond);
    pthread_mutex_unlock(&intr_request.complete_mutex);

    pthread_mutex_unlock(&requests_mutex); // lock schedule table operations

}

// set state of INIT
void qunibusadapter_c::worker_init_event() {
    unsigned device_handle;
    qunibusdevice_c *device;
    // notify device on change of INIT line
    DEBUG("worker_init_event(): INIT %s", line_INIT ? "asserted" : "negated");
    for (device_handle = 0; device_handle <= MAX_DEVICE_HANDLE; device_handle++)
        if ((device = devices[device_handle])) {
            device->init_asserted = line_INIT;
            device->on_init_changed();
        }

    // Clear bus request queues,
    pthread_mutex_lock(&requests_mutex);
    requests_cancel_scheduled();
    // reset all scheduled tables, also requests on PRU
    requests_init();
    pthread_mutex_unlock(&requests_mutex);
}

void qunibusadapter_c::worker_power_event(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
    unsigned device_handle;
    qunibusdevice_c *device;
    // notify device on change of ACLO/DCLO lines
#if defined(UNIBUS)
    DEBUG("worker_power_event(aclo_edge=%d=%s, dclo_edge=%d=s)",
          (int)aclo_edge, signal_edge_text(aclo_edge), (int)dclo_edge,signal_edge_text(dclo_edge));
#elif defined(QBUS)
    DEBUG("worker_power_event(aclo_edge=%d=%s (ACLO=NOT POK), dclo_edge=%d=%s (DCLO=NOT DCOK)",
          (int)aclo_edge, signal_edge_text(aclo_edge), (int)dclo_edge,signal_edge_text(dclo_edge));
#endif
    for (device_handle = 0; device_handle <= MAX_DEVICE_HANDLE; device_handle++)
        if ((device = devices[device_handle])) {
            device->on_power_changed(aclo_edge, dclo_edge);
        }

    // in true power fail, terminate pending DMA/CPU transfer
    if (dclo_edge == SIGNAL_EDGE_RAISING) {
        // Clear bus request queues,
        pthread_mutex_lock(&requests_mutex);
        requests_cancel_scheduled();
        // reset all scheduled tables, also requests on PRU
        requests_init();
        pthread_mutex_unlock(&requests_mutex);
    }
}

// process DATI/DATO access to active device registers

void qunibusadapter_c::worker_deviceregister_event() {
    unsigned device_handle;
    qunibusdevice_c *device;
    device_handle = mailbox->events.deviceregister.device_handle;
    assert(device_handle);
    device = devices[device_handle];
    unsigned evt_idx = mailbox->events.deviceregister.register_idx;
    uint32_t evt_addr = mailbox->events.deviceregister.addr;
    // normally evt_data == device_reg->pru_iopage_register->value
    // but shared value gets desorted if INIT in same event clears the registers before DATO
    uint16_t evt_data = mailbox->events.deviceregister.data;
    qunibusdevice_register_t *device_reg = &(device->registers[evt_idx]);
    uint8_t unibus_control = mailbox->events.deviceregister.unibus_control;

    // QBUS: for IOpage only addr bits <12:0> transferred,
    // "IOPage" signal BS7 encoded in QUNIBUS_IOPAGE_ADDR_BITMASK
#if defined(QBUS)
    assert(evt_addr & QUNIBUS_IOPAGE_ADDR_BITMASK) ; // must be in marked IOpage
#endif
    evt_addr = qunibus->iopage_start_addr + (evt_addr & 0x1fff) ;


    /* call device event callback

     The PRU has only one "value" for each register, but "active" registers have separate
     read(DATI) and write(DATO) flipflops. So if register is "active:
     BEFORE processing of logic state changes:
     DATO: save written value into .active_write_val
     restore changed shared PRU register with .active_read_val for next read
     */
    if (device_reg->active_on_dati && !QUNIBUS_CYCLE_IS_DATO(unibus_control)) {
        // register is read with DATI, this changes the logic state
        evt_addr &= ~1; // make even
        assert(evt_addr == device->base_addr.value + 2 * evt_idx);
        unibus_control = QUNIBUS_CYCLE_DATI;
        // read access: dati-flipflops do not change

        // signal: changed by QBUS/UNIBUS
        device->log_register_event("DATI", device_reg);

        device->on_after_register_access(device_reg, unibus_control);
    } else if (device_reg->active_on_dato && QUNIBUS_CYCLE_IS_DATO(unibus_control)) {
        //		uint16_t reg_value_written = device_reg->pru_iopage_register->value;
        //	restore value accessible by DATI
        device_reg->pru_iopage_register->value = device_reg->active_dati_flipflops;
        // Restauration of pru_iopage_register->value IS NOT ATOMIC against device logic threads.
        // Devices must use only reg->active_dati_flipflops !
        switch (unibus_control) {
        case QUNIBUS_CYCLE_DATO:
            // write into a register with separate read/write flipflops
            assert(evt_addr == device->base_addr.value + 2 * evt_idx);
            // clear unused bits, save written value
            device_reg->active_dato_flipflops = evt_data & device_reg->writable_bits;
            // signal: changed by QBUS/UNIBUS
            device->log_register_event("DATO", device_reg);
            break;
        case QUNIBUS_CYCLE_DATOB:
            // QBUS/UNIBUS may access only 8bit half of register with DATOB.
            // convert all active registers accesses to 16 bit
            evt_data &= device_reg->writable_bits; // clear unused bits
            // save written value
            if (evt_addr & 1) // odd address: bits 15:8 written
                device_reg->active_dato_flipflops = (device_reg->active_dato_flipflops & 0x00ff)
                                                    | (evt_data & 0xff00);
            else
                // even address : bits 7:0 written
                device_reg->active_dato_flipflops = (device_reg->active_dato_flipflops & 0xff00)
                                                    | (evt_data & 0x00ff);
            unibus_control = QUNIBUS_CYCLE_DATO; // simulate 16 bit access
            // signal: changed by QBUS/UNIBUS
            device->log_register_event("DATOB", device_reg);
            break;
        }
        device->on_after_register_access(device_reg, unibus_control);
        /*
         DEBUG(LL_DEBUG, LC_UNIBUS, "dev.reg=%d.%d, %s, addr %06o, data %06o->%06o",
         device_handle, evt_idx,
         qunibus_c::control2text(mailbox->event.unibus_control), evt_addr,
         oldval, device_reg->pru_iopage_register->value);
         */
    }
}

// called by PRU signal when DMA transmission complete
// Called for device DMA() chunk,
// or cpu_DATA_transfer()
void qunibusadapter_c::worker_device_dma_chunk_complete_event() {
    priority_request_level_c *prl = &request_levels[PRIORITY_LEVEL_INDEX_NPR];
    bool more_chunks;
    // Must run under pthread_mutex_lock(&requests_mutex) ;

    dma_request_c *dmareq = dynamic_cast<dma_request_c *>(prl->active);

    assert(dmareq != NULL);
    // fix PRU data struct: remove IOPAGE bit from mailbox struct, was set im mailbox_execute()
    mailbox->dma.startaddr &= ~QUNIBUS_IOPAGE_ADDR_BITMASK ;
    mailbox->dma.cur_addr &= ~QUNIBUS_IOPAGE_ADDR_BITMASK ;
    dmareq->qunibus_end_addr = mailbox->dma.cur_addr; // track emnd of trasnmission, eror position
    unsigned wordcount_transferred = dmareq->wordcount_completed_chunks()
                                     + mailbox->dma.wordcount;
    assert(wordcount_transferred <= dmareq->wordcount);
    assert(!dmareq->is_cpu_access || dmareq->wordcount == 1); // CPU accesses only single words
    if (QUNIBUS_CYCLE_IS_DATI(mailbox->dma.buscycle)) {
        // guard against buffer overrun
        // PRU read chunk data from QBUS/UNIBUS into mailbox
        // copy result cur_DMA_wordcount from mailbox->DMA buffer to cur_DMA_buffer
        memcpy(dmareq->chunk_buffer_start(), (void *) mailbox->dma.words,
               2 * mailbox->dma.wordcount);
    }
    if (mailbox->dma.cur_status != DMA_STATE_READY) {
        // failure: abort remaining chunks
        dmareq->success = false;
        more_chunks = false;
    } else if (wordcount_transferred == dmareq->wordcount) {
        // last chunk completed
        dmareq->success = true;
        more_chunks = false;
    } else {
        // more data to transfer: next chunk.
        assert(!dmareq->is_cpu_access); // CPU accesses only single words
        dmareq->chunk_qunibus_start_addr = mailbox->dma.cur_addr + 2;
        // dmarequest remains prl->active and ->busy

        _DEBUG(
            "DMA chunk complete: dev %s, %s @ %s..%s, wordcount %d, data=%06o, %06o, ... %s",
            prl->active->device ? prl->active->device->name.value.c_str() : "none",
            qunibus->control2text(mailbox->dma.buscycle), qunibus->addr2text(mailbox->dma.startaddr),
            qunibus->addr2text(mailbox->dma.cur_addr), mailbox->dma.wordcount, mailbox->dma.words[0],
            mailbox->dma.words[1], dmareq->success ? "OK" : "TIMEOUT");

        // re-activate this request, or choose another with higher slot priority,
        // inserted in parallel (interrupt this DMA)
        prl->active = NULL;
        request_activate_lowest_slot(PRIORITY_LEVEL_INDEX_NPR);

        request_execute_active_on_PRU(PRIORITY_LEVEL_INDEX_NPR);
        more_chunks = true;

    }
    if (!more_chunks) {
        _DEBUG("DMA ready: %s @ %s..%s, wordcount %d, data=%06o, %06o, ... %s",
               qunibus->control2text(dmareq->qunibus_control),
               qunibus->addr2text(dmareq->qunibus_start_addr),
               qunibus->addr2text(dmareq->qunibus_end_addr), dmareq->wordcount, dmareq->buffer[0],
               dmareq->buffer[1], dmareq->success ? "OK" : "TIMEOUT");

        // clear from schedule table of this level
        // CPu memory accesses are not signaled, but polled in DMA()
        request_active_complete(PRIORITY_LEVEL_INDEX_NPR, /*signal*/!dmareq->is_cpu_access);

        // check and execute DMA on other priority_slot
        if (request_activate_lowest_slot(PRIORITY_LEVEL_INDEX_NPR))
            request_execute_active_on_PRU(PRIORITY_LEVEL_INDEX_NPR);

    }
}

// called by PRU signal when INTR vector transmission complete
// OR the request has been canceled
// priority_level_index:  0..3 = BR4..BR7
void qunibusadapter_c::worker_intr_complete_event(uint8_t level_index) {
    // Must run under pthread_mutex_lock(&requests_mutex) ;
    priority_request_level_c *prl = &request_levels[level_index];

    // if 1st opcode of an ISR is a "clear of INTR" condition,
    // cancel_INTR() may be called in worker_deviceregister_event()
    // then the request is already removed from schedule table.
    //assert(prl->active);

    // clear from schedule table of this level
    request_active_complete(level_index, true);

    // activate next request of this level on PRU for priority arbitration
    request_activate_lowest_slot(level_index);
    if (prl->active) {
        _DEBUG("INTR() complete, next scheduled");
        request_execute_active_on_PRU(level_index);
    } else {
        _DEBUG("INTR() complete, no next scheduled");
    }
    // else INTRs for all slots of this level completed
}

// runs in background, catches and distributes PRU events
void qunibusadapter_c::worker(unsigned instance) {
    UNUSED(instance); // only one
    int res;
    bool any_event;

    // set thread priority to MAX.
    // - fastest response to select() call in prussdrv_pru_wait_event_timeout()
    //  (minimal I/O latency)
    // - not interrupted by other tasks while running
    // check with tool "top" or "htop".
    worker_init_realtime_priority(rt_device); // do not block while debugging
    // worker_init_realtime_priority(rt_max); // set to max prio

    // mailbox may be un-initialized

    while (!workers_terminate) {
        // Timing:
        // This is THE ONE mechanism where "realtime meets Linux"
        // To respond to the PRU signal, Linux must wake up schedule this thread
        // Test:
        // - set RT prio of this worker thread to true 100% (SCHED_FIFO. max prio,
        //		/proc/sys/kernel/sched_rt_runtime_us = -1
        // - let the PRU pulse a GPIO on signal_set
        // - let this worker raise a GPIO while its active
        // - verify with scope that PRU - signal to worker start is about 100-300us
        //	MUST NOT GET LARGER on any Linux activity,

        // main loop
        // wait 0.1 sec, just tell
        /* The prussdrv_pru_wait_event() function returns the number of times
         the event has taken place, as an unsigned int. There is no out-of-
         band value to indicate error (and it can wrap around to 0 if you
         run the program just a whole lot of times). */
        res = prussdrv_pru_wait_event_timeout(PRU_EVTOUT_0, 100000/*us*/);
//res = prussdrv_pru_wait_event(PRU_EVTOUT_0);
        // PRU may have raised more than one event before signal is accepted.
        // single combination of only INIT+DATI/O possible
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
        // uses select() internally: 0 = timeout, -1 = error, else event count received
        any_event = true;
        // at startup sequence, mailbox may be not yet valid
        while (mailbox && res > 0 && any_event) { // res is const
            any_event = false;
            // Process multiple events sent by PRU.
            //
            // while ARM accepts the signal, the PRU may set more events
            // critical a mix of INIT and DATI/DATO: RESET and register access follow directly
            // But no DATI/DATO occurs while INIT active. So reconstruct event order by
            // processing order: INIT_NEGATE, DATI/O, INIT_ASSERT, DCLO/ACLO
            signal_edge_enum aclo_edge = SIGNAL_EDGE_NONE ;
            signal_edge_enum dclo_edge = SIGNAL_EDGE_NONE ;
#if defined(UNIBUS)
            bool init_raising_edge = false;
            bool init_falling_edge = false;
#endif			
            if (!EVENT_IS_ACKED(*mailbox, init)) {
                any_event = true;
                // QBUS: bus blocked by DMR until EVENT_ACK for raising edge
//              printf("mailbox->events.init_signals_cur 0x%x\n", (unsigned)mailbox->events.init_signal_cur) ;
#if defined(UNIBUS)
                if (mailbox->events.init_signal_cur) {
                    if (!line_INIT)
                        init_raising_edge = true;
                    line_INIT = true;
                } else {
                    if (line_INIT)
                        init_falling_edge = true;
                    line_INIT = false;
                }
				if (!init_raising_edge && !init_falling_edge) {
					// clear stray event
	                EVENT_ACK(*mailbox, init);
				} else if (init_falling_edge) { // INIT asserted -> negated.  DATI/DATO cycle only possible after that.
	                // raising edge below ?!
                    worker_init_event();
	                EVENT_ACK(*mailbox, init); // PRU may re-raise and change mailbox now
                	}
                DEBUG(
                    "EVENT_INIT: init_signal_cur=0x%x, init_raise=%d, init_fall=%d",
                    mailbox->events.init_signal_cur, init_raising_edge, init_falling_edge) ;
#elif defined(QBUS)
			// QBUS: INIT is 10us pulse (not a state)
			line_INIT = true;
			worker_init_event();
			line_INIT = false;
			worker_init_event();
			DEBUG("EVENT_INIT") ;
			EVENT_ACK(*mailbox, init); // PRU releases DMR line now, CPU continues, event may re-raise and change mailbox now
#endif
            }
            if (!EVENT_IS_ACKED(*mailbox, power)) {
#if defined(UNIBUS)
                bool	dclo_event = mailbox->events.power_signals_cur & INITIALIZATIONSIGNAL_DCLO ;
                bool aclo_event = mailbox->events.power_signals_cur & INITIALIZATIONSIGNAL_ACLO ;
#elif defined(QBUS)
                // power states encoded with UNIBUS ACLO/DCLO
                bool	dclo_event = ! (mailbox->events.power_signals_cur & INITIALIZATIONSIGNAL_DCOK) ;
                bool aclo_event = ! (mailbox->events.power_signals_cur & INITIALIZATIONSIGNAL_POK) ;
#endif
                any_event = true;
                if (dclo_event) {
                    if (!line_DCLO)
                        dclo_edge = SIGNAL_EDGE_RAISING ;
                    line_DCLO = true;
                } else {
                    if (line_DCLO)
                        dclo_edge = SIGNAL_EDGE_FALLING ;
                    line_DCLO = false;
                }
                if (aclo_event) {
                    if (!line_ACLO)
                        aclo_edge =  SIGNAL_EDGE_RAISING  ;
                    line_ACLO = true;
                } else {
                    if (line_ACLO)
                        aclo_edge = SIGNAL_EDGE_FALLING;
                    line_ACLO = false;
                }
                DEBUG(
                    "EVENT_POWER: power_signals_prev=0x%x, power_signals_cur=0x%x, aclo_edge=%d, dclo_edge=%d",
                    mailbox->events.power_signals_prev, mailbox->events.power_signals_cur, (int)aclo_edge, (int)dclo_edge) ;
                if (aclo_edge != SIGNAL_EDGE_NONE || dclo_edge != SIGNAL_EDGE_NONE)
                    worker_power_event(aclo_edge, dclo_edge) ;
                EVENT_ACK(*mailbox, power); // PRU may re-raise and change mailbox now

            }

			// QBUS: INIT is a short pulse, PDP11 may execute many opcodes after RESET until
			// we receive event INIT and execute device initalization.
			// PDP11 CPU state is out of sync with device state in that time, but CPU sees device
			// only via SSYN/RPLY halted deviceregister accesses. So make sure pending INIT are processed before register access
            if (!EVENT_IS_ACKED(*mailbox, deviceregister) && EVENT_IS_ACKED(*mailbox, init)) {
                any_event = true;

                // DATI/DATO
                // DEBUG("EVENT_DEVICEREGISTER:  control=%d, addr=%06o", (int)mailbox->events.unibus_control, mailbox->events.addr);
                worker_deviceregister_event();
                // ARM2PRU opcodes raised by device logic are processed in midst of bus cycle
                EVENT_ACK(*mailbox, deviceregister); // PRU continues bus cycle with SSYN now
            }

            if (!EVENT_IS_ACKED(*mailbox, dma) && !mailbox->dma.cpu_access) {
                // not called for CPU DATI/DATO

                any_event = true;
                pthread_mutex_lock(&requests_mutex);
                worker_device_dma_chunk_complete_event();

                pthread_mutex_unlock(&requests_mutex);
                // PRU may have set again event_dma again, if this is called before EVENT signal??
                // call this only on signal, not on timeout!

                // this may clear reraised PRU event flag!
                EVENT_ACK(*mailbox, dma); // PRU may re-raise and change mailbox now
            }

            // 4 events for each BG4,5,6,7
            for (unsigned level_index = 0; level_index < 4; level_index++) {
                if (!EVENT_IS_ACKED(*mailbox, intr_master[level_index])) {
                    // Device INTR was transmitted. INTRs are granted unpredictable by Arbitrator
                    any_event = true;
                    // INTR of which level? the .active rquest of the"
                    pthread_mutex_lock(&requests_mutex);
                    worker_intr_complete_event(level_index);
                    pthread_mutex_unlock(&requests_mutex);
                    EVENT_ACK(*mailbox, intr_master[level_index]); // PRU may re-raise and change mailbox now
                }
            }

            if (!EVENT_IS_ACKED(*mailbox, intr_slave)) {
                // If CPU emulation enabled: a device INTR was detected on bus,
                assert(registered_cpu); // if INTR events are enabled, cpu must be instantiated
                // see register_device()
                registered_cpu->on_interrupt(mailbox->events.intr_slave.vector);
                // clear SSYN, INTR cycle completes
                EVENT_ACK(*mailbox, intr_slave);
                // mailbox->arbitrator.cpu_priority_level now CPU_PRIORITY_LEVEL_FETCHING
                // BG grants blocked

                // PRU may re-raise and change mailbox now
            }
#if defined(UNIBUS)
            if (init_raising_edge) { // INIT deasserted -> negated. DATI/DATO cycle only possible before that.
                // QBUS: bus blocked by DMR until EVENT_ACK for raising edge
                worker_init_event();
				EVENT_ACK(*mailbox, init);
			}
#endif			
        }
        // Signal to PRU: continue QBUS/UNIBUS cycles now with SSYN negated
    }
}

// debugging: print PRU sharead regsster map
void qunibusadapter_c::print_pru_iopage_register_map() {
    unsigned i;
    uint32_t addr;
    unsigned register_handle;
    unsigned device_handle;
    volatile pru_iopage_register_t *shared_reg;

// registers by address
    printf("Device registers by IO page address:\n");
    for (i = 0; i < 0x1000; i++) {
        // scan all addresses in IO page
        addr =  qunibus->iopage_start_addr + 2 * i;
        register_handle = IOPAGE_REGISTER_ENTRY(*pru_iopage_registers, addr);
        if (register_handle) {
            shared_reg = &(pru_iopage_registers->registers[register_handle]);
            printf("%s=reg[%2u] ", qunibus->addr2text(addr), register_handle);
            printf("cur val=%06o, writable=%06o. ", shared_reg->value,
                   shared_reg->writable_bits);
            // "passive" registers are not linked to controllers.
            // better use MSB of event_controller_handle to mark "active"?
            if (shared_reg->event_device_handle == 0)
                printf("passive reg (not linked to device).\n");
            else {
                printf("active reg linked to device #%u.reg[%2u]\n",
                       shared_reg->event_device_handle, shared_reg->event_device_register_idx);
            }

        }
    }

    // dump known devices
    printf("Registered devices by handle:\n");
    for (device_handle = 0; device_handle <= MAX_DEVICE_HANDLE; device_handle++)
        if (devices[device_handle] != NULL) {
            qunibusdevice_c *dev = devices[device_handle];
            printf("Device #%u \"%s\" @%s: %u registers\n", device_handle,
                   dev->name.value.c_str(), qunibus->addr2text(dev->base_addr.value), dev->register_count);
            assert(dev->register_count <= MAX_IOPAGE_REGISTERS_PER_DEVICE);
            for (i = 0; i < dev->register_count; i++) {
                qunibusdevice_register_t *device_reg = &(dev->registers[i]);
                char s_active[80];
                if (device_reg->active_on_dati || device_reg->active_on_dato) {
                    strcpy(s_active, "active ");
                    if (device_reg->active_on_dati)
                        strcat(s_active, "dati ");
                    if (device_reg->active_on_dato)
                        strcat(s_active, "dato");
                } else
                    strcpy(s_active, "passive");
                printf("  Reg[%2u]@%s: %s, resetval=%06o, writable=%06o.\n", i,
                       qunibus->addr2text(dev->base_addr.value + 2 * i), s_active,
                       (unsigned) device_reg->reset_value,
                       (unsigned) device_reg->writable_bits);
            }
        }
}

// diag: access to internal state of DMA and interupt request handling
mailbox_t mailbox_snapshot;

// reset measurements and timeouts
void qunibusadapter_c::debug_init() {
    // count events both on ARM and PRU, must be same!
}

// look into data strucures
void qunibusadapter_c::debug_snapshot(void) {
    // copy PRU mailbox state to space visible in Debugger (why necessary?)
    memcpy(&mailbox_snapshot, (void *) mailbox, sizeof(mailbox_t));
    //mailbox_snapshot = *(mailbox_t *)mailbox;
    break_here();			// pos for breakpoint

}

