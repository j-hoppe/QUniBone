/* gpios_q.cpp: QBone functions for ARM and PRU GPIOs

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


 21-may-2019  JH      added UNIBUS signals
 12-nov-2018  JH      entered beta phase
 */

#define _GPIOS_CPP_

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "mailbox.h"

#include "pru.hpp"
#include "utils.hpp"
#include "logsource.hpp"
#include "logger.hpp"
#include "timeout.hpp"

#include "gpios.hpp"

// see spruh73n.pdf
// address range of GPIO related registers. [spruh73n] page 181
#define GPIO_SIZE 	0x1000 // size of each GPIO register range
#define GPIO0_START_ADDR 0x44E07000
#define GPIO1_START_ADDR 0x4804C000
#define GPIO2_START_ADDR 0x481AC000
#define GPIO3_START_ADDR 0x481AE000 // offset in each GPIO register range
#define GPIO_OE_ADDROFFSET 0x134
#define GPIO_DATAIN_ADDROFFSET 0x138
#define GPIO_DATAOUT_ADDROFFSET 0x13c
#define GPIO_SETDATAOUT_ADDROFFSET 0x194
#define GPIO_CLEARDATAOUT_ADDROFFSET 0x190

gpios_c *gpios; // Singleton

gpios_c::gpios_c() {
    log_label = "GPIOS";

    cmdline_leds = 0 ; // is set before init()
    leds_for_debug = false ;

    memory_filedescriptor = open((char*) "/dev/mem", O_RDWR);
    if (!memory_filedescriptor)
        FATAL("Can not open /dev/mem");

}

/* fill the 4 gpio_banks with values and
 *	map addresses
 */
void gpios_c::bank_map_registers(unsigned bank_idx, unsigned unmapped_start_addr)
{
    gpio_bank_t *bank;

    assert(bank_idx < 4);

    bank = &(banks[bank_idx]);
    bank->gpios_in_use = 0;
    bank->registerrange_addr_unmapped = unmapped_start_addr; // info only
    INFO("GPIO%d registers at %X - %X (size = %X)", bank_idx, unmapped_start_addr,
         unmapped_start_addr + GPIO_SIZE - 1, GPIO_SIZE);
    bank->registerrange_start_addr = (uint8_t *) mmap(0, GPIO_SIZE, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, memory_filedescriptor, unmapped_start_addr);
    if (bank->registerrange_start_addr == MAP_FAILED)
        FATAL("Unable to map GPIO%d", bank_idx);

    bank->oe_addr = (uint32_t *) (bank->registerrange_start_addr + GPIO_OE_ADDROFFSET);
    bank->datain_addr = (uint32_t *) (bank->registerrange_start_addr + GPIO_DATAIN_ADDROFFSET);
    bank->dataout_addr =
        (uint32_t *) (bank->registerrange_start_addr + GPIO_DATAOUT_ADDROFFSET);
    bank->setdataout_addr = (uint32_t *) (bank->registerrange_start_addr
                                          + GPIO_SETDATAOUT_ADDROFFSET);
    bank->clrdataout_addr = (uint32_t *) (bank->registerrange_start_addr
                                          + GPIO_CLEARDATAOUT_ADDROFFSET);
}

gpio_config_t *gpios_c::config(const char *name, int direction, unsigned bank_idx,
                               unsigned pin_in_bank)
{
    gpio_config_t *result = (gpio_config_t *) malloc(sizeof(gpio_config_t));
    if (name)
        strcpy(result->name, name);
    else
        strcpy(result->name, "");
    if (strlen(result->name) > 0)
        result->internal = 0;
    else
        result->internal = 1;
    result->direction = direction;
    assert(bank_idx < 4);
    result->bank_idx = bank_idx;
    result->bank = &(banks[bank_idx]);
    result->bank->gpios_in_use++;
    result->pin_in_bank = pin_in_bank;
    result->linear_no = 32 * bank_idx + pin_in_bank;
    result->pin_in_bank_mask = 1 << pin_in_bank;

    return result;
}

