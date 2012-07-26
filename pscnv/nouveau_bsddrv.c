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
#include "pscnv_chan.h"
#if 0
#include "nouveau_hw.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nv50_display.h"
#endif

#include "drm_pciids.h"
#include "pscnv_ioctl.h"
#include "pscnv_kapi.h"
#include "device_if.h"

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
int pscnv_mem_debug = 1;
module_param_named(mem_debug, pscnv_mem_debug, int, 0400);

MODULE_PARM_DESC(vm_debug, "VM debug level: 0-2.");
int pscnv_vm_debug = 1;
module_param_named(vm_debug, pscnv_vm_debug, int, 0400);

MODULE_PARM_DESC(ramht_debug, "RAMHT debug level: 0-2.");
int pscnv_ramht_debug = 0;
module_param_named(ramht_debug, pscnv_ramht_debug, int, 0400);

MODULE_PARM_DESC(gem_debug, "GEM debug level: 0-1.");
int pscnv_gem_debug = 1;
module_param_named(gem_debug, pscnv_gem_debug, int, 0400);

int nouveau_fbpercrtc;
#if 0
module_param_named(fbpercrtc, nouveau_fbpercrtc, int, 0400);
#endif

static drm_pci_id_list_t pciidlist[] = {
	{
		/*PCI_VENDOR_ID_NVIDIA*/ 0x10de, 0U, 0, "nVidia graphics card"
	},
	{
		/*PCI_VENDOR_ID_NVIDIA_SGS*/ 0x12d2, 0U, 0, "nVidia SGS graphics card"
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
	/* drm_sleep_locking_init(dev); XXX PLHK */
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
#ifdef DEVICE_AFTER_ATTACH
	DEVMETHOD(device_after_attach,	drm_after_attach),
#endif
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
MODULE_DEPEND(pscnv, drmn, 1, 1, 1);
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

static int
pscnv_gem_pager_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color)
{
	struct drm_gem_object *gem_obj = handle;
	struct drm_device *dev = gem_obj->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_bo *bo = gem_obj->driver_private;
	int ret;
	*color = 0; /* ...? */

	if (!bo->chan && !bo->dmapages && (ret = -dev_priv->vm->map_user(bo)))
		return (ret);

	if (bo->chan)
		pscnv_chan_ref(bo->chan);
	else
		drm_gem_object_reference(gem_obj);

	NV_WARN(dev, "Mapping %p\n", bo);
	return (0);
}

static int
pscnv_gem_pager_fault(vm_object_t vm_obj, vm_ooffset_t offset, int prot,
    vm_page_t *mres)
{
	struct drm_gem_object *gem_obj = vm_obj->handle;
	struct pscnv_bo *bo = gem_obj->driver_private;
	struct drm_device *dev = gem_obj->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	vm_page_t m = NULL;
	vm_page_t oldm;
	vm_memattr_t mattr;
	vm_paddr_t paddr;
	const char *what;

	if (bo->chan) {
		paddr = dev_priv->fb_phys + offset +
			nvc0_fifo_ctrl_offs(dev, bo->chan->cid);
		mattr = VM_MEMATTR_UNCACHEABLE;
		what = "fifo";
	} else switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
	case PSCNV_GEM_VRAM_SMALL:
	case PSCNV_GEM_VRAM_LARGE:
		paddr = dev_priv->fb_phys + bo->map1->start + offset;
		mattr = VM_MEMATTR_WRITE_COMBINING;
		what = "vram";
		break;
	case PSCNV_GEM_SYSRAM_SNOOP:
	case PSCNV_GEM_SYSRAM_NOSNOOP:
		paddr = bo->dmapages[OFF_TO_IDX(offset)];
		mattr = VM_MEMATTR_WRITE_BACK;
		what = "sysram";
		break;
	default: return (EINVAL);
	}

	if (offset >= bo->size) {
		if (pscnv_mem_debug > 0)
			NV_WARN(dev, "Reading %p + %08llx (%s) is past max size %08llx\n",
				bo, offset, what, bo->size);
		return (VM_PAGER_ERROR);
	}
	if (pscnv_mem_debug > 0)
		NV_WARN(dev, "Connecting %p+%08llx (%s) at phys %010llx\n",
			bo, offset, what, paddr);
	vm_object_pip_add(vm_obj, 1);

	if (*mres != NULL) {
		oldm = *mres;
		vm_page_lock(oldm);
		vm_page_remove(oldm);
		vm_page_unlock(oldm);
		*mres = NULL;
	} else
		oldm = NULL;
	//VM_OBJECT_LOCK(vm_obj);
	m = vm_phys_fictitious_to_vm_page(paddr);
	if (m == NULL) {
		return -EFAULT;
	}
	KASSERT((m->flags & PG_FICTITIOUS) != 0,
	    ("not fictitious %p", m));
	KASSERT(m->wire_count == 1, ("wire_count not 1 %p", m));

	if ((m->flags & VPO_BUSY) != 0) {
		return -EFAULT;
	}
	pmap_page_set_memattr(m, mattr);
	m->valid = VM_PAGE_BITS_ALL;
	*mres = m;
	vm_page_lock(m);
	vm_page_insert(m, vm_obj, OFF_TO_IDX(offset));
	vm_page_unlock(m);
	vm_page_busy(m);

	CTR4(KTR_DRM, "fault %p %jx %x phys %x", gem_obj, offset, prot,
	    m->phys_addr);
	//DRM_UNLOCK(dev);
	if (oldm != NULL) {
		vm_page_lock(oldm);
		vm_page_free(oldm);
		vm_page_unlock(oldm);
	}
	vm_object_pip_wakeup(vm_obj);
	return (VM_PAGER_OK);
}

static void
pscnv_gem_pager_dtor(void *handle)
{
	struct drm_gem_object *gem_obj = handle;
	struct pscnv_bo *bo = gem_obj->driver_private;
	struct drm_device *dev = gem_obj->dev;
	vm_object_t devobj = cdev_pager_lookup(handle);

	if (devobj != NULL) {
		vm_size_t page_count = OFF_TO_IDX(bo->size);
		vm_page_t m;
		int i;
		VM_OBJECT_LOCK(devobj);
		for (i = 0; i < page_count; i++) {
			m = vm_page_lookup(devobj, i);
			if (!m)
				continue;
			if (pscnv_mem_debug > 0)
				NV_WARN(dev, "Freeing %010llx + %08llx (%p\n", bo->start, i * PAGE_SIZE, m);
			cdev_pager_free_page(devobj, m);
		}
		VM_OBJECT_UNLOCK(devobj);
		vm_object_deallocate(devobj);
	}
	else {
		NV_ERROR(dev, "Could not find handle %p\n", handle);
		return;
	}
	if (pscnv_mem_debug > 0)
		NV_WARN(dev, "Freed %010llx (%p)\n", bo->start, bo);
	//kfree(bo->fake_pages);

	if (bo->chan)
		pscnv_chan_unref(bo->chan);
	else
		drm_gem_object_unreference_unlocked(gem_obj);
}

static struct cdev_pager_ops pscnv_gem_pager_ops = {
	.cdev_pg_fault	= pscnv_gem_pager_fault,
	.cdev_pg_ctor	= pscnv_gem_pager_ctor,
	.cdev_pg_dtor	= pscnv_gem_pager_dtor
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
	.gem_pager_ops	= &pscnv_gem_pager_ops,

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
