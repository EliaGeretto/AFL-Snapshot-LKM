ifneq ($(KERNELRELEASE),)

obj-m += afl_snapshot.o
afl_snapshot-objs := memory.o files.o threads.o task_data.o snapshot.o hook.o module.o

ccflags-y := -I $(src)/../include
ifneq ($(DEBUG),)
  ccflags-y += -g -DDEBUG
endif

else

KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: modules modules_install clean help

modules:
	$(MAKE) -C $(KDIR) M=$$PWD DEBUG=$(DEBUG) modules

modules_install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean

help:
	$(MAKE) -C $(KDIR) M=$$PWD help

endif