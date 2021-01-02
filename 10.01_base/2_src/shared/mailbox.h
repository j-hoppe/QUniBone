/* mailbox.h: Command and status data structures common to ARM and PRU

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


 12-nov-2018  JH      entered beta phase
 */

#ifndef _MAILBOX_H_
#define _MAILBOX_H_

#include <stdint.h>
#include "qunibus.h"

// ARM to PRU
#define ARM2PRU_NONE	0	// Operation complete: must be 0!
#define ARM2PRU_NOP	1	// to check wether PRU is running
#define ARM2PRU_HALT	2	// run PRU1 into halt
#define ARM2PRU_MAILBOXTEST1	3
#define ARM2PRU_BUSLATCH_INIT	4	// reset all mux registers to "neutral"
#define ARM2PRU_BUSLATCH_SET	5	// set a mux register
#define ARM2PRU_BUSLATCH_GET	6 	// read a mux register
#define ARM2PRU_BUSLATCH_EXERCISER	7 	// exercise 8 accesses to mux registers
#define ARM2PRU_BUSLATCH_TEST	8 	// read a mux register
#define ARM2PRU_INITALIZATIONSIGNAL_SET		9 	// set an ACL=/DCLO/INIT signal
#define ARM2PRU_ADDRESS_OVERLAY	10 	// const ADRRESS bits for M9312 BOOT logic
#define ARM2PRU_ARB_MODE_NONE		11               // DMA without NPR/NPG/SACK arbitration
#define ARM2PRU_ARB_MODE_CLIENT		12               // DMA with arbitration by external Arbitrator
#define ARM2PRU_DMA		13               // DMA with selected arbitration
#define ARM2PRU_INTR		14               // INTR with arbitration by external Arbitrator
#define ARM2PRU_INTR_CANCEL		15               // clear INTR which has been requested
#define ARM2PRU_CPU_ENABLE		16	// switch CPU master side functions ON/OFF
#define ARM2PRU_DDR_FILL_PATTERN	17	// fill DDR with test pattern
#define ARM2PRU_DDR_SLAVE_MEMORY	18	// use DDR as QBUS/UNIBUS slave memory
#define ARM2PRU_ARB_GRANT_INTR_REQUESTS	19 // emulated CPU answers device requests
#define ARM2PRU_CPU_BUS_ACCESS 20 // prohibit any activity of CPU on QBUS




#if defined(UNIBUS)
// signal IDs for ARM2PRU_INITALIZATIONSIGNAL_*
// states of initialization section lines. Bitmask = latch[7]
#define INITIALIZATIONSIGNAL_INIT	(1 << 3)
#define INITIALIZATIONSIGNAL_ACLO	(1 << 4)
#define INITIALIZATIONSIGNAL_DCLO	(1 << 5)
#define INITIALIZATIONSIGNAL_ANY (INITIALIZATIONSIGNAL_INIT | INITIALIZATIONSIGNAL_ACLO | INITIALIZATIONSIGNAL_DCLO)

#elif defined(QBUS)
// signal IDs for ARM2PRU_INITALIZATIONSIGNAL_*
// states of initialization section lines. Bitmask = latch[5]
#define INITIALIZATIONSIGNAL_INIT	BIT(0)
#define INITIALIZATIONSIGNAL_HALT	BIT(1) 
#define INITIALIZATIONSIGNAL_POK	BIT(3)
#define INITIALIZATIONSIGNAL_DCOK	BIT(4)
#define INITIALIZATIONSIGNAL_ANY (INITIALIZATIONSIGNAL_INIT | INITIALIZATIONSIGNAL_HALT | INITIALIZATIONSIGNAL_POK | INITIALIZATIONSIGNAL_DCOK)
#endif

// possible states of DMA machine
#define DMA_STATE_READY	0        	// idle
#define DMA_STATE_ARBITRATING	1	// in NPR/NPG/SACK arbitration
#define DMA_STATE_RUNNING	2	// transfering data
#define DMA_STATE_TIMEOUTSTOP	3	// stop because of QBUS/UNIBUS timeout
#define DMA_STATE_INITSTOP	4	// stop because INIT signal sensed

// Bit masks BR*/NPR and BG*/NPG in buslatch 0 and 1
// bit # is index into arbitration_request[] array.
#define PRIORITY_ARBITRATION_BIT_B4	0x01
#define PRIORITY_ARBITRATION_BIT_B5	0x02
#define PRIORITY_ARBITRATION_BIT_B6	0x04
#define PRIORITY_ARBITRATION_BIT_B7	0x08
#define PRIORITY_ARBITRATION_BIT_NP	0x10
#define PRIORITY_ARBITRATION_INTR_MASK	0x0f	// B4|B5|B6|B7
#define PRIORITY_ARBITRATION_BIT_MASK	0x1f

// CPU pririty level invalid between INTR receive and fetch of next PSW
#define CPU_PRIORITY_LEVEL_FETCHING	0xff

// data for a requested DMA operation
#define	PRU_MAX_DMA_WORDCOUNT	(8*512)

