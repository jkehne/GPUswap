kapitest-y := fail.o gamma_set_5.o gamma_set_6.o

obj-m := kapitest.o

EXTRA_CFLAGS = -Iinclude/drm

SYSSRC = /lib/modules/$(shell uname -r)/build

all:
	+make -C $(SYSSRC) M=$(PWD) modules

clean:
	+make -C $(SYSSRC) M=$(PWD) clean
