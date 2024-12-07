#include "11.h"
#include "ka11.h"

#include "gpios.hpp" // ARM_DEBUG_PIN*

void unibone_grant_interrupts(void) ;
int unibone_dato(unsigned addr, unsigned data);
int unibone_datob(unsigned addr, unsigned data);
int unibone_dati(unsigned addr, unsigned *data);
void unibone_prioritylevelchange(uint8_t level);
void unibone_bus_init() ;

bool unibone_trace_addr(uint16_t a) ;


int
dati_bus(Bus *bus)
{
	unsigned int data;
	if(!unibone_dati(bus->addr, &data))
		return 1;
	bus->data = data;
	return 0;
}

int
dato_bus(Bus *bus)
{
	return !unibone_dato(bus->addr, bus->data);
}

int
datob_bus(Bus *bus)
{
	return !unibone_datob(bus->addr, bus->data);
}

void
levelchange(word psw)
{
	unibone_prioritylevelchange(psw>>5);
}




enum {
	PSW_PR = 0340,
	PSW_T = 020,
	PSW_N = 010,
	PSW_Z = 004,
	PSW_V = 002,
	PSW_C = 001,
};

enum {
	TRAP_STACK = 1,
	TRAP_PWR = 2,
	TRAP_BR7 = 4,
	TRAP_BR6 = 010,
	TRAP_BR5 = 040,
	TRAP_BR4 = 0100,
	TRAP_CSTOP = 01000	// can't happen?
};

#define ISSET(f) ((cpu->psw&(f)) != 0)


word
sgn(word w)
{
	return (w>>15)&1;
}

word
sxt(byte b)
{
	return (word)(int8_t)b;
}

static uint32
ubxt(word a)
{
	return (a&0160000)==0160000 ? a|0600000 : a;
}

void
ka11_tracestate(KA11 *cpu)
{
	(void)cpu;
	trace(" R0 %06o R1 %06o R2 %06o R3 %06o R4 %06o R5 %06o R6 %06o R7 %06o\n"
		" 10 %06o 11 %06o 12 %06o 13 %06o 14 %06o 15 %06o 16 %06o 17 %06o\n"
		" BA %06o IR %06o PSW %03o\n"
		,
		cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3],
		cpu->r[4], cpu->r[5], cpu->r[6], cpu->r[7],
		cpu->r[8], cpu->r[9], cpu->r[10], cpu->r[11],
		cpu->r[12], cpu->r[13], cpu->r[14], cpu->r[15],
		cpu->ba, cpu->ir, cpu->psw);
}

void
ka11_printstate(KA11 *cpu)
{
	(void)cpu;
	printf(" R0 %06o R1 %06o R2 %06o R3 %06o R4 %06o R5 %06o R6 %06o R7 %06o\n"
		" 10 %06o 11 %06o 12 %06o 13 %06o 14 %06o 15 %06o 16 %06o 17 %06o\n"
		" BA %06o IR %06o PSW %03o\n"
		,
		cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3],
		cpu->r[4], cpu->r[5], cpu->r[6], cpu->r[7],
		cpu->r[8], cpu->r[9], cpu->r[10], cpu->r[11],
		cpu->r[12], cpu->r[13], cpu->r[14], cpu->r[15],
		cpu->ba, cpu->ir, cpu->psw);
}

// only to be called from ka11_condstep() thread
void
ka11_reset(KA11 *cpu)
{
	Busdev *bd;

	cpu->traps = 0;
	cpu->external_intr = 0;
	cpu->mutex = PTHREAD_MUTEX_INITIALIZER ;

	for(bd = cpu->bus->devs; bd; bd = bd->next)
		bd->reset(bd->dev);
}

