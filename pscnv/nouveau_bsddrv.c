/*
 * Copyright 2005 Stephane Marchesin.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nouveau_drv.h"
#include "drm.h"
#include "drm_crtc_helper.h"
#include "nouveau_pm.h"
#include "pscnv_gem.h"
#include "pscnv_vm.h"
#if 0
#include "nouveau_hw.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nv50_display.h"
#endif

#include "drm_pciids.h"
#include "pscnv_ioctl.h"
#include "pscnv_kapi.h"

#define MODULE_PARM_DESC(a, b)
#define module_param_named(name, where, type, mode) TUNABLE_INT("drm.pscnv." #name, &where)
#define module_param_str(name, where) TUNABLE_STR("drm.pscnv." #name , where, sizeof(where))

MODULE_PARM_DESC(agpmode, "AGP mode (0 to disable AGP)");
int nouveau_agpmode = -1;
module_param_named(agpmode, nouveau_agpmode, int, 0400);

MODULE_PARM_DESC(modeset, "Enable kernel modesetting");
static int nouveau_modeset = 1; /* kms */
module_param_named(modeset, nouveau_modeset, int, 0400);

MODULE_PARM_DESC(vbios, "Override default VBIOS location");
char nouveau_vbios_array[32];
char *nouveau_vbios = nouveau_vbios_array;
module_param_str(vbios, nouveau_vbios_array);

MODULE_PARM_DESC(duallink, "Allow dual-link TMDS (>=GeForce 8)");
int nouveau_duallink = 1;
module_param_named(duallink, nouveau_duallink, int, 0400);

MODULE_PARM_DESC(uscript_lvds, "LVDS output script table ID (>=GeForce 8)");
int nouveau_uscript_lvds = -1;
module_param_named(uscript_lvds, nouveau_uscript_lvds, int, 0400);

MODULE_PARM_DESC(uscript_tmds, "TMDS output script table ID (>=GeForce 8)");
int nouveau_uscript_tmds = -1;
module_param_named(uscript_tmds, nouveau_uscript_tmds, int, 0400);

MODULE_PARM_DESC(ignorelid, "Ignore ACPI lid status");
int nouveau_ignorelid = 0;
module_param_named(ignorelid, nouveau_ignorelid, int, 0400);

MODULE_PARM_DESC(noaccel, "Disable all acceleration");
int nouveau_noaccel = 0;
module_param_named(noaccel, nouveau_noaccel, int, 0400);

MODULE_PARM_DESC(nofbaccel, "Disable fbcon acceleration");
int nouveau_nofbaccel = 0;
module_param_named(nofbaccel, nouveau_nofbaccel, int, 0400);

MODULE_PARM_DESC(force_post, "Force POST");
int nouveau_force_post = 0;
module_param_named(force_post, nouveau_force_post, int, 0400);

MODULE_PARM_DESC(override_conntype, "Ignore DCB connector type");
int nouveau_override_conntype = 0;
module_param_named(override_conntype, nouveau_override_conntype, int, 0400);

MODULE_PARM_DESC(tv_disable, "Disable TV-out detection\n");
int nouveau_tv_disable = 0;
module_param_named(tv_disable, nouveau_tv_disable, int, 0400);

MODULE_PARM_DESC(reg_debug, "Register access debug bitmask:\n"
		"\t\t0x1 mc, 0x2 video, 0x4 fb, 0x8 extdev,\n"
		"\t\t0x10 crtc, 0x20 ramdac, 0x40 vgacrtc, 0x80 rmvio,\n"
		"\t\t0x100 vgaattr, 0x200 EVO (G80+). ");
int nouveau_reg_debug;
module_param_named(reg_debug, nouveau_reg_debug, int, 0600);

MODULE_PARM_DESC(perflvl, "Performance level (default: boot)\n");
static char nouveau_perflvl_array[32];
char *nouveau_perflvl = nouveau_perflvl_array;
module_param_str(perflvl, nouveau_perflvl_array);

MODULE_PARM_DESC(perflvl_wr, "Allow perflvl changes (warning: dangerous!)\n");
int nouveau_perflvl_wr;
module_param_named(perflvl_wr, nouveau_perflvl_wr, int, 0400);

MODULE_PARM_DESC(mm_debug, "mm debug level: 0-2.");
int pscnv_mm_debug = 0;
module_param_named(mm_debug, pscnv_mm_debug, int, 0400);

MODULE_PARM_DESC(mem_debug, "memory debug level: 0-1.");
int pscnv_mem_debug = 0;
module_param_named(mem_debug, pscnv_mem_debug, int, 0400);

MODULE_PARM_DESC(vm_debug, "VM debug level: 0-2.");
int pscnv_vm_debug = 0;
module_param_named(vm_debug, pscnv_vm_debug, int, 0400);