#include "ddrmem.h"

/***** start of shared structs *****/
// on PRU. all struct are byte-packed, no "#pragma pack" there
// (support answer 20.5.2018,  issue CODEGEN-4832)
#ifdef ARM
#pragma pack(push,1)
#endif

typedef struct {
	uint32_t addr; // register 0..7
	uint32_t val; // value set/get.
} mailbox_test_t;

typedef struct {
	uint32_t addr; // register 0..7
	uint32_t bitmask; // change only these bits in register
	uint32_t val; // value set/get.
} mailbox_buslatch_t;

#if defined(UNIBUS)
#define MAILBOX_BUSLATCH_EXERCISER_PATTERN_COUNT	4
#elif defined(QBUS)
#define MAILBOX_BUSLATCH_EXERCISER_PATTERN_COUNT	3
#endif
typedef struct {
	uint8_t pattern; // input: which access pattern? 0..MAILBOX_BUSLATCH_EXERCISER_PATTERN_COUNT-1
	uint8_t addr[8]; // access sequence of register addresses
	uint8_t writeval[8]; // data value for each
	uint8_t readval[8]; // read back results
} mailbox_buslatch_exerciser_t;

typedef struct {
	uint8_t addr_0_7;	// start values for test sequence
	uint8_t addr_8_15;
	uint8_t data_0_7;
	uint8_t data_8_15;
} mailbox_buslatch_test_t;

typedef struct {
	uint16_t id; // which signal to set or get? one of INITIALIZATIONSIGNAL_*
	uint16_t val; // value set/get.
} mailbox_initializationsignal_t;

// data for bus arbitrator
typedef struct {
	// ifs = Interrupt Fielding Processor
	uint8_t ifs_priority_level; // Priority level of CPU, visible in PSW. 7,6,5,4 <4.

	uint8_t	ifs_intr_arbitration_pending ; // produce GRANTS from requests

	uint8_t _dummy[2];	// keep 32 bit borders

} mailbox_arbitrator_t;

// data for a requested DMA operation
typedef struct {
	// take care of 32 bit word borders for struct members
	uint8_t cur_status; // 0 = idle, 1 = DMA running, 2 = timeout error

	uint8_t buscycle; // cycle to perform: only DATO, DATI allowed
	uint16_t wordcount; // # of remaining words transmit/receive, static
	// ---dword---
	uint8_t	cpu_access ; // 0 for device DMA, 1 for emulated CPU
	uint8_t	dummy[3] ;
	// ---dword---
	uint32_t cur_addr; // current address in transfer, if timeout: offending address.
	// if complete: last address accessed.
	uint32_t startaddr; // address of 1st word to transfer
	uint16_t words[PRU_MAX_DMA_WORDCOUNT]; // buffer for rcv/xmt data
} mailbox_dma_t;

// data for all 4 pending INTR requests
// vector for an INTR transaction
typedef struct {
	/* all requested INTRs */
	uint16_t vector[4]; // interrupt vectors for BR4..7 to be transferred
	// ---dword---

	/* data for currently requested with ARM2PRU_INTR */
	uint8_t priority_arbitration_bit; //  PRIORITY_ARBITRATION_BIT_*
	uint8_t level_index; // newly requested BR*. 0 = BR4, ... 3 = BR7
	// interrupt register state to be set atomically with BR line
	uint16_t iopage_register_value;
	// ---dword---
	uint8_t iopage_register_handle;
	uint8_t _dummy1, _dummy2, _dummy3;
	// multiple of 32 bit now
} mailbox_intr_t;

/* PRU->ARM event signaling is a signal/acknowledge protocoll.
 There are no shared mutexes for PRU / ARM mailbox protection.
 So protocol must be implmeneted with the "single writer -multiple reader" pattern,
 where only a single writer modifes shared variables.
 For each event source there are 2 channels (variables)
 - signal: PRU arites, ARM reads
 - acknowledge: ARM writes, PRU reads.
 Both variables are rollaround-counters, which are simply updated on event.
 PRU raises event with "signaled++", and checks for ARM ack with
 "if (signaled != acked) ..."
 ARM checks for pending signals with
 "if (signaled != acked) ..."
 and acknowledees an event with "acked++".
 */
#define EVENT_SIGNAL(mailbox,source) ((mailbox).events.source.signaled++)
#define EVENT_ACK(mailbox,source) ((mailbox).events.source.acked++)
#define EVENT_IS_ACKED(mailbox,source) ((mailbox).events.source.signaled == (mailbox).events.source.acked)

// Access to device register detected
typedef struct {
	uint8_t signaled; //  PRU->ARM
	uint8_t acked; // ARM->PRU
	// info about register access
	uint8_t unibus_control; // DATI,DATO,DATOB
	// handle of controller
	uint8_t device_handle;
	// ---dword---
	uint16_t data; // deviceregister_data value for DATO event
	uint8_t register_idx; // # of register in device space
	uint8_t _dummy1;
	// ---dword---
	// QUNIBUS address accessed
	uint32_t addr; // accessed address: odd/even important for DATOB
} mailbox_event_deviceregister_t;