int
dati(KA11 *cpu, int b)
{
	if(!b && cpu->ba&1)
		goto be;

	/* internal registers */
	if((cpu->ba&0177400) == 0177400){
		switch(cpu->ba&0377){
		case 0170: case 0171:
			cpu->bus->data = cpu->sw;
			goto ok;
		case 0376:
			cpu->bus->data = cpu->psw;
			goto ok;
		case 0377:
		    goto be;

		/* respond but don't return real data */
		case 0147:
			cpu->bus->data = 0;
			goto ok;
		}
	}

	cpu->bus->addr = ubxt(cpu->ba)&~1;
	if(dati_bus(cpu->bus))
		goto be;
ok:
if (unibone_trace_addr(cpu->ba))
 	trace("DATI [%06o] => %06o\n", cpu->ba, cpu->bus->data);
	cpu->be = 0;
	return 0;
be:
	trace("DATI [%06o]: NXM\n", cpu->ba);
	cpu->be++;
	return 1;
}

int
dato(KA11 *cpu, int b)
{
if (unibone_trace_addr(cpu->ba)) // default: all
trace("%s [%06o] <= %06o\n", b? "DATOB":"DATO", cpu->ba, cpu->bus->data);
	if(!b && cpu->ba&1)
		goto be;

	/* internal registers */
	if((cpu->ba&0177400) == 0177400){
		switch(cpu->ba&0377){
		case 0170: case 0171:
			/* can't write switches */
			goto ok;
		case 0376:
			/* writes 0 for the odd byte.
			   I think this is correct. */
			cpu->psw = cpu->bus->data;
			levelchange(cpu->psw);
			goto ok;
		case 0377:
		    goto be;
		}
	}

	if(b){
		cpu->bus->addr = ubxt(cpu->ba);
		if(datob_bus(cpu->bus))
			goto be;
	}else{
		cpu->bus->addr = ubxt(cpu->ba)&~1;
		if(dato_bus(cpu->bus))
			goto be;
	}
ok:
	cpu->be = 0;
	return 0;
be:
	cpu->be++;
	return 1;
}

static void
svc(KA11 *cpu, Bus *bus)
{
	int l;
	Busdev *bd;
	static int brtraps[4] = { TRAP_BR4, TRAP_BR5, TRAP_BR6, TRAP_BR7 };
	for(l = 0; l < 4; l++){
		cpu->br[l].bg = nil;
		cpu->br[l].dev = nil;
	}
	cpu->traps &= ~(TRAP_BR4|TRAP_BR5|TRAP_BR6|TRAP_BR7);
	for(bd = bus->devs; bd; bd = bd->next){
		l = bd->svc(bus, bd->dev);
		if(l >= 4 && l <= 7 && cpu->br[l-4].bg == nil){
			cpu->br[l-4].bg = bd->bg;
			cpu->br[l-4].dev = bd->dev;
			cpu->traps |= brtraps[l-4];
		}
	}
}

static int
addrop(KA11 *cpu, int m, int b)
{
	int r;
	int ai;
	r = m&7;
	m >>= 3;
	ai = 1 + (!b || (r&6)==6 || m&1);
	if(m == 0){
		assert(0);
		return 0;
	}
	switch(m&6){
	case 0:		// REG
		cpu->b = cpu->ba = cpu->r[r];
		return 0;	// this already is mode 1
	case 2:		// INC
		cpu->ba = cpu->r[r];
		cpu->b = cpu->r[r] = cpu->r[r] + ai;
		break;
	case 4:		// DEC
		cpu->b = cpu->ba = cpu->r[r]-ai;
		if(r == 6 && (cpu->ba&~0377) == 0) cpu->traps |= TRAP_STACK;
		cpu->r[r] = cpu->ba;
		break;
	case 6:		// INDEX
		cpu->ba = cpu->r[7];
		cpu->r[7] += 2;
		if(dati(cpu, 0)) return 1;
		cpu->b = cpu->ba = cpu->bus->data + cpu->r[r];
		break;
	}
	if(m&1){
		if(dati(cpu, 0)) return 1;
		cpu->b = cpu->ba = cpu->bus->data;
	}
	return 0;

}

static int
fetchop(KA11 *cpu, int t, int m, int b)
{
	int r;
	r = m&7;
	if((m&070) == 0)
		cpu->r[t] = cpu->r[r];
	else{
		if(dati(cpu, b)) return 1;
		cpu->r[t] = cpu->bus->data;
		if(b && cpu->ba&1) cpu->r[t] = cpu->r[t]>>8;
	}
	if(b) cpu->r[t] = sxt(cpu->r[t]);
	return 0;
}

static int
readop(KA11 *cpu, int t, int m, int b)
{
	return !(addrop(cpu, m, b) == 0 && fetchop(cpu, t, m, b) == 0);
}

static int
writedest(KA11 *cpu, word v, int b)
{
	int d;
	if((cpu->ir & 070) == 0){
		d = cpu->ir & 7;
		if(b) SETMASK(cpu->r[d], v, 0377);
		else cpu->r[d] = v;
	}else{
		if(cpu->ba&1) v <<= 8;
		cpu->bus->data = v;
		if(dato(cpu, b)) return 1;
	}
	return 0;
}

static void
setnz(KA11 *cpu, word w)
{
	cpu->psw &= ~(PSW_N|PSW_Z);
	if(w & 0100000) cpu->psw |= PSW_N;
	if(w == 0) cpu->psw |= PSW_Z;
}

