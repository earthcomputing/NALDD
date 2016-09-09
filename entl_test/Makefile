# enable bash extensions for build shell
SHELL :=/bin/bash

# include headers for driver api
INCLUDE = ../drivers/net/ethernet/intel/e1000e
SRC = entl_test_main.c entl_signal_test_main.c ifconfig.c
OBJS = $(SRC:.c=.o)
TARGETS = entl_test entl_signal_test

#entl_test: ${$@}_main.c
#	cc -I ${INCLUDE} -o $@ entl_test_main.o 

all: ${TARGETS}

entl_test: entl_test_main.c
	cc -I ${INCLUDE} -o $@ $?

entl_signal_test: entl_signal_test_main.c
	cc -I ${INCLUDE} -o $@ $? 
clean:
	rm ${TARGETS}

