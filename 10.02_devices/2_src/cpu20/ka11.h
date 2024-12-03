// Interface of 11/20 CPU emulator to UniBone

enum {
	KA11_STATE_HALTED = 0,
	KA11_STATE_RUNNING = 1,
	KA11_STATE_WAITING = 2
};


typedef struct KA11 KA11;
struct KA11
{
	word r[16];
	word b;		// B register before BUT JSRJMP
	word ba;
	word ir;
	Bus *bus;
	byte psw;
	int traps;
	int be;
	int state;

	struct {
		int (*bg)(void *dev);
		void *dev;
	} br[4];

	// UniBone 	
	pthread_mutex_t mutex ;
	volatile bool external_intr ; // INTR by parallel thread pending
	volatile word external_intrvec;	// associated vector

	word sw;
	int swab_vbit;

    // jal extended instruction set
    int extended_instr;
};


void ka11_tracestate(KA11 *cpu);
void ka11_printstate(KA11 *cpu);
void ka11_reset(KA11 *cpu);
void ka11_setintr(KA11 *cpu, unsigned vec);
void ka11_pwrfail_trap(KA11 *cpu);
void ka11_pwrup_vector_fetch(KA11 *cpu);
void ka11_condstep(KA11 *cpu);

