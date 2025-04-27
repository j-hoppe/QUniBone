# Main switch between UNIBUS and QBUS makefile

ifeq ($(QUNIBONE_PLATFORM),UNIBUS)
include makefile_u
else ifeq  ($(QUNIBONE_PLATFORM),QBUS)
include makefile_q
else
$(error Set QUNIBONE_PLATFORM to UNIBUS or QBUS!)
endif
