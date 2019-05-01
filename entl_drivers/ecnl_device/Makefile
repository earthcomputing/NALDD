CONFIG_MODULE_SIG=n
KDIR := /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

ccflags-y += -std=gnu99
ccflags-y += -Wno-declaration-after-statement
ccflags-y += -Wno-unused-variable
ccflags-y += -Wno-unused-function

#EXTRA_CFLAGS += -I$(PWD)/../e1000e-3.4.0.2/src/ec_device
#generated-y += -I$(PWD)/../e1000e-3.4.0.2/src/ec_device

EXTRA_CFLAGS += -I$(src)/../e1000e-3.3.4/src
generated-y += -I$(src)/../e1000e-3.3.4/src

obj-m += ecnl_device.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf *.o *.ko *.mod.* *.cmd .module* modules* Module* .*.cmd .tmp*
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
