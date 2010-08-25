#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
nouveau-y := nouveau_channel.o nouveau_dma.o nouveau_drv.o nouveau_irq.o \
             nouveau_mem.o nouveau_notifier.o nouveau_object.o nouveau_pscmm.o \
             nouveau_sgdma.o nouveau_dma.o \
             nouveau_sgdma.o nouveau_state.o \
             nv04_fifo.o nv04_timer.o nv50_fb.o \
             nv50_fifo.o nv50_grctx.o nv50_graph.o nv50_instmem.o nv50_mc.o

nouveau-$(CONFIG_DRM_NOUVEAU_DEBUG) += nouveau_debugfs.o
nouveau-$(CONFIG_COMPAT) += nouveau_ioc32.o
nouveau-$(CONFIG_DRM_NOUVEAU_BACKLIGHT) += nouveau_backlight.o
nouveau-$(CONFIG_ACPI) += nouveau_acpi.o

obj-$(CONFIG_DRM_NOUVEAU)+= nouveau.o
