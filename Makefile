# Makefile for Earth Computing drivers and harnesses
#
# Author : Mike Mian

# enable bash extensions for build shell
SHELL :=/bin/bash

# include headers for driver api
e1000e_dir = entl_drivers/e1000e-3.3.4/src

# test harness
harness_dir = entl_test

TARGETS = e1000e test

e1000e:
	pushd ${e1000e_dir}; \
	make -C /lib/modules/`uname -r`/build M=`pwd` modules; \
	popd

test:
	pushd ${harness_dir}; \
	make all; \
	popd

all: ${TARGETS}

install: e1000e
	 sudo rmmod $?
	 sudo insmod ${${?}_dir}/${?}.ko



clean:
	pushd ${harness_dir}; \
	make clean; \
	popd; \
	pushd ${e1000e_dir}; \
	make clean; \
	popd
