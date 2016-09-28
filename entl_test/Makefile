# Makefile for Earth Computing test harnesses
#
# Author : Mike Mian

# enable bash extensions for build shell
SHELL :=/bin/bash

# include headers for driver api
INCLUDE = ../entl_drivers/e1000e-3.3.4/src/

SRC= entl_test_main.c entl_signal_test_main.c ifconfig.c demo_window.c

OBJS = $(SRC: .c=.o)

TARGETS = entl_test entl_signal_test demo_window demo_web

all: ${TARGETS}

entl_signal_test: entl_signal_test_main.c
	cc -I ${INCLUDE} -o $@ $?

entl_test: entl_test_main.c
	cc -I ${INCLUDE} -o $@ $?

demo_window: demo_window.c
	cc -pthread -lpthread -I ${INCLUDE} -o $@ $?

demo_web: demo_window.c
	cc -pthread -lpthread -I ${INCLUDE} -o $@ $?

clean:
	rm ${TARGETS}