void
step(KA11 *cpu)
{
	uint by;
	uint br;
	uint b;
	uint c;
	uint src, dst, sf, df, sm, dm;
	word mask, sign;
	int inhov;
	byte oldpsw;
	uint reg;
	int32_t prod;
	word sh;

//	printf("fetch from %06o\n", cpu->r[7]);
//	printstate(cpu);

#define SP	cpu->r[6]
#define PC	cpu->r[7]
#define SR	cpu->r[010]
#define DR	cpu->r[011]
#define TV	cpu->r[012]
#define BA	cpu->ba
#define PSW	cpu->psw

#define RD_B	if(sm != 0) if(readop(cpu, 010, src, by)) goto be;\
		if(dm != 0) if(readop(cpu, 011, dst, by)) goto be;\
		if(sm == 0) fetchop(cpu, 010, src, by);\
		if(dm == 0) fetchop(cpu, 011, dst, by)
#define RD_U	if(dm != 0) if(readop(cpu, 011, dst, by)) goto be;\
		if(dm == 0) fetchop(cpu, 011, dst, by);\
		SR = DR
#define WR	if(writedest(cpu, b, by)) goto be
#define NZ	setnz(cpu, b)
#define SVC	goto service
#define TRAP(v)	TV = v; goto trap
#define CLC	cpu->psw &= ~PSW_C
#define CLV	cpu->psw &= ~PSW_V
#define CLCV	cpu->psw &= ~(PSW_V|PSW_C)
#define SEV	cpu->psw |= PSW_V
#define SEC	cpu->psw |= PSW_C
#define C	if(b & 0200000) SEC
#define NC	if((b & 0200000) == 0) SEC
#define CLNZ	cpu->psw &= ~(PSW_N|PSW_Z)
#define SEN	cpu->psw |= PSW_N
#define SEZ	cpu->psw |= PSW_Z
#define BXT	if(by) b = sxt(b)
#define BR	PC += br
#define CBR(c)	if(((c)>>(cpu->psw&017)) & 1) BR
#define PUSH	SP -= 2; if(!inhov && (SP&~0377) == 0) cpu->traps |= TRAP_STACK
#define POP	SP += 2
#define OUT(a,d)	cpu->ba = (a); cpu->bus->data = (d); if(dato(cpu, 0)) goto be
#define IN(d)	if(dati(cpu, 0)) goto be; d = cpu->bus->data
#define INA(a,d)	cpu->ba = a; if(dati(cpu, 0)) goto be; d = cpu->bus->data
#define TR(m)	if (unibone_trace_addr(PC-2)) trace("EXEC [%06o] "#m"\n", PC-2)
#define TRB(m)	if (unibone_trace_addr(PC-2)) trace("EXEC [%06o] "#m"%s\n", PC-2, by ? "B" : "")
//#define TR(m)	trace("EXEC [%06o] "#m"\n", PC-2)
//#define TRB(m)	trace("EXEC [%06o] "#m"%s\n", PC-2, by ? "B" : "")

	inhov = 0;

	{
		// external interrupt from parallel threads?
		pthread_mutex_lock(&cpu->mutex) ;
		bool external_intr = cpu->external_intr ;
		word external_intrvec = cpu->external_intrvec ;
		cpu->external_intr = 0 ;
		pthread_mutex_unlock(&cpu->mutex) ;
		if (external_intr){
			//ARM_DEBUG_PIN1(0);	// INTR processed
			cpu->state = KA11_STATE_RUNNING ;
			TRAP(external_intrvec);
		}	
	}


	oldpsw = PSW;
	INA(PC, cpu->ir);
	PC += 2;	/* don't increment on bus error! */
	by = !!(cpu->ir&B15);
	br = sxt(cpu->ir)<<1;
	src = cpu->ir>>6 & 077;
	sf = src & 7;
	sm = src>>3 & 7;
	dst = cpu->ir & 077;
	df = dst & 7;
	dm = dst>>3 & 7;
	if(by)	mask = M8, sign = B7;
	else	mask = M16, sign = B15;

	/* Binary */
	switch(cpu->ir & 0170000){
	case 0110000: case 0010000:	TRB(MOV);
		RD_B; CLV;
		b = SR; NZ;
		if(dm==0) cpu->r[df] = SR;
		else writedest(cpu, SR, by);
		SVC;
	case 0120000: case 0020000:	TRB(CMP);
		RD_B; CLCV;
		b = SR + W(~DR) + 1; NC; BXT;
		if(sgn((SR ^ DR) & ~(DR ^ b))) SEV;
		NZ; SVC;
	case 0130000: case 0030000:	TRB(BIT);
		RD_B; CLV;
		b = DR & SR;
		NZ; SVC;
	case 0140000: case 0040000:	TRB(BIC);
		RD_B; CLV;
		b = DR & ~SR;
		NZ; WR; SVC;
	case 0150000: case 0050000:	TRB(BIS);
		RD_B; CLV;
		b = DR | SR;
		NZ; WR; SVC;
	case 0060000:			TR(ADD);
		by = 0; RD_B; CLCV;
		b = SR + DR; C;
		if(sgn(~(SR ^ DR) & (DR ^ b))) SEV;
		NZ; WR; SVC;
	case 0160000:			TR(SUB);
		by = 0; RD_B; CLCV;
		b = DR + W(~SR) + 1; NC;
		if(sgn((SR ^ DR) & (DR ^ b))) SEV;
		NZ; WR; SVC;

	/* Reserved instructions */
	case 0170000:
        goto ri;

	case 0070000:
		reg = (cpu->ir >> 6) & 07;
    	if(cpu->extended_instr) {
        	switch(cpu->ir & 0177000) {
              	default:
		    		printf("-- ext: %o\n", cpu->ir);
                	goto ri;

				case 0070000:		TR(MUL);
					RD_U;
              		cpu->psw &= ~(PSW_N|PSW_Z|PSW_V);
              		prod = (int32_t) DR * (int32_t) cpu->r[reg];
					if(prod < 32768 || prod > 32767)
						SEC;
					if(prod == 0)
						SEZ;
					if(prod < 0)
						SEN;

              		if(reg & 0x1) {
              			//-- Odd register: store only lower 16 bits
						cpu->r[reg] = (word) prod;
              		} else {
              			cpu->r[reg] = prod & 0xffff;
              			cpu->r[reg + 1] = (word) (prod >> 16);
              		}
					SVC;

				case 0071000:		TR(DIV);
					RD_U;
              		cpu->psw &= ~(PSW_N|PSW_Z|PSW_V|PSW_C);
					if(reg & 0x1) goto ri;			// for div register must be even
					prod = (int32_t) cpu->r[reg] | ((int32_t) cpu->r[reg + 1] << 16);	// 32bit signed r, r+1
					if(DR == 0) {
						SEC;
						SEV;
					} else {
						uint32_t quot = prod / (int32_t) DR;
						uint32_t rem = prod % (int32_t) DR;
						if(quot < 32768 || quot > 32767) {
							SEV;
						} else {
							cpu->r[reg] = (word) quot;
							cpu->r[reg + 1] = (word) (rem);
						}
					}
					SVC;

				case 0072000:		TR(ASH);
                	// ASH
					RD_U;
              		cpu->psw &= ~(PSW_N|PSW_Z);
					b = cpu->r[reg];
//					printf("ASH: reg=%d, in=%o, shift=%o\n", reg, b, DR);
					sh = (DR & 0x3f);				// Extract 6 bits
					if(sh & 0x20) {		// -ve?
						// we shift right
						sh = 0x40 - sh;					// +ve shift, 1..62
                        if(sh > 15) {
                			b = 0;
//                			CLC;				// not clear whether this gets cleared
                			SEZ;
                		} else {
                			mask = sgn(b) ? 0xffff : 0x0;
							if(b & (1 << (sh - 1)))
								SEC;
							else
								CLC;
							b >>= sh;
							mask <<= (16 - sh);
							b |= mask;					// Sign extend
							NZ;
							if(b & B15)
								SEN;
                		}
                	} else {
                		// we shift left
						if(sh > 15) {
							b = 0;
//							CLC;
							SEZ;
						} else if(sh > 0) {
							if(b & (1 << (16 - sh))) {	// Get bit shifted out @ left
								SEC;
							} else {
								CLC;
							}
							b <<= sh;
							NZ;
							if(b & B15)
								SEN;
						}
                	}
//					printf("ASH: out=%o\n", b);
					cpu->r[reg] = b;
					SVC;

              	case 0073000:		TR(ASHC);
					RD_U;
              		cpu->psw &= ~(PSW_N|PSW_Z);
              		{
              			uint32_t val = ((uint32_t) cpu->r[reg] << 16) | cpu->r[reg | 1];	// The bitwise OR is intentional!

//						printf("ASHC: reg=%d, in=%o, shift=%o\n", reg, val, DR);
						sh = (DR & 0x3f);					// Extract 6 bits
						if(sh & 0x20) {		// -ve?
							// we shift right
							sh = 0x40 - sh;					// +ve shift, 1..62
                    	    if(sh > 31) {
                				val = 0;
//                				CLC;						// not clear whether this gets cleared
	                			SEZ;
    	            		} else {
        	        			uint32_t msk = val & 0x80000000L ? 0xffffffffL : 0x0;
								if(val & (1 << (sh - 1)))
									SEC;
								else
									CLC;
								val >>= sh;
								msk <<= (32 - sh);
								val |= msk;				// Sign extend
								if(val == 0)
									SEZ;
								if(val & 0x80000000L)
									SEN;
                			}
	                	} else {
    	            		//-- We shift left
							if(sh > 31) {
								val = 0;
//								CLC;
								SEZ;
							} else if(sh > 0) {
								if(val & (1 << (32 - sh))) {	// Get bit shifted out & left
									SEC;
								} else {
									CLC;
								}
								val <<= sh;
								if(val == 0)
									SEZ;
								if(val & 0x80000000L)
									SEN;
							}
            	    	}
//						printf("ASH: out=%o\n", b);
						if(reg & 0x1) {
							cpu->r[reg] = (word) val;		// Truncated result
						} else {
							cpu->r[reg] = (word) (val >> 16);
							cpu->r[reg + 1] = (word) val;
						}
					}
					SVC;
                	break;

              	case 0074000:		TR(XOR);
              		RD_U;
              		cpu->psw &= ~(PSW_N|PSW_Z|PSW_V);
					b = cpu->r[reg];
//					printf("XOR: reg=%d, in=%o, val=%o\n", (cpu->ir >> 6) & 07, b, DR);
					b = DR ^ b;
//					printf("- result=%o\n", b);
					if(sgn(b)) {
						SEN;
					}
					NZ;
					WR; SVC;

				case 0077000:		TR(SOB);
					b = --(cpu->r[reg]);		// decrement reg
//					printf("SOB: reg=%d, val after dec=%o, off=%o\n", (cpu->ir >> 6) & 07, b, (cpu->ir & 077) << 1);
					if(b != 0) {
						//-- Jump
						mask = (cpu->ir & 077) << 1;			// Get jump offset (*2)
						cpu->r[7] -= mask;						// Decrement by offset
//						printf("- jmp to %o\n", cpu->r[7]);
					}
					SVC;
			}
		}

        // All else, or not extended instr
       	goto ri;
	}
	//-- remaining here is ir=x0xxxx

	/* Unary */
	switch(cpu->ir & 0007700){
	case 0005000:	TRB(CLR);
		RD_U; CLCV;
		b = 0;
		NZ; WR; SVC;
	case 0005100:	TRB(COM);
		RD_U; CLV; SEC;
		b = W(~SR);
		NZ; WR; SVC;
	case 0005200:	TRB(INC);
		RD_U; CLV;
		b = W(SR+1); BXT;
		if(sgn(~SR&b)) SEV;
		NZ; WR; SVC;
	case 0005300:	TRB(DEC);
		RD_U; CLV;
		b = W(SR+~0); BXT;
		if(sgn(SR&~b)) SEV;
		NZ; WR; SVC;
	case 0005400:	TRB(NEG);
		RD_U; CLCV;
		b = W(~SR+1); BXT; if(b) SEC;
		if(sgn(b&SR)) SEV;
		NZ; WR; SVC;
	case 0005500:	TRB(ADC);
		RD_U; c = ISSET(PSW_C); CLCV;
		b = SR + c; C; BXT;
		if(sgn(~SR&b)) SEV;
		NZ; WR; SVC;
	case 0005600:	TRB(SBC);
		RD_U; c = !ISSET(PSW_C)-1; CLCV;
		b = W(SR+c); if(c && SR == 0) SEC; BXT;
		if(sgn(SR&~b)) SEV;
		NZ; WR; SVC;
	case 0005700:	TRB(TST);
		RD_U; CLCV;
		b = SR;
		NZ; SVC;

	case 0006000:	TRB(ROR);
		RD_U; c = ISSET(PSW_C); CLCV;
		b = (SR&mask) >> 1; if(c) b |= sign; if(SR & 1) SEC; BXT;
		NZ; if((PSW>>3^PSW)&1) SEV;
		WR; SVC;
	case 0006100:	TRB(ROL);
		RD_U; c = ISSET(PSW_C); CLCV;
		b = (SR<<1) & mask; if(c) b |= 1; if(SR & B15) SEC; BXT;
		NZ; if((PSW>>3^PSW)&1) SEV;
		WR; SVC;
	case 0006200:	TRB(ASR);
		RD_U; c = ISSET(PSW_C); CLCV;
		b = W(SR>>1) | SR&B15; if(SR & 1) SEC; BXT;
		NZ; if((PSW>>3^PSW)&1) SEV;
		WR; SVC;
	case 0006300:	TRB(ASL);
		RD_U; CLCV;
		b = W(SR<<1); if(SR & B15) SEC; BXT;
		NZ; if((PSW>>3^PSW)&1) SEV;
		WR; SVC;

	case 0006400:
		// mtps
		if(!cpu->allow_mxps || !by)
			goto ri;
		RD_U;
		cpu->psw = (cpu->psw & 0xff00) | (DR & 0377);
		SVC;

	case 0006500:
	case 0006600:
		goto ri;

	case 0006700:
		// mfps
		if(!cpu->allow_mxps || !by)
			goto ri;
		b = cpu->psw & 0377;
		WR; SVC;
	}

	switch(cpu->ir & 0107400){
	case 0004000:
	case 0004400:	TR(JSR);
		if(dm == 0) goto ill;
		if(addrop(cpu, dst, 0)) goto be;
		DR = cpu->b;
		PUSH; OUT(SP, cpu->r[sf]);
		cpu->r[sf] = PC; PC = DR;
		SVC;
	case 0104000:	TR(EMT); TRAP(030);
	case 0104400:	TR(TRAP); TRAP(034);
	}

	/* Branches */
    // ! 000 0!! !xx xxx xxx    (! = at least one is non-zero)
    if((cpu->ir & 074000) == 0 && (cpu->ir & 0103400) != 0) {
        switch(cpu->ir & 0103400){
        case 0000400:	TR(BR); BR; SVC;
        case 0001000:	TR(BNE); CBR(0x0F0F); SVC;
        case 0001400:	TR(BEQ); CBR(0xF0F0); SVC;
        case 0002000:	TR(BGE); CBR(0xCC33); SVC;
        case 0002400:	TR(BLT); CBR(0x33CC); SVC;
        case 0003000:	TR(BGT); CBR(0x0C03); SVC;
        case 0003400:	TR(BLE); CBR(0xF3FC); SVC;
        case 0100000:	TR(BPL); CBR(0x00FF); SVC;
        case 0100400:	TR(BMI); CBR(0xFF00); SVC;
        case 0101000:	TR(BHI); CBR(0x0505); SVC;
        case 0101400:	TR(BLOS); CBR(0xFAFA); SVC;
        case 0102000:	TR(BVC); CBR(0x3333); SVC;
        case 0102400:	TR(BVS); CBR(0xCCCC); SVC;
        case 0103000:	TR(BCC); CBR(0x5555); SVC;
        case 0103400:	TR(BCS); CBR(0xAAAA); SVC;
        }
    }

	/* Misc */
	switch(cpu->ir & 0777300){
	case 0100:	TR(JMP);
		if(dm == 0) goto ill;
		if(addrop(cpu, dst, 0)) goto be;
		PC = cpu->b;
		SVC;
	case 0200:
        switch(cpu->ir&070){
        case 000:	TR(RTS);
            BA = SP; POP;
            PC = cpu->r[df];
            IN(cpu->r[df]);
            SVC;
        case 010: case 020: case 030:
            goto ri;
        case 040: case 050:	TR(CCC); PSW &= ~(cpu->ir&017); SVC;
        case 060: case 070:	TR(SEC); PSW |= cpu->ir&017; SVC;
        }
	case 0300:	TR(SWAB);
		RD_U;
		if(cpu->swab_vbit) {
		    CLCV;   // v-bit cleared, ZQKC compatible
		} else {
		    CLC;    // v-bit unchanged, actual 11/20 behavior
		}
		b = WD(DR & 0377, (DR>>8) & 0377);
		CLNZ; if(b & B7) SEN; if((b & M8) == 0) SEZ;
		WR; SVC;
	}

	/* Operate */
	switch(cpu->ir){
	case 0:	TR(HALT); cpu->state = KA11_STATE_HALTED; return;
	case 1:	TR(WAIT); cpu->state = KA11_STATE_WAITING; return ; // no traps
	case 2:	TR(RTI);
		BA = SP; POP; IN(PC);
		BA = SP; POP; IN(PSW);
		levelchange(cpu->psw) ;
		SVC;
	case 3:	TR(BPT); TRAP(014);
	case 4:	TR(IOT); TRAP(020);
	case 5:	TR(RESET); ka11_reset(cpu); unibone_bus_init() ; SVC;
	}

	// All other instructions should be reserved now

ri:	TRAP(010);
ill:	TRAP(4);
be:	if(cpu->be > 1){
		printf("double bus error, HALT\n");
		trace("double bus error, HALT");
		cpu->state = KA11_STATE_HALTED;
		return;
	}
	trace("bus error at %06o\n", cpu->ba);
	TRAP(4);

trap:
	if (unibone_trace_addr(PC-2)) 
	trace("TRAP %o\n", TV);
	PUSH; OUT(SP, PSW);
	PUSH; OUT(SP, PC);
	INA(TV, PC);
	INA(TV+2, PSW);
	levelchange(PSW);
	/* no trace trap after a trap */
	oldpsw = PSW;

	if (unibone_trace_addr(PC-2)) 
	ka11_tracestate(cpu);
	return;		// TODO: is this correct?
//	SVC;

service:
	c = PSW >> 5;
	if(oldpsw & PSW_T){
		oldpsw &= ~PSW_T;
		TRAP(014);
	}else if(cpu->traps & TRAP_STACK){
		cpu->traps &= ~TRAP_STACK;
		inhov = 1;
		TRAP(4);
	}else if(cpu->traps & TRAP_PWR){
		cpu->traps &= ~TRAP_PWR;
		TRAP(024);
	}else if(c < 7 && cpu->traps & TRAP_BR7){
		cpu->traps &= ~TRAP_BR7;
		TRAP(cpu->br[3].bg(cpu->br[3].dev));
	}else if(c < 6 && cpu->traps & TRAP_BR6){
		cpu->traps &= ~TRAP_BR6;
		TRAP(cpu->br[2].bg(cpu->br[2].dev));
	}else if(c < 5 && cpu->traps & TRAP_BR5){
		cpu->traps &= ~TRAP_BR5;
		TRAP(cpu->br[1].bg(cpu->br[1].dev));
	}else if(c < 4 && cpu->traps & TRAP_BR4){
		cpu->traps &= ~TRAP_BR4;
		TRAP(cpu->br[0].bg(cpu->br[0].dev));
	}else
	// TODO? console stop
		/* fetch next instruction */
		return;
}

