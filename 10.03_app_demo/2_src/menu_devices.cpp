/* menu_devices.cpp: user sub menu

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

 16-Nov-2018  JH      created
 16-Oct-2022  MR      Copied the "m lt file" option from other menu to here
 27-Feb-2023  JD/JH   RS11/RF11 new. KE11 EAE for UNIBUS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <linux/limits.h>

#include "logger.hpp"
#include "inputline.hpp"
#include "mcout.h"

#include "application.hpp" // own

#include "pru.hpp"
#include "gpios.hpp"
#include "buslatches.hpp"
#include "mailbox.h"
#include "iopageregister.h"
#include "parameter.hpp"
#include "qunibus.h"
#include "memoryimage.hpp"

#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"

#include "storagedrive.hpp"
#include "panel.hpp"
#include "blinkenbone.hpp"
#include "demo_io.hpp"
#include "testcontroller.hpp"
#include "rf11.hpp"
#include "rl11.hpp"
#include "rk11.hpp"
#include "rx11211.hpp"
#include "uda.hpp"
#include "dl11w.hpp"
#include "ke11.hpp"
#if defined(UNIBUS)
#include "m9312.hpp"
#endif
#include "cpu.hpp"

/*** handle loading of memory content  from macro-11 listing ***/
static char memory_filename[PATH_MAX + 1];

// entry_label is program start, typically "start"
// format: 0 = macrop11, 1 = papertape
static void load_memory(memory_fileformat_t format, char *fname, const char *entry_label)
{
    codelabel_map_c codelabels ;
    uint32_t firstaddr, lastaddr;
    uint32_t entry_address = MEMORY_ADDRESS_INVALID ;

    bool load_ok;
    bool timeout;
    switch (format) {
    case fileformat_macro11_listing:
        load_ok = membuffer->load_macro11_listing(fname, &codelabels);
        if (codelabels.is_defined(entry_label))
            entry_address = codelabels.get_address(entry_label) ;
        break;
    case fileformat_papertape:
        load_ok = membuffer->load_papertape(fname, &codelabels);
        if (codelabels.size() > 0) // only one possbile label: "entry"
            entry_address = codelabels.begin()->second;
        break;
    case fileformat_addr_value_text:
        load_ok = membuffer->load_addr_value_text(fname);
        break;
    default:
        load_ok = false;
    }
    if (load_ok) {
        strcpy(memory_filename, fname);
        membuffer->get_addr_range(&firstaddr, &lastaddr);
        printf(
            "Loaded MACRO-11 listing from file \"%s\" into memory: %d words from %06o to %06o.\n",
            fname, membuffer->get_word_count(), firstaddr, lastaddr);
        if (entry_label == NULL)
            printf("  No entry address label.\n");
        else if (entry_address != MEMORY_ADDRESS_INVALID)
            printf("  Entry address at \"%s\" label is %06o.\n", entry_label,
                   entry_address);
        else
            printf("  No entry address at \"%s\" label is %06o.\n", entry_label,
                   entry_address);
        qunibus->mem_write(membuffer->data.words, firstaddr, lastaddr,
                           &timeout);
        if (timeout)
            printf("  Error writing " QUNIBUS_NAME " memory\n");
    }
}

static void print_device(device_c *device)
{
    qunibusdevice_c *ubdevice = dynamic_cast<qunibusdevice_c *>(device);
    if (ubdevice)
        printf("- %-12s  Type %s, %s.\n", ubdevice->name.value.c_str(),
               ubdevice->type_name.value.c_str(), ubdevice->get_qunibus_resource_info());
    else
        printf("- %-12s  Type %s.\n", device->name.value.c_str(),
               device->type_name.value.c_str());
}