// "export" a pin over the sys file system
// this is necessary for GPIO2&3, despite we operate memory mapped.
void gpios_c::export_pin(gpio_config_t *pin)
{
    char fname[256];
    FILE *f;
    struct stat statbuff;

    sprintf(fname, "/sys/class/gpio/export");
    f = fopen(fname, "w");
    if (!f)
        FATAL("Can not open %s", fname);
    fprintf(f, "%d\n", pin->linear_no);
    fclose(f);

    // verify: now there should appear the control directory for the pin
    sprintf(fname, "/sys/class/gpio/gpio%d", pin->linear_no);
    if (stat(fname, &statbuff) != 0 || !S_ISDIR(statbuff.st_mode))
        FATAL("Gpio control dir %s not generated", fname);

}

/* export the NON-PRU pins: */
void gpios_c::init()
{
    unsigned n;
    gpio_config_t *gpio;

    bank_map_registers(0, GPIO0_START_ADDR);
    bank_map_registers(1, GPIO1_START_ADDR);
    bank_map_registers(2, GPIO2_START_ADDR);
    bank_map_registers(3, GPIO3_START_ADDR);

    // fill pin database
    n = 0;
    pins[n++] = led[0] = config("LED0", DIR_OUTPUT, 1, 0);
    pins[n++] = led[1] = config("LED1", DIR_OUTPUT, 1, 1);
    pins[n++] = led[2] = config("LED2", DIR_OUTPUT, 1, 2);
    pins[n++] = led[3] = config("LED3", DIR_OUTPUT, 1, 3);
    pins[n++] = swtch[0] = config("SW0", DIR_INPUT, 1, 4);
    pins[n++] = swtch[1] = config("SW1", DIR_INPUT, 1, 5);
    pins[n++] = swtch[2] = config("SW2", DIR_INPUT, 1, 6);
    pins[n++] = swtch[3] = config("SW3", DIR_INPUT, 1, 7);
    pins[n++] = button = config("BUTTON", DIR_INPUT, 1, 12);
    pins[n++] = reg_enable = config("REG_ENABLE", DIR_OUTPUT, 1, 14);
    pins[n++] = bus_enable = config("BUS_ENABLE", DIR_OUTPUT, 1, 13);
    pins[n++] = i2c_panel_reset = config("PANEL_RESET", DIR_OUTPUT, 1, 28);
    pins[n++] = qunibus_activity_led = config("QUNIBUS_LED", DIR_OUTPUT, 0, 30);

    // double functions on header: P9.41 set other pin function to tristate
    pins[n++] = collision_p9_41 = config(NULL, DIR_INPUT, 3, 20);
    pins[n++] = collision_p9_42 = config(NULL, DIR_INPUT, 3, 18);

    pins[n] = NULL; // end mark

    // program pins registers
    // echo no  > /sys/class/gpio/export_pin

    for (n = 0; (gpio = pins[n]); n++)
        export_pin(gpio);

    // set pin directions
    // a) echo in|out > /sys/class/gpio/gpio<no>/direction
    // b) bitmask in pin->gpio_bank_oe_addr
    for (n = 0; (gpio = pins[n]); n++) {
        unsigned reg = *(gpio->bank->oe_addr);
        // bit set in OE register: pin is input.
        reg &= ~gpio->pin_in_bank_mask; // clear bit
        if (gpio->direction == DIR_INPUT)
            reg |= gpio->pin_in_bank_mask;
        *(gpio->bank->oe_addr) = reg;
    }

    // init output register cache with values (after export_pin!)
    for (n = 0; n < 4; n++)
        // bus error, if no gpio registered from this bank
        if (banks[n].gpios_in_use > 0)
            banks[n].cur_dataout_val = *(banks[n].dataout_addr);

    if (leds_for_debug) {
        // shared with LEDs
        ARM_DEBUG_PIN0(0)
        ;
        ARM_DEBUG_PIN1(0)
        ;
        ARM_DEBUG_PIN2(0)
        ;
        ARM_DEBUG_PIN3(0)
        ;
    } else {
        // set the 4 LEDs to OFF
        set_leds(cmdline_leds) ;
        if (qunibus_activity_led) // default: OFF
            GPIO_SETVAL(qunibus_activity_led, 1) ;
    }

	// On both UniBone PCB before 2022 and QBone timer5 is connected only to a test pin.
	// On UniBone 2022 timer5 can be jumpered onto UNIBUS LTC.
#if defined(UNIBUS)
    // produce 50Hz wave at LTC output.
    set_frequency(50) ;
#elif defined(QBUS)
    set_frequency(0) ; // switch off for now, FPGA generates LTC
#endif
}