MODULE_PARM_DESC(ramht_debug, "RAMHT debug level: 0-2.");
int pscnv_ramht_debug = 0;
module_param_named(ramht_debug, pscnv_ramht_debug, int, 0400);

MODULE_PARM_DESC(gem_debug, "GEM debug level: 0-1.");
int pscnv_gem_debug = 0;
module_param_named(gem_debug, pscnv_gem_debug, int, 0400);

int nouveau_fbpercrtc;
#if 0
module_param_named(fbpercrtc, nouveau_fbpercrtc, int, 0400);
#endif

static drm_pci_id_list_t pciidlist[] = {
	{
		/*PCI_VENDOR_ID_NVIDIA*/ 0x10de, ~0U
	},
	{
		/*PCI_VENDOR_ID_NVIDIA_SGS*/ 0x12d2, ~0U
	},
	{}
};

static struct drm_driver_info driver;

static int
pscnv_attach(device_t kdev)
{
	struct drm_device *dev;
	dev = device_get_softc(kdev);
	if (nouveau_modeset == 1)
		driver.driver_features |= DRIVER_MODESET;
	dev->driver = &driver;
	drm_sleep_locking_init(dev);
	return (drm_attach(kdev, pciidlist));
}


static int
pscnv_probe(device_t kdev)
{
	return drm_probe(kdev, pciidlist);
}

static int
nouveau_suspend(device_t kdev)
{
	return (ENXIO);
}

static int
nouveau_resume(device_t kdev)
{
	return (ENXIO);
}

static device_method_t pscnv_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pscnv_probe),
	DEVMETHOD(device_attach,	pscnv_attach),
	DEVMETHOD(device_suspend,	nouveau_suspend),
	DEVMETHOD(device_resume,	nouveau_resume),
	DEVMETHOD(device_detach,	drm_detach),
	DEVMETHOD_END
};

static driver_t pscnv_driver = {
	"drm",
	pscnv_methods,
	sizeof(struct drm_device)
};

extern devclass_t drm_devclass;
DRIVER_MODULE_ORDERED(pscnv, vgapci, pscnv_driver, drm_devclass, 0, 0,
    SI_ORDER_ANY);
MODULE_DEPEND(pscnv, drm, 1, 1, 1);
MODULE_DEPEND(pscnv, agp, 1, 1, 1);
MODULE_DEPEND(pscnv, iicbus, 1, 1, 1);
MODULE_DEPEND(pscnv, iic, 1, 1, 1);
MODULE_DEPEND(pscnv, iicbb, 1, 1, 1);

struct drm_ioctl_desc nouveau_ioctls[] = {
	DRM_IOCTL_DEF(DRM_PSCNV_GETPARAM, pscnv_ioctl_getparam, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_GEM_NEW, pscnv_ioctl_gem_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_GEM_INFO, pscnv_ioctl_gem_info, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_VSPACE_NEW, pscnv_ioctl_vspace_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_VSPACE_FREE, pscnv_ioctl_vspace_free, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_VSPACE_MAP, pscnv_ioctl_vspace_map, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_VSPACE_UNMAP, pscnv_ioctl_vspace_unmap, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_CHAN_NEW, pscnv_ioctl_chan_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_CHAN_FREE, pscnv_ioctl_chan_free, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_OBJ_VDMA_NEW, pscnv_ioctl_obj_vdma_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_FIFO_INIT, pscnv_ioctl_fifo_init, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_OBJ_ENG_NEW, pscnv_ioctl_obj_eng_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_PSCNV_FIFO_INIT_IB, pscnv_ioctl_fifo_init_ib, DRM_UNLOCKED),
};

static struct drm_driver_info driver = {
	.driver_features =
		DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG | DRIVER_USE_MTRR |
		DRIVER_HAVE_IRQ | DRIVER_LOCKLESS_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM,
	.load = nouveau_load,
	.firstopen = nouveau_firstopen,
	.lastclose = nouveau_lastclose,
	.unload = nouveau_unload,
	.preclose = nouveau_preclose,
#if defined(CONFIG_DRM_NOUVEAU_DEBUG)
	.debugfs_init = nouveau_debugfs_init,
	.debugfs_cleanup = nouveau_debugfs_takedown,
#endif
	.irq_preinstall = nouveau_irq_preinstall,
	.irq_postinstall = nouveau_irq_postinstall,
	.irq_uninstall = nouveau_irq_uninstall,
	.irq_handler = nouveau_irq_handler,
	.ioctls = nouveau_ioctls,
	.max_ioctl = DRM_ARRAY_SIZE(nouveau_ioctls),

	// .gem_init_object ?
	// .dumb_* ?
	// sysctl init/cleanup ?
	.gem_free_object = pscnv_gem_free_object,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
#ifdef GIT_REVISION
	.date = GIT_REVISION,
#else
	.date = DRIVER_DATE,
#endif
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};
