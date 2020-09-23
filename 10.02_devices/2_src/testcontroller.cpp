/* testcontroller.cpp: sample UNIBUS controller with selftest logic

 Copyright (c) 2018-2019, Joerg Hoppe
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

 23-jul-2019  JH      added interrupt and DMA functions
 12-nov-2018  JH      entered beta phase

 "Tester" is  a device to test the event, stress INTR and DMA,  and
 implements 32 registers at start of IOpage

 Controller registers:
 ------------------------
 32 registers @ 0760200.. 0760276
 all registers are marked as "active":
 DATI and DATO are routed via events into the controller logic
 UNIBUS is stopped with long SSYN

 +0 = CSR: write command, read = status

 other registers have no functions, are simple memory cells.

 Backplane Slots: 10,11,12

 DMA
 ---
 has 2 DMA channels on different priority_slots
 Used for priroity test of parallel DMA req with different slot priority

 INTR
 ---
 4 x 3 interrupts (BR4,5,6,7 at slot 10,11,12)
 To test slot priority and level priroity
 raise all simulataneously with CPU level = 7
 -> no INTR triggered
 lower CPU lvel to 6 -> 2 INTR in increasing slot priority triggered




 Test #1 DMA priority test
 ------------------------
 Write of 1 into CSR triggers test
 First a long 1K DMA "A" with lower slot priority is started (DEPOSIT)
 2nd a DMA "B" with higher slot priority is started
 After some A-chunks, B get priorized and is completed earlier, despit started later.
 Verify: At mem start, "B" values re found (B later),
 at mem end "A" values are found (runs later)




 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
//#include <iostream>

#include "utils.hpp"
#include "logger.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"	// definition of class device_c
#include "testcontroller.hpp"

testcontroller_c::testcontroller_c() :
    qunibusdevice_c()  // super class constructor
{
    unsigned i;

    // static config
    name.value = "Test controller";
    type_name.value = "testcontroller_c";
    log_label = "tc";

    // mem at 160000: RT11 crashes?
    set_default_bus_params(0760200, 16, 0, 0); // base addr, priority_slot, intr-vector, intr level
    //

    register_count = 32; // up to 760200 .. 760276

    // CSR
    CSR = &(this->registers[0]);
    strcpy(CSR->name, "CSR");
    CSR->active_on_dati = true; // controller state change on read
    CSR->active_on_dato = true; // writing changes controller state
    CSR->reset_value = 0;
    CSR->writable_bits = 0xffff;  // all registers are memory cells

    // Other registers are "active": receive "on_after_register_access"
    for (i = 1; i < this->register_count; i++) {
        qunibusdevice_register_t *reg = &(this->registers[i]);
        sprintf(reg->name, "reg%02o", i); // name is register offset: "reg07"
        reg->active_on_dati = true; // controller state change on read
        reg->active_on_dato = true; // writing changes controller state
        reg->reset_value = 0;
        reg->writable_bits = 0xffff;  // all registers are memory cells
    }

    // create DMA requests.
    for (unsigned i = 0; i < dma_channel_count; i++) {
        dma_request_c *dmareq = dma_channel_request[i] = new dma_request_c(this);
        // lowest index = highest slot priority
        dmareq->set_priority_slot(i + 15);
        dma_channel_buffer[i] = new memoryimage_c();

    }
    // create INTR requests.
    for (unsigned slot = 1; slot < PRIORITY_SLOT_COUNT; slot++)
        for (unsigned level_index = 0; level_index < 4; level_index++) {
            intr_request_c *intreq = intr_request[slot][level_index] = new intr_request_c(this);
            intreq->set_priority_slot(slot);
            intreq->set_level(level_index+4);
            // "vector" uninitialized, must be set on use!
        }

    // dynamic state
    access_count.value = 0;

}

testcontroller_c::~testcontroller_c() {
    for (unsigned i = 0; i < dma_channel_count; i++) {
        delete dma_channel_request[i];
        delete dma_channel_buffer[i];
    }

    for (unsigned slot = 1; slot < PRIORITY_SLOT_COUNT; slot++)
        for (unsigned level_index = 0; level_index < 4; level_index++)
            delete intr_request[slot][level_index];
}

bool testcontroller_c::on_param_changed(parameter_c *param) {
    // no own parameter or "enable" logic
    return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}

// process DATI/DATO access to one of my "active" registers
// !! called asynchronuously by PRU, with SSYN asserted and blocking UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void testcontroller_c::on_after_register_access(qunibusdevice_register_t *device_reg,
        uint8_t unibus_control) {

    // emulate a plain memory cell: written values can be read unchanged
    if (unibus_control == QUNIBUS_CYCLE_DATI) {
    }

    if (unibus_control == QUNIBUS_CYCLE_DATO)
        set_register_dati_value(device_reg, device_reg->active_dato_flipflops, __func__);
    if (device_reg == CSR) {
        /// CSR has been written: signal worker()
        pthread_cond_signal(&on_after_register_access_cond);
        // worker now executes CSR command and clears datao lfipflops
    }
    // this is also called for some DATIs, no action anyhow.

    access_count.value++;
    // DEBUG writes to disk & console ... measured delay up to 30ms !
    //DEBUG(LL_DEBUG, LC_demo_regs, "[%6u] reg +%d @ %06o %s", accesscount, (int ) device_reg->index,
    //		device_reg->addr, qunibus_c::control2text(unibus_control));
}

// after UNIBUS install, device is reset by DCLO cycle
void testcontroller_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
    UNUSED(aclo_edge) ;
    UNUSED(dclo_edge) ;
}

// UNIBUS INIT: clear all registers
void testcontroller_c::on_init_changed(void) {
    // write all registers to "reset-values"
    if (init_asserted) {
        reset_unibus_registers();
        INFO("testcontroller_c::on_init()");
    }
}

// background worker.
// Just print a heart beat
void testcontroller_c::worker(unsigned instance) {
    UNUSED(instance); // only one
    timeout_c timeout;
    assert(!pthread_mutex_lock(&on_after_register_access_mutex));

    // set prio to RT, but less than unibus_adapter
    worker_init_realtime_priority(rt_device);

    while (!workers_terminate) {

        int res = pthread_cond_wait(&on_after_register_access_cond,
                                    &on_after_register_access_mutex);
        if (res != 0) {
            ERROR("testcontroller_c::worker() pthread_cond_wait = %d = %s>", res,
                  strerror(res));
            continue;
        }
        // execute command in CSR,
        uint16_t cmd = CSR->active_dato_flipflops;
        // mark as processed
        CSR->active_dato_flipflops = 0;
        set_register_dati_value(CSR, 0, __func__); // no status
        switch (cmd) {
        case 1:
            test_dma_priority();
            break;

        }

        timeout.wait_ms(1000);
        cout << ".";
    }

    assert(!pthread_mutex_unlock(&on_after_register_access_mutex));
}

void testcontroller_c::test_dma_priority() {
}

