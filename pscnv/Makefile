pscnv-objs := nouveau_drv.o nouveau_ioc32.o nouveau_acpi.o nouveau_state.o \
	     nouveau_irq.o nouveau_debugfs.o nouveau_bios.o nouveau_hw.o \
	     nouveau_i2c.o nouveau_calc.o nouveau_dp.o nouveau_connector.o \
	     nv04_tv.o nv04_dfp.o nv04_dac.o nv04_timer.o \
	     nv17_gpio.o \
	     nv50_gpio.o nv50_crtc.o \
	     pscnv_vram.o

obj-m := pscnv.o

EXTRA_CFLAGS = -Iinclude/drm

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
