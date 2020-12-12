# PRU_CGT environment variable must point to the TI PRU code gen tools directory. E.g.:
#(Desktop Linux) export PRU_CGT=/path/to/pru/code/gen/tools/ti-cgt-pru_2.1.2
#(Windows) set PRU_CGT=C:/path/to/pru/code/gen/tools/ti-cgt-pru_2.1.2
#(ARM Linux*) export PRU_CGT=/usr/share/ti/cgt-pru
#
# *ARM Linux also needs to create a symbolic link to the /usr/bin/ directory in
# order to use the same Makefile
#(ARM Linux) ln -s /usr/bin/ /usr/share/ti/cgt-pru/bin

ifndef PRU_CGT
define ERROR_BODY

*******************************************************************************
PRU_CGT environment variable is not set. Examples given:
(Desktop Linux) export PRU_CGT=/path/to/pru/code/gen/tools/ti-cgt-pru_2.1.2
(Windows) set PRU_CGT=C:/path/to/pru/code/gen/tools/ti-cgt-pru_2.1.2
(ARM Linux*) export PRU_CGT=/usr/share/ti/cgt-pru

*ARM Linux also needs to create a symbolic link to the /usr/bin/ directory in
order to use the same Makefile
(ARM Linux) ln -s /usr/bin/ /usr/share/ti/cgt-pru/bin
*******************************************************************************

endef
$(error $(ERROR_BODY))
endif

PROJ_NAME=pru0
ifeq ($(QUNIBONE_PLATFORM),UNIBUS)
	OBJ_DIR=$(abspath ../../4_deploy_u)
else ifeq  ($(QUNIBONE_PLATFORM),QBUS)
	OBJ_DIR=$(abspath ../../4_deploy_q)
else
	$(error Set QUNIBONE_PLATFORM to UNIBUS or QBUS!)
endif
SHARED_DIR=$(abspath ../shared)
MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(notdir $(patsubst %/,%,$(dir $(MKFILE_PATH))))
COMMON_SCRIPTS_DIR=$(QUNIBONE_DIR)/90_common/scripts
SUPPORTPACKAGE_DIR := $(QUNIBONE_DIR)/91_3rd_party/pru-c-compile/pru-software-support-package
LINKER_COMMAND_FILE=./AM335x_PRU.cmd
LIBS=--library=$(SUPPORTPACKAGE_DIR)/lib/rpmsg_lib.lib
INCLUDE=--include_path=$(SUPPORTPACKAGE_DIR)/include --include_path=$(SUPPORTPACKAGE_DIR)/include/am335x \
	--include_path=$(SHARED_DIR)
STACK_SIZE=0x100
HEAP_SIZE=0x100

#Common compiler and linker flags (Defined in 'PRU Optimizing C/C++ Compiler User's Guide)
CFLAGS=-v3 -O3 --display_error_number --endian=little --hardware_mac=on --obj_directory=$(OBJ_DIR) --pp_directory=$(OBJ_DIR) -ppd -ppa \
        --c_src_interlist --optimizer_interlist

#Linker flags (Defined in 'PRU Optimizing C/C++ Compiler User's Guide)
LFLAGS=--reread_libs --warn_sections --stack_size=$(STACK_SIZE) --heap_size=$(HEAP_SIZE)

SOURCES=$(wildcard *.c)
# extension is ".asmsrc", not ".asm", do distinguish from compiler-generated .asm files.
SOURCES_ASM=$(wildcard *.asmsrc)
#Using .object instead of .obj in order to not conflict with the CCS build process
OBJECTS_ALL=$(patsubst %,$(OBJ_DIR)/%,$(SOURCES:.c=.object))
OBJECTS_ASM=$(patsubst %,$(OBJ_DIR)/%,$(SOURCES_ASM:.asmsrc=.asmobject))

# all .object files with exceptions of the *main*
OBJECTS_COMMON= \
    $(OBJ_DIR)/pru0_pru_mailbox.object

# rule to print a variable.
# use: make print-VARIALBE
print-%  : ; @echo $* = $($*)


# only interested on the image as C-array.
# Chained builds, so keep *.objects, else recompile.
all: $(OBJECTS_ALL) \
	$(OBJ_DIR)/pru0_code_all.out	\
	$(OBJ_DIR)/pru0_code_all_array.c

# Rule to generate several linked binaries from several main*.c,
# then several C_array files from the linked binary.
# arrayfile <codevariant>_code.c depends on all objects and <codevariant>_main.c
# Example: pru1_main_test.c => obj_dir/pru1_code_test.c
# call with "<path>/make target_pru1_test_code.c"
$(OBJ_DIR)/pru0_code_%.out : $(OBJ_DIR)/pru0_main_%.object $(OBJECTS_COMMON) $(OBJECTS_ASM) $(LINKER_COMMAND_FILE)
	@echo ''
	@echo 'Building binary $@'
	@echo 'Invoking: PRU Linker'
	$(PRU_CGT)/bin/clpru $(CFLAGS) -z -i$(PRU_CGT)/lib -i$(PRU_CGT)/include $(LFLAGS) -o $@  -m$@.map $^ --library=libc.a $(LIBS)
	@echo 'Finished building $@'

# Rule to generate several C-array hex dumps from linked binaries
# hexpru appends "_array" to the base file name
$(OBJ_DIR)/pru0_code_%_array.c : $(OBJ_DIR)/pru0_code_%.out
	@echo 'Generating C-arrays containing binary images with PRU code.'
	( \
	cd $(OBJ_DIR) ; \
	$(PRU_CGT)/bin/hexpru --array $< ; \
	)

# Invokes the compiler on all c files in the directory to create the object files
$(OBJ_DIR)/%.object: %.c
	@mkdir -p $(OBJ_DIR)
	@echo ''
	@echo 'Building file: $<'
	@echo 'Invoking: PRU Compiler'
	$(PRU_CGT)/bin/clpru --include_path=$(PRU_CGT)/include $(INCLUDE) $(CFLAGS) -fe $@ $<
	@echo Produce assembler listing
	$(PRU_CGT)/bin/clpru --include_path=$(PRU_CGT)/include $(INCLUDE) $(CFLAGS) --absolute_listing -fe $@ $<
	mv *.asm $(OBJ_DIR)


# Invokes the compiler on all asm files in the directory to create the object files
$(OBJ_DIR)/%.asmobject: %.asmsrc
#$(OBJ_DIR)/pru1_pru0_datout.o: pru1_pru0_datout.asmsrc
	@mkdir -p $(OBJ_DIR)
	@echo ''
	@echo 'Building file: $<'
	@echo 'Invoking: PRU Compiler'
	$(PRU_CGT)/bin/clpru --asm_listing --asm_file=$^ --output_file=$@
	-mv *.lst $(OBJ_DIR)


.PHONY: all clean

# Remove the $(OBJ_DIR) directory
clean:
	@echo Removing all $(PROJ_NAME)'*' files in the "$(OBJ_DIR)" directory
	@rm -f $(OBJ_DIR)/$(PROJ_NAME)*.*

# Includes the dependencies that the compiler creates (-ppd and -ppa flags)
-include $(OBJECTS_ALL:%.object=%.pp)