void application_c::menu_devices(const char *menu_code, bool with_emulated_CPU)
{
    /** list of usable devices ***/
    bool with_storage_file_test = false;

    bool ready = false;
    bool show_help = true ;
    bool memory_emulated = false;
    device_c *cur_device = NULL;
    qunibusdevice_c *unibuscontroller = NULL;
    unsigned n_fields;
    char *s_choice;
    char s_opcode[256], s_param[3][256];

    strcpy(memory_filename, "");

//	iopageregisters_init();
    // QBUS/UNIBUS activity
    // assert(qunibus->arbitrator_client) ; // External Bus Arbitrator required
    hardware_startup(pru_c::PRUCODE_EMULATION);
    // now PRU executing QBUS/UNIBUS master/slave code, physical PDP-11 CPU as arbitrator required.
    gpios->drive_activity_led.enabled = !gpios->leds_for_debug ;
    buslatches.output_enable(true);

    // devices need physical or emulated CPU Arbitrator
    // to answer BR and NPR requests.
    if (with_emulated_CPU)
        // not yet active, switches to CLIENT when emulated CPU started
        qunibus->set_arbitrator_active(false) ;
    else
        qunibus->set_arbitrator_active(true) ;

    // without PDP-11 CPU no INIT after power ON was generated.
    // Devices may trash the bus lines.
    qunibus->init();

    qunibusadapter->enabled.set(true);

    // memory mapped blinkenbone panels
    blinkenbone_c *blinkenbone = new blinkenbone_c();

    // 2 demo controller
    cur_device = NULL;
    demo_io_c *demo_io = new demo_io_c();


    // uses all slot resource, can onyl run alone
    //testcontroller_c *test_controller = new testcontroller_c();

#if defined(UNIBUS)
    cpu_c *cpu = NULL;
#endif
    // create RF11 + RS11 drive
    rf11_c *RF11 = new rf11_c();

    // create RL11 +  also 4 RL01/02 drives
#if defined(UNIBUS)
    RL11_c *RL11 = new RL11_c();
#elif defined(QBUS)
    RLV12_c *RL11 = new RLV12_c();
#endif
    paneldriver->reset(); // reset I2C, restart worker()
    // create RK11 + drives
#if defined(UNIBUS)
    rk11_c *RK11 = new rk11_c();
#elif defined(QBUS)
    rkv11_c *RK11 = new rkv11_c();
#endif

    // Create UDA50
    uda_c *UDA50 = new uda_c();
    // Create 2 SLUs + LTC
    slu_c *DL11 = new slu_c();
    slu_c *DL11b = new slu_c();
    // 2nd UART different parameters, default for TU58 interface
    // !!! disable Linux usage, agetty !!!
    DL11b->name.value = "DL11b";
    log_label = "slub";
    DL11b->priority_slot.value = DL11->priority_slot.value +1 ; // next to 1st uart
    DL11b->base_addr.value = 0176500 ; //  AK6DN boot loader listing
    DL11b->intr_vector.value = 0300 ;  // PDP-11/44 doc
    DL11b->intr_level.value = 4 ; // PDP-11/44 doc
    DL11b->serialport.value = "ttyS1" ; // well, cross-over 2nd UART == UART1 on board
    DL11b->baudrate.value = 38400 ;
    DL11b->mode.value = "8N1";
    DL11b->error_bits_enable.value = false ; // M7856 SW4-7 ?
    DL11b->break_enable.value = true ; // TU58 needs BREAK

    // to inject characters into DL11 receiver
    // only 1st SLU, use this for console
    std::stringstream dl11_rcv_stream(std::ios::app | std::ios::in | std::ios::out);
    DL11->rs232adapter.stream_rcv = &dl11_rcv_stream;
    DL11->rs232adapter.stream_xmt = NULL; // do not echo output to stdout
    DL11->rs232adapter.baudrate = DL11->baudrate.value; // limit speed of injected chars

    ltc_c *LTC = new ltc_c();

#if defined(UNIBUS)
    RX11_c *RX11 = new RX11_c() ;
    RX211_c *RX211 = new RX211_c() ;
#elif defined(QBUS)
    RXV11_c *RX11 = new RXV11_c() ;
    RXV21_c *RX211 = new RXV21_c() ;
#endif
    //	//demo_regs.install();
    //	//demo_regs.worker_start();



#if defined(UNIBUS)
    m9312_c *m9312 = new m9312_c();
    ke11_c *KE11A = new ke11_c();

    if (with_emulated_CPU) {
        cpu = new cpu_c();
        cpu->enabled.set(true);
    }
#endif

    if (with_storage_file_test) {
        const char *testfname = "/tmp/storagedrive_selftest.bin";
        remove(testfname);
        storagedrive_selftest_c dut(testfname, /* block_size*/1024, /* block_count */137);
        dut.test();
    }

    // now devices are "Plugged in". Reset PDP-11.
//	qunibus->probe_grant_continuity(true);

    while (!ready) {
        // no menu display when reading script
        if (show_help && ! script_active()) {
            show_help = false; // only once
            printf("\n");
            printf("*** Test of device parameter interface and states.\n");
            print_arbitration_info("    ");
            if (cur_device) {
                printf("    Current device is \"%s\"\n", cur_device->name.value.c_str());
                if (unibuscontroller)
                    printf("    " QUNIBUS_NAME " controller base address = %06o\n",
                           unibuscontroller->base_addr.value);
            } else
                printf("    No current device selected\n");
            if (memory_emulated) {
                printf("    " QUNIBUS_NAME " memory emulated from %s to %s.\n",
                       qunibus->addr2text(emulated_memory_start_addr), qunibus->addr2text(emulated_memory_end_addr));
            } else
                printf("    NO " QUNIBUS_NAME " memory installed ... device test limited!\n");
            printf("\n");
            printf("m i [<endaddr>]      Install (emulate) max " QUNIBUS_NAME " memory, or up to including even <endaddr>\n");
            printf("m f [word]           Fill " QUNIBUS_NAME " memory (with 0 or other octal value)\n");
            printf("m d                  Dump " QUNIBUS_NAME " memory to disk\n");
            printf(
                "m ll <filename>      Load memory content from MACRO-11 listing file (boot loader)\n");
            if (strlen(memory_filename))
                printf(
                    "m ll                 Reload last memory content from file \"%s\"\n",
                    memory_filename);
            printf("m lp <filename>      Load memory content from absolute papertape image\n");
            printf("m lp                 Reload last memory content from file \"%s\"\n",
                   memory_filename);
            printf("m lt <filename>      Load memory content from address-value text file\n");
            printf("m lt                 Reload last memory content from file \"%s\"\n", memory_filename);
            printf("ld                   List all defined devices\n");
            printf("en <dev>             Enable a device\n");
            printf("dis <dev>            Disable device\n");
            printf("sd <dev>             Select \"current device\"\n");

            if (cur_device) {
                printf("p <param> <val>      Set parameter value of current device\n");
                printf("p <param>            Get parameter value of current device\n");
                printf("p panel              Force parameter update from panel\n");
                printf("p                    Show all parameter of current device\n");
            }
            if (unibuscontroller) {
                printf("d <regname> <val>    Deposit octal value into named device register\n");
                printf("e <regname>          Examine single device register (regno decimal)\n");
                printf("e                    Examine all device registers\n");
            }
            printf("e <addr>             Examine octal " QUNIBUS_NAME " address.\n");
            printf("d <addr> <val>       Deposit octal val into " QUNIBUS_NAME " address.\n");
            if (DL11->enabled.value) {
                printf(
                    "dl11 rcv [<wait_ms>] <string>   inject characters as if DL11 received them.\n");
                printf(
                    "                     Before output there's an optional pause of <wait_ms> milliseconds.\n");
                printf(
                    "                     <string> uses C-escapes: \"\\r\"= CR, \040 = space, etc.\n");
                printf(
                    "dl11 wait <timeout_ms> <string>	wait time until DL11 was ordered to transmit <string>.\n");
                printf("                     On timeout, script execution is terminated.\n");
            }
            printf("dbg c|s|f            Debug log: Clear, Show on console, dump to File.\n");
            printf("                       (file = %s)\n", logger->default_filepath.c_str());
            printf("init                 Pulse " QUNIBUS_NAME " INIT\n");
#if defined(UNIBUS)
            printf("pwr                  Simulate UNIBUS power cycle (ACLO/DCLO)\n");
#elif defined(QBUS)
            printf("h <1|0>              Set/release QBUS HALT, like front panel toggle switch\n");
            printf("pwr                  Simulate QBUS power cycle (DCOK/POK) like front panel RESTART\n");
#endif
            printf("q                    Quit\n");
        }
        s_choice = getchoice(menu_code);

        printf("\n");
        try {
            n_fields = sscanf(s_choice, "%s %s %s %s", s_opcode, s_param[0], s_param[1],
                              s_param[2]);
            if (!strcasecmp(s_opcode, "q")) {
                ready = true;
            } else if (!strcasecmp(s_opcode, "init")) {
                qunibus->init();
            } else if (!strcasecmp(s_opcode, "pwr")) {
                qunibus->powercycle() ;
#if defined(QBUS)
            } else if (!strcasecmp(s_opcode, "h") && n_fields == 2) {
                uint16_t active ;
                qunibus->parse_word(s_param[0], &active) ;
                qunibus->set_halt(active) ;
#endif
            } else if (!strcasecmp(s_opcode, "dbg") && n_fields == 2) {
                if (!strcasecmp(s_param[0], "c")) {
                    logger->clear();
                    qunibusadapter->debug_init(); // special diagnostics
                    printf("Debug log cleared.\n");
                } else if (!strcasecmp(s_param[0], "s")) {
                    qunibusadapter->debug_snapshot(); // special diagnostics
                    logger->dump();
                } else if (!strcasecmp(s_param[0], "f")) {
                    logger->dump(logger->default_filepath);
                }
            } else if (!strcasecmp(s_opcode, "m") && n_fields >= 2
                       && !strcasecmp(s_param[0], "i")) {
                // install (emulate) max QBUS/UNIBUS memory or limited by <endaddr>
                uint32_t endaddr = 0;
                if (n_fields == 3)
                    qunibus->parse_addr(s_param[1], &endaddr);
                memory_emulated = emulate_memory(endaddr);
                show_help = true; // menu struct changed
            } else if (!strcasecmp(s_opcode, "m") && n_fields >= 2
                       && !strcasecmp(s_param[0], "f")) {
                //  clear QBUS/UNIBUS memory
                bool timeout;
                uint16_t fillword = 0;
                if (n_fields == 3)
                    qunibus->parse_word(s_param[1], &fillword);
                membuffer->set_addr_range(emulated_memory_start_addr, emulated_memory_end_addr);
                membuffer->fill(fillword);
                // write buffer-> QBUS/UNIBUS
                printf("Fill memory with %06o, writing " QUNIBUS_NAME " memory[%s:%s]\n", fillword,
                       qunibus->addr2text(emulated_memory_start_addr), qunibus->addr2text(emulated_memory_end_addr));
                qunibus->mem_write(membuffer->data.words,
                                   emulated_memory_start_addr, emulated_memory_end_addr, &timeout);
                if (timeout)
                    printf("Error writing " QUNIBUS_NAME " memory!\n");
            } else if (!strcasecmp(s_opcode, "m") && n_fields == 2
                       && !strcasecmp(s_param[0], "d")) {
                // dump QBUS/UNIBUS memory to disk
                const char * filename = "memory.dump";
                bool timeout;
                // 1. read QBUS/UNIBUS memory
                uint32_t end_addr = qunibus->test_sizer() - 2;
                printf("Reading " QUNIBUS_NAME " memory[0:%s] with DMA\n", qunibus->addr2text(end_addr));
                //  clear memory buffer, to be sure content changed
                membuffer->set_addr_range(0, end_addr);
                membuffer->fill(0);

                qunibus->mem_read(membuffer->data.words, 0, end_addr,
                                  &timeout);
                if (timeout)
                    printf("Error reading " QUNIBUS_NAME " memory!\n");
                else {
                    // 1. read QBUS/UNIBUS memory
                    printf("Saving to file \"%s\"\n", filename);
                    membuffer->save_binary(filename, end_addr + 2);
                }
            } else if (!strcasecmp(s_opcode, "m") && n_fields == 3
                       && !strcasecmp(s_param[0], "ll")) {
                // m ll <filename>
                load_memory(fileformat_macro11_listing, s_param[1], "start");
            } else if (!strcasecmp(s_opcode, "m") && n_fields == 2
                       && !strcasecmp(s_param[0], "ll") && strlen(memory_filename)) {
                // m ll
                load_memory(fileformat_macro11_listing, memory_filename,
                            "start");
            } else if (!strcasecmp(s_opcode, "m") && n_fields == 3
                       && !strcasecmp(s_param[0], "lp")) {
                // m lp <filename>
                load_memory(fileformat_papertape, s_param[1], NULL);
            } else if (!strcasecmp(s_opcode, "m") && n_fields == 2
                       && !strcasecmp(s_param[0], "lp") && strlen(memory_filename)) {
                // m lp
                load_memory(fileformat_papertape, memory_filename, NULL);
            } else if (!strcasecmp(s_opcode, "m") && n_fields == 3
                       && !strcasecmp(s_param[0], "lt")) {
                // m lt <fiilename>
                load_memory(fileformat_addr_value_text, s_param[1], NULL);
            } else if (!strcasecmp(s_opcode, "m") && n_fields == 2
                       && !strcasecmp(s_param[0], "lt") && strlen(memory_filename)) {
                // m lt
                load_memory(fileformat_addr_value_text, memory_filename, NULL);
            } else if (!strcasecmp(s_opcode, "ld") && n_fields == 1) {
                unsigned n;
                std::list<device_c *>::iterator it;
                for (n = 0, it = device_c::mydevices.begin(); it != device_c::mydevices.end();
                        ++it)
                    if ((*it)->enabled.value) {
                        if (n == 0)
                            std::cout << "Enabled devices:\n";
                        n++;
                        print_device(*it);
                    }
                if (n == 0)
                    std::cout << "No enabled devices.\n";

                for (n = 0, it = device_c::mydevices.begin(); it != device_c::mydevices.end();
                        ++it)
                    if (!(*it)->enabled.value) {
                        if (n == 0)
                            std::cout << "Disabled devices:\n";
                        n++;
                        print_device(*it);
                    }
                if (n == 0)
                    std::cout << "No disabled devices.\n";
            } else if (!strcasecmp(s_opcode, "en") && n_fields == 2) {
                device_c *dev = device_c::find_by_name(s_param[0]);
                if (!dev) {
                    std::cout << "Device \"" << s_param[0] << "\" not found.\n";
                    show_help = true;
                } else
                    dev->enabled.set(true);
            } else if (!strcasecmp(s_opcode, "dis") && n_fields == 2) {
                device_c *dev = device_c::find_by_name(s_param[0]);
                if (!dev) {
                    std::cout << "Device \"" << s_param[0] << "\" not found.\n";
                    show_help = true;
                } else
                    dev->enabled.set(false);
            } else if (!strcasecmp(s_opcode, "sd") && n_fields == 2) {
                cur_device = device_c::find_by_name(s_param[0]);

                if (!cur_device) {
                    std::cout << "Device \"" << s_param[0] << "\" not found.\n";
                    show_help = true;
                } else {
                    printf("Current device is \"%s\"\n", cur_device->name.value.c_str());
                    // find base address of assoiated QBUS/UNIBUS unibuscontroller
                    if (cur_device != NULL && dynamic_cast<qunibusdevice_c *>(cur_device))
                        unibuscontroller = dynamic_cast<qunibusdevice_c *>(cur_device);
                    else if (cur_device->parent != NULL
                             && dynamic_cast<qunibusdevice_c *>(cur_device->parent)) {
                        unibuscontroller = dynamic_cast<qunibusdevice_c *>(cur_device->parent);
                        printf("Controller base address = %s\n",
                               qunibus->addr2text(unibuscontroller->base_addr.value));
                    } else
                        unibuscontroller = NULL; // no unibuscontroller found
                    show_help = true;
                }
            } else if (cur_device && !strcasecmp(s_opcode, "p") && n_fields == 1) {
                std::cout << "Parameters of device " << cur_device->name.value << ":\n";
                print_params(cur_device, NULL);
            } else if (cur_device && !strcasecmp(s_opcode, "p") && n_fields == 2
                       && !strcasecmp(s_param[0], "panel")) {
                paneldriver->refresh_params(cur_device);
                // RL11.refresh_params_from_panel(); // all 4 drives
            } else if (cur_device && !strcasecmp(s_opcode, "p") && n_fields == 2) {
                // show selected
                std::string pn(s_param[0]);
                parameter_c *p = cur_device->param_by_name(pn);
                if (p == NULL)
                    std::cout << "Device \"" << cur_device->name.value << "\" has no parameter \""
                              << pn << "\".\n";
                else {
                    // string parameter set to "" ?
                    if (typeid(*p) == typeid(parameter_string_c))
                        p->parse("");
                    print_params(cur_device, p);
                }
            } else if (cur_device && !strcasecmp(s_opcode, "p") && n_fields == 3) {
                std::string pn(s_param[0]);
                parameter_c *p = cur_device->param_by_name(pn);
                if (p == NULL)
                    std::cout << "Device \"" << cur_device->name.value << "\" has no parameter \""
                              << pn << "\".\n";
                else {
                    std::string sval(s_param[1]);
                    p->parse(sval);
                    print_params(cur_device, p);
                }
            } else if (!strcasecmp(s_opcode, "d") && n_fields == 3) {
                uint32_t addr;
                uint16_t wordbuffer;
                qunibusdevice_register_t *reg = NULL;
                if (unibuscontroller)
                    reg = unibuscontroller->register_by_name(s_param[0]);
                if (reg) // register name given
                    addr = reg->addr;
                else
                    // interpret as address
                    qunibus->parse_addr(s_param[0], &addr);

                qunibus->parse_word(s_param[1], &wordbuffer);
                bool timeout = !qunibus->dma(true, QUNIBUS_CYCLE_DATO, addr,
                                             &wordbuffer, 1);
                if (reg) {
                    assert(
                        reg
                        == unibuscontroller->register_by_unibus_address(
                            qunibus->dma_request->qunibus_end_addr));
                    printf("DEPOSIT reg #%d \"%s\" %s <- %06o\n", reg->index, reg->name,
                           qunibus->addr2text(reg->addr), wordbuffer);
                } else
                    printf("DEPOSIT %s <- %06o\n", qunibus->addr2text(addr), wordbuffer);
                if (timeout)
                    printf("Bus timeout at %s.\n", qunibus->addr2text(mailbox->dma.cur_addr));
            } else if (!strcasecmp(s_opcode, "e") && n_fields <= 2) {
                bool timeout = false;
                uint32_t addr;
                qunibusdevice_register_t *reg = NULL;
                if (n_fields == 2) { // single reg number or address given
                    uint16_t wordbuffer; // exam single word
                    if (unibuscontroller)
                        reg = unibuscontroller->register_by_name(s_param[0]);
                    if (reg)
                        addr = reg->addr;
                    else
                        qunibus->parse_addr(s_param[0], &addr); // interpret as 18 bit address
                    timeout = !qunibus->dma(true, QUNIBUS_CYCLE_DATI, addr,
                                            &wordbuffer, 1);
                    printf("EXAM %s -> %06o\n", qunibus->addr2text(addr), wordbuffer);
                } else if (n_fields == 1 && unibuscontroller) { // list all regs
                    uint16_t wordbuffer[MAX_IOPAGE_REGISTERS_PER_DEVICE];
                    addr = unibuscontroller->base_addr.value; // all device registers
                    unsigned wordcount = unibuscontroller->register_count;
                    if (wordcount) {
                        timeout = !qunibus->dma(true, QUNIBUS_CYCLE_DATI, addr, wordbuffer, wordcount);
                        for (unsigned i = 0; addr <= mailbox->dma.cur_addr; i++, addr += 2) {
                            reg = unibuscontroller->register_by_unibus_address(addr);
                            assert(reg);
                            printf("EXAM reg #%d %s %s -> %06o\n", reg->index, reg->name,
                                   qunibus->addr2text(reg->addr), wordbuffer[i]);
                        }
                    } else {
                        timeout = false;
                        printf("Device has no " QUNIBUS_NAME " registers.\n");
                    }
                } else {
                    // no device: no "display all"
                    show_help = true;
                }
                if (timeout)
                    printf("Bus timeout at %s.\n", qunibus->addr2text(mailbox->dma.cur_addr));
                // cur_addr now on last address in block
            } else if (DL11->enabled.value && !strcasecmp(s_opcode, "dl11")) {
                if ((n_fields == 3 || n_fields == 4) && !strcasecmp(s_param[0], "rcv")) {
                    // dl11 rcv [<wait_ms>] <string>
                    char buff[256];
                    unsigned wait_ms;
                    timeout_c timeout;
                    char *s;
                    if (n_fields == 3) {
                        wait_ms = 0;
                        s = s_param[1];
                    } else {
                        wait_ms = strtol(s_param[1], NULL, 10);
                        s = s_param[2];
                    }
                    if (!str_decode_escapes(buff, sizeof(buff), s)) {
                        printf("Error in escape sequences.\n");
                        inputline.init();
                        continue;
                    }
                    timeout.wait_ms(wait_ms);
                    // let DL11 produce chars in 'buff'
                    pthread_mutex_lock(&DL11->rs232adapter.mutex);
                    dl11_rcv_stream.clear();
                    dl11_rcv_stream.write(buff, strlen(buff)); // add endlessly to string
                    // dl11_rcv_stream.str(buff);
                    pthread_mutex_unlock(&DL11->rs232adapter.mutex);
//							printf("AAA %d\n", (int)dl11_rcv_stream.get()) ;
                } else if (n_fields == 4 && !strcasecmp(s_param[0], "wait")) {
                    // dl11 wait <timeout_ms> <string>
                    timeout_c timeout, timeout2;
                    unsigned ms = strtol(s_param[1], NULL, 10);
                    char buff[256];
                    if (!str_decode_escapes(buff, sizeof(buff), s_param[2])) {
                        printf("Error in escape sequences.\n");
                        inputline.init();
                        continue;
                    }
                    // while waiting echo to stdout, for diag
                    DL11->rs232adapter.stream_xmt = &std::cout;
                    DL11->rs232adapter.set_pattern(buff);
                    timeout.start_ms(ms);
                    while (!timeout.reached() && !DL11->rs232adapter.pattern_found)
                        timeout2.wait_ms(1);
                    DL11->rs232adapter.stream_xmt = NULL; // stop echo

                    if (!DL11->rs232adapter.pattern_found) {
                        printf(
                            "\nPDP-11 did not xmt \"%s\" over DL11 within %u ms, aborting script\n",
                            s_param[2], ms);
                        inputline.init();
                    }
                } else {
                    printf("Unknown DL11 command \"%s\"!\n", s_choice);
                    show_help = true;
                }
            } else {
                printf("Unknown command \"%s\"!\n", s_choice);
                show_help = true;
            }
        } catch (bad_parameter& e) {
            std::cout << "Error : " << e.what() << "\n";
        }
    } // ready

#if defined(UNIBUS)
    if (with_emulated_CPU) {
        cpu->enabled.set(false);
        delete cpu;
    }
    m9312->enabled.set(false) ;
    delete m9312 ;
    KE11A->enabled.set(false);
    delete KE11A;

#endif

    RX11->enabled.set(false) ;
    delete RX11;

    RX211->enabled.set(false) ;
    delete RX211;

    LTC->enabled.set(false);
    delete LTC;
    DL11b->enabled.set(false);
    delete DL11b;
    DL11->enabled.set(false);
    delete DL11;

    RF11->enabled.set(false);
    delete RF11;

    RL11->enabled.set(false);
    delete RL11;

    RK11->enabled.set(false);
    delete RK11;

    UDA50->enabled.set(false);
    delete UDA50;

    //test_controller->enabled.set(false);
    //delete test_controller;

    demo_io->enabled.set(false);
    delete demo_io;

    blinkenbone->enabled.set(false);
    delete blinkenbone;

    gpios->drive_activity_led.enabled = false ;

    qunibusadapter->enabled.set(false);

    buslatches.output_enable(false);
    hardware_shutdown(); // stop PRU
}

