#ccflags-y+=-Wfatal-errors
#ccflags-y+=-include $M/ctracer.h
#ccflags-y+=-D CTRACER_ON
obj-m += ubq_core.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
EXTRA_CFLAGS += -g -fms-extensions
CROSS_COMPILE ?= 

all:	modules

ubq_core-y := core.o gadget.o driver.o com.o com_udp.o debug.o debug_usb.o common.o msg.o

modules:
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$$PWD modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$$PWD clean