// to be called from parallel threads to signal async intr
// (unibusadapter worker thread)
void
ka11_setintr(KA11 *cpu, unsigned vec)
{
	pthread_mutex_lock(&cpu->mutex) ;
	cpu->external_intr = true;
	cpu->external_intrvec = vec;
	trace("INTR vec=%03o\n", vec) ;
//	if (cpu->state == KA11_STATE_WAITING) // atomically
//		cpu->state = KA11_STATE_RUNNING ;
	pthread_mutex_unlock(&cpu->mutex) ;
}

// only to be called from ka11_condstep() thread

void
ka11_pwrfail_trap(KA11 *cpu)
{
	cpu->traps |= TRAP_PWR;
}

// only to be called from ka11_condstep() thread
// if locked, will lock DATI and unibus adapter()!
void
ka11_pwrup_vector_fetch(KA11 *cpu)
{
	// caller must have issued reset()
	// cpu->traps &= ~TRAP_PWR; // no, would be a fix
	INA(024, PC);
	INA(024+2, PSW);
	return ;
be:
	trace("BE\n");
	cpu->be++ ;
}

void
ka11_condstep(KA11 *cpu)
{
	if(cpu->state == KA11_STATE_RUNNING || cpu->state == KA11_STATE_WAITING)
		// GRANT Interrupts before opcode fetch, or when CPU is on WAIT
	unibone_grant_interrupts() ;

	if((cpu->state == KA11_STATE_RUNNING) ||
	   (cpu->state == KA11_STATE_WAITING && cpu->traps)
	   || (cpu->state == KA11_STATE_WAITING && cpu->external_intr) ){
		cpu->state = KA11_STATE_RUNNING;
		// external_intr WAIT handled atomically in ka11_setintr() !

		svc(cpu, cpu->bus);
		step(cpu);
	}
}

void
run(KA11 *cpu)
{
	cpu->state = KA11_STATE_RUNNING;
	
	while(cpu->state != KA11_STATE_HALTED){
		ka11_condstep(cpu);
	}

	ka11_printstate(cpu);
}
