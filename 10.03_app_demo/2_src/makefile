# Main switch between UNIBUS and QBUS makefile

ifeq ($(MAKE_QUNIBUS),UNIBUS)
        include makefile_u
else ifeq  ($(MAKE_QUNIBUS),QBUS)
        include makefile_q
else
	$(error Set MAKE_QUNIBUS to UNIBUS or QBUS!)
endif
