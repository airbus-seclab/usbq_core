#ccflags-y+=-Wfatal-errors
#ccflags-y+=-include $M/ctracer.h
#ccflags-y+=-D CTRACER_ON
obj-m += ubq_core.o

KERNELDIR ?= /home/ben64/Documents/eads/projects/2015/usb/beagleboard/linux
EXTRA_CFLAGS += -g -fms-extensions

all:	modules

ubq_core-y := core.o gadget.o driver.o com.o com_udp.o debug.o debug_usb.o common.o msg.o

modules:
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -C $(KERNELDIR) M=$$PWD modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$$PWD clean
