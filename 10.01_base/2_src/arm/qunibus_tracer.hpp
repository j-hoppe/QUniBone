/* qunibus_tracer.hpp: debugging tool to find and trace sequences of BUS DATI/DATO accesses

 Copyright (c) 2021, Joerg Hoppe
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


 13-feb-2021	JH      created


 Multi stage trigger on DATI/DATO
 triggers after a sequence of events has been met.

 To be insert into CPU code, like a logic analyzer probe.
 Meant to stop emulated CPU before XXDP diags start their printout and
 fill the trace history with UART related code.

 */

#ifndef _QUNIBUS_TRACER_HPP_
#define _QUNIBUS_TRACER_HPP_


#include <vector>
#include <string>


#define TRIGGER_DATI (1 << QUNIBUS_CYCLE_DATI)
#define TRIGGER_DATO (1 << QUNIBUS_CYCLE_DATO)
#define TRIGGER_DATOB (1 << QUNIBUS_CYCLE_DATOB)
#define TRIGGER_DATANY (TRIGGER_DATI | TRIGGER_DATO | TRIGGER_DATOB)

class trigger_condition_c {
public:
    // cycles: OR set of TRIGGER_DAT*
    trigger_condition_c(unsigned address, unsigned cycle_mask) {
        this->address_from = this->address_to = address ;
        this->cycle_mask = cycle_mask ;
    }
    trigger_condition_c(unsigned address_from, unsigned address_to, unsigned cycle_mask) {
        this->address_from = address_from ;
        this->address_to = address_to ;
        this->cycle_mask = cycle_mask ;
    }

    unsigned	address_from ;
    unsigned	address_to ;
    unsigned	cycle_mask ; // multiple of "1 << " QUNIBUS_CYCLE_DATI,DATO,DATOB
    bool matches(uint32_t address, unsigned cycle) {
        return (address >= this->address_from)
               && (address <= this->address_to)
               && (this->cycle_mask & (1 << cycle)) ;
    }
    char *to_string() {
        static char buff[255] ; // how C-ish !
        if (address_from == address_to)
            sprintf(buff, "%06o on ", address_to) ;
        else
            sprintf(buff, "%06o-%06o on ", address_from, address_to) ;
        if (cycle_mask & TRIGGER_DATI)
            strcat(buff, "DATI ") ;
        if (cycle_mask & TRIGGER_DATO)
            strcat(buff, "DATO ") ;
        if (cycle_mask & TRIGGER_DATOB)
            strcat(buff, "DATOB ") ;
        return buff ;
    }
} ;

//is an ordered list of conditions
class trigger_c: std::vector<trigger_condition_c> {
public:
    trigger_c() {
        conditions_clear() ;
    }

    /* defining */
    void conditions_clear() {
        clear() ;
        reset() ;
    }

    // define a multi-level conditions
    void condition_add(trigger_condition_c tc) {
        push_back(tc) ;
    }

    /* to monitor bus activity */
    unsigned	level ; // # of conditions met so far

    // insert into CPU code to monitor bus traffic
    void probe(uint32_t address, uint8_t cycle) {
        if (has_triggered() || size() == 0 )
            return ;
        if (at(level).matches(address,cycle))
            level++ ;
    }

    /* checking */
    // start probing again
    void reset() {
        level = 0 ;
    }

    // check wether all conditions met.
    bool has_triggered(void) {
        return (size() > 0 && level >= size()) ;
    }


    void print(FILE *stream) {
        for(unsigned  i=0 ; i < size() ; i++)
            fprintf(stream, "%d) %s\n", i, at(i).to_string()) ;
    }

} ;


// map for each memory address, wheter to trace or not
class tracer_c {
public:
    bool enabled ; // to be evaluated?
    bool	addr[0x10000] ; // one flag per logical 16 bit address
    tracer_c() {
        clear() ;
    }

    void clear() {
        enabled = false ;
        memset(addr, 0, sizeof(addr)) ;
    }
    // enable address range for tracing
    void enable(uint16_t addr_from, uint16_t addr_to) {
        enabled = true ;
        for (unsigned i=addr_from ; i <= addr_to ; i++)
            addr[i] = true ;
    }
    void disable(uint16_t addr_from, uint16_t addr_to) {
        unsigned i ;
        for (i=addr_from ; i <= addr_to ; i++)
            addr[i] = false ;
        enabled = false ; // any conditions set?
        for (i=0 ;! enabled &&  (i < 0x10000) ; i++)
            if (addr[i])
                enabled = true ;
    }
} ;


#endif