// Setup timer5 output for aribtrary frequency 1Hz-24MHz
// used on UniBone for LTC generation
// frequency = 0: stable 0 level on timer5 pin
void gpios_c::set_frequency(unsigned frequency)
{
    // timer5 is programmed to toggle the output on each timer reload

    // map registers
    // on error: enable clock module for timer5
    // https://groups.google.com/g/beagleboard/c/_Xxpk05npuU/m/_5noVGFSaHkJ

    // 1) Module clock. CM_PER Clock module peripheral registers, 0x44e00000...ffff
    uint8_t *cm_per_base = (uint8_t *) mmap(0, 0x10000, PROT_READ | PROT_WRITE,
                                            MAP_SHARED, memory_filedescriptor, 0x44e00000) ;
    assert(cm_per_base != MAP_FAILED) ;
    uint32_t *cm_per_timer5_clkctrl = (uint32_t*) (cm_per_base + 0xec) ;
    uint32_t *clksel_timer5_clk = (uint32_t *) (cm_per_base + 0x518)  ; // CM_DPLL+0x18, page 1334
    *cm_per_timer5_clkctrl = 0x2 ; // enable timer5 clock module
    // now timer5 accessible!
    *clksel_timer5_clk = 1 ; // select CLK_M_OSC master clock = 24 MHz
    // clock source at page 1191

    // 2) Timer. DMTIMER5 module registers, 4K
    // see spruh73n.pdf, Chapter 20, pp 4370
    uint8_t *timer5_base = (uint8_t *) mmap(0, 4096, PROT_READ | PROT_WRITE,
                                            MAP_SHARED, memory_filedescriptor, 0x48046000);
    assert(timer5_base != MAP_FAILED) ;
    uint32_t *timer5_tclr = (uint32_t *) (timer5_base + 0x38) ; // control register
    uint32_t *timer5_tcrr = (uint32_t *) (timer5_base + 0x3c) ; // counter register
    uint32_t *timer5_tldr = (uint32_t *) (timer5_base + 0x40) ; // load register < 0xffffffff

	// Setup Timer5
    if (frequency > 0)  {
        *timer5_tclr =
            BIT(0) // ST = 1 Start the timer
            | BIT(1) // auto reload mode: tcrr := tldr on overflow
//        | ((7) << 2) // bit 4,3,2 = PTV = Pre scale clock timer value 2^7 = 128
//        | BIT(5) // PRE =1 = prescaler enabled
            | ((1) << 10) // bit11,10 = 01 = Trigger output mode on PORTIMERPWM = Trigger on overflow
            | BIT(12)  // PT = 1 = PORTIMERPWM Toggle
            // Bit14 = GPO_CFG General purpose output. This register drives directly the PORGPOCFG output pin
            // 0h = PORGPOCFG drives 0 and configures the timer pin as an output
            ;

		// timer counts from reload value upwards to 0xffffffff
        // timer must run at 100/120hz toggle rate to produce 50/60Hz square wave
        // 24Mhz count
        uint32_t timer_ticks_per_overflow = 24000000 / (2*frequency) ;
        *timer5_tcrr = *timer5_tldr = 0xffffffff - timer_ticks_per_overflow + 1 ; // write reloads
    } else {
        // stable GND level on timer5 pin
        // timer stop, PORGPOCFG drives 0 and configures the timer pin as an output,
        // no trigger, no autoreload
        // SCPWM=0: clear pin and generate positive pulses (which never happen)
        *timer5_tclr = 0 ;
        *timer5_tcrr = 0 ;
    }

    // free register mapping
    munmap(cm_per_base, 0x10000);
    munmap(timer5_base, 4096);

}


