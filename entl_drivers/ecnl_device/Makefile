KDIR := /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

EXTRA_CFLAGS += -I$(PWD)/../e1000e-3.4.0.2/src/ec_device
generated-y += -I$(PWD)/../e1000e-3.4.0.2/src/ec_device
obj-m += ecnl_device.o

all:
	echo $(EXTRA_CFLAGS)
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf *.o *.ko *.mod.* *.cmd .module* modules* Module* .*.cmd .tmp*
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean