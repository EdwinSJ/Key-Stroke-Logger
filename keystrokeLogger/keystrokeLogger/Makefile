CFLAGS_kisni.o := -DDEBUG

obj-m += keystrokelogger.o

kisni-objs := keystrokelogger.o

KERNELVERSION = $(shell uname -r)
KDIR := /lib/modules/$(KERNELVERSION)/build

all:	
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