// DMA transfer complete
typedef struct {
	/* After ARM2PRU_DMA_*, NPR/NPG/SACK protocll was executed and
	 Data trasnfered accoring to mailbox_dma_t.
	 After that, mailbox_dma_t is updated and signal raised.
	 */
	uint8_t signaled; //  PRU->ARM
	uint8_t acked; // ARM->PRU
//int8_t cpu_transfer; // 1: ARM must process DMA as completed cpu DATA transfer
	uint8_t _dummy2[2];
} mailbox_event_dma_t;

// INTR raised by device
typedef struct {
	/* Event priority arbitration INTR transfer complete
	 After ARM2PRU_INTR, one of BR4/5/6/7 NP was requested,
	 granted, and the deviceregister.data transfer was handled as bus master.
	 */
	uint8_t signaled; // PRU->ARM, one of BR/IRQ4,5,6,7 vector on QBUS/UNIBUS
	uint8_t acked; // ARM->PRU
	//uint8_t level_index; // 0..3 -> BR4..BR7
	uint8_t _dummy[2];
} mailbox_event_intr_master_t;

// INTR received by CPU
typedef struct {
	uint8_t signaled; // PRU->ARM, one of BR4/IRQ,5,6,7 vector on QBUS/UNIBUS
	uint8_t acked; // ARM->PRU
	uint16_t vector; // received vector
} mailbox_event_intr_slave_t;

// change of INIT signal
typedef struct {
	uint8_t signaled; // PRU->ARM
	uint8_t acked; // ARM->PRU
	uint8_t _dummy[2];
} mailbox_event_init_t;

// change of ACLO/DCLO signals
typedef struct {
	uint8_t signaled; // PRU->ARM
	uint8_t acked; // ARM->PRU
	uint8_t _dummy[2];
} mailbox_event_power_t;

typedef struct {
	// different events can be raised asynchronically and concurrent,
	// but a single event type is sequentially signaled by PRU and acked by ARM.
	mailbox_event_deviceregister_t deviceregister;
	mailbox_event_dma_t dma;

	// one event for each BG4,5,6,7
	mailbox_event_intr_master_t intr_master[4];

	mailbox_event_intr_slave_t intr_slave;

	/*** INIT or Power cycle seen on QBUS/UNIBUS ***/
	mailbox_event_init_t init;
	mailbox_event_power_t power;

	uint8_t power_signals_prev; // on event: a signal changed from this ...
	uint8_t power_signals_cur; // ... to this

//	uint8_t init_signal_prev; // on event: a signal changed from this ...
	uint8_t init_signal_cur; // ... to this

	uint8_t _dummy9[1]; // make record multiple of dword !!!
} mailbox_events_t;

typedef struct {

	// generic request/response flags
	uint32_t arm2pru_req;

	// physical location of shared DDR memory. PDP-11 memory words.
	volatile ddrmem_t *ddrmem_base_physical;

	mailbox_arbitrator_t arbitrator;

	// set by PRU, read by ARM on event
	mailbox_events_t events;

	mailbox_intr_t intr;

	mailbox_dma_t dma;

	uint32_t address_overlay;

	// data structs for misc. opcodes
	union {
		mailbox_test_t mailbox_test;
		mailbox_buslatch_t buslatch;
		mailbox_buslatch_test_t buslatch_test;
		mailbox_buslatch_exerciser_t buslatch_exerciser;
		mailbox_initializationsignal_t initializationsignal;
		uint32_t	param ; // generic parameter for ARM2PRU commands
	};

    // possibly not aligned to 32 bit here
} mailbox_t;

#ifdef ARM
#pragma pack(pop)
#endif
/***** end of shared structs *****/

#ifdef ARM

// included by ARM code
#ifndef _MAILBOX_CPP_
extern volatile mailbox_t *mailbox;
#endif

// interface to mailbox.c on ARM application
void mailbox_print(void);
int mailbox_connect(void);
void mailbox_test1(void);
bool mailbox_execute(uint8_t request);

#else
// included by PRU code
#ifndef _MAILBOX_C_
extern volatile far mailbox_t mailbox;
#endif

// code to send an register access event
// iopageregister_t *reg
#define DO_EVENT_DEVICEREGISTER(_reg,_unibus_control,_addr,_data)	do { \
			/* register read changes device state: signal to ARM */ 	\
			mailbox.events.deviceregister.unibus_control = _unibus_control ;				\
			mailbox.events.deviceregister.device_handle = _reg->event_device_handle ;\
			mailbox.events.deviceregister.register_idx = _reg->event_device_register_idx ; \
			mailbox.events.deviceregister.addr = _addr ;									 \
			mailbox.events.deviceregister.data = _data ;									\
			EVENT_SIGNAL(mailbox,deviceregister) ;						\
			/* data for ARM valid now*/ 									\
			PRU2ARM_INTERRUPT ; 											\
			/* leave SSYN asserted until mailbox.event.signal ACKEd to 0 */ \
		} while(0)


#endif


#endif // _MAILBOX_H_