// display a number on the 4 LEDs
void gpios_c::set_leds(unsigned number)
{
    // inverted drivers
    GPIO_SETVAL(led[0], !(number & 1)) ;
    GPIO_SETVAL(led[1], !(number & 2)) ;
    GPIO_SETVAL(led[2], !(number & 4)) ;
    GPIO_SETVAL(led[3], !(number & 8)) ;
}


/*
 * Toggle in high speed, break with ^C
 */
void gpios_c::test_toggle(gpio_config_t *gpio)
{
    INFO("Highspeed toggle pin %s, stop with ^C.", gpio->name);

    // Setup ^C catcher
    SIGINTcatchnext();
    // high speed loop
    while (!SIGINTreceived) {
        // run, baby, run!
        // the access macros:
        GPIO_SETVAL(gpio, 1)
        ;
        GPIO_SETVAL(gpio, 0)
        ;
        // fastest possible:
        // *(gpio->bank->setdataout_addr) = gpio->pin_in_bank_mask;
        //*(gpio->bank->clrdataout_addr) = gpio->pin_in_bank_mask;
    }
}

/* visible loop back
 * Switches control LEDs
 * Button controls QBUS/UNIBUS reg_enable
 */
void gpios_c::test_loopback(void)
{
    timeout_c timeout;

    INFO("Manual loopback test, stop with ^C");
    INFO("Switch control LEDs, button controls \"" QUNIBONE_NAME " enable\".");

    // Setup ^C catcher
    SIGINTcatchnext();
    // high speed loop
    while (!SIGINTreceived) {
        GPIO_SETVAL(led[0], GPIO_GETVAL(swtch[0]));
        GPIO_SETVAL(led[1], GPIO_GETVAL(swtch[1]));
        GPIO_SETVAL(led[2], GPIO_GETVAL(swtch[2]));
        GPIO_SETVAL(led[3], GPIO_GETVAL(swtch[3]));
        GPIO_SETVAL(bus_enable, GPIO_GETVAL(button));
        if (qunibus_activity_led) // tied to driver enable
            GPIO_SETVAL(qunibus_activity_led, GPIO_GETVAL(button));
        timeout.wait_ms(10);
    }
}


void activity_led_c::waiter_func()
{
    // loop until terminated
    while (!waiter_terminated) {
        // polling frequency
        std::this_thread::sleep_for(std::chrono::milliseconds(cycle_time_ms));
        for (unsigned led_idx=0 ; led_idx < led_count ; led_idx++) {
            if (cycles[led_idx]) {
                const std::lock_guard<std::mutex> lock(m); // cycles[]
                // it is ON, or will change
                cycles[led_idx]-- ;
                if (cycles[led_idx]) // just switched ON, or still ON
                    GPIO_SETVAL(gpios->led[led_idx], 0) ; // ON
                else
                    GPIO_SETVAL(gpios->led[led_idx], 1) ; // now OFF
            }
        }
    }
}

activity_led_c::activity_led_c()
{
    waiter_terminated = false ;
    for (unsigned led_idx=0 ; led_idx < led_count ; led_idx++)
        cycles[led_idx] = 1 ; // will go off when thread waiter_func() starts
    enabled = false ;
    minimal_on_time_ms = 100 ;
}


activity_led_c::~activity_led_c()
{
    waiter_terminated = true ;
    waiter.join() ;
}


// a "set" pusles for 100ms, a "clear" does nothing
void activity_led_c::set(unsigned led_idx, bool onoff)
{
    const std::lock_guard<std::mutex> lock(m); // cycles[]
    assert(led_idx < led_count) ;
    // atomic commands
    if (onoff)
        // ON: load counter. "+1": pre-decrement in thread
        cycles[led_idx] = (minimal_on_time_ms / cycle_time_ms) + 1 ;
//    else
//        cycles[led_idx] = 0 ; // still active until counted down
}




