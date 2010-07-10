pscnv-objs := nouveau_drv.o nouveau_ioc32.o nouveau_acpi.o nouveau_state.o \
	     nouveau_irq.o nouveau_debugfs.o nouveau_bios.o nouveau_hw.o \
	     nouveau_i2c.o nouveau_calc.o nouveau_dp.o nouveau_connector.o \
	     nouveau_display.o nouveau_fbcon.o nouveau_dma.o nouveau_backlight.o \
	     nv04_tv.o nv04_dfp.o nv04_dac.o nv04_timer.o \
	     nv17_gpio.o \
	     nv50_gpio.o nv50_grctx.o \
	     nv50_display.o nv50_crtc.o nv50_cursor.o nv50_calc.o nv50_dac.o \
	     nv50_sor.o \
	     pscnv_vram.o pscnv_vm.o pscnv_gem.o pscnv_ramht.o pscnv_chan.o \
	     pscnv_engine.o nv50_fifo.o nv50_graph.o nv50_vm.o

obj-m := pscnv.o

EXTRA_CFLAGS = -Iinclude/drm

all:
	+make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	+make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
