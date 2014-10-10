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

#include <linux/console.h>
#include <linux/version.h>
#include <linux/module.h>

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
#include "pscnv_kapi.h"
#include "pscnv_ioctl.h"
#include "pscnv_client.h"
#include "pscnv_mmap.h"

MODULE_PARM_DESC(agpmode, "AGP mode (0 to disable AGP)");
int nouveau_agpmode = -1;
module_param_named(agpmode, nouveau_agpmode, int, 0400);

MODULE_PARM_DESC(modeset, "Enable kernel modesetting");
static int nouveau_modeset = 0; /* kms */
module_param_named(modeset, nouveau_modeset, int, 0400);

MODULE_PARM_DESC(vbios, "Override default VBIOS location");
char *nouveau_vbios;
module_param_named(vbios, nouveau_vbios, charp, 0400);

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

MODULE_PARM_DESC(tv_norm, "Default TV norm.\n"
		 "\t\tSupported: PAL, PAL-M, PAL-N, PAL-Nc, NTSC-M, NTSC-J,\n"
		 "\t\t\thd480i, hd480p, hd576i, hd576p, hd720p, hd1080i.\n"
		 "\t\tDefault: PAL\n"
		 "\t\t*NOTE* Ignored for cards with external TV encoders.");
char *nouveau_tv_norm;
module_param_named(tv_norm, nouveau_tv_norm, charp, 0400);

MODULE_PARM_DESC(reg_debug, "Register access debug bitmask:\n"
		"\t\t0x1 mc, 0x2 video, 0x4 fb, 0x8 extdev,\n"
		"\t\t0x10 crtc, 0x20 ramdac, 0x40 vgacrtc, 0x80 rmvio,\n"
		"\t\t0x100 vgaattr, 0x200 EVO (G80+). ");
int nouveau_reg_debug;
module_param_named(reg_debug, nouveau_reg_debug, int, 0600);

MODULE_PARM_DESC(perflvl, "Performance level (default: boot)\n");
char *nouveau_perflvl;
module_param_named(perflvl, nouveau_perflvl, charp, 0400);

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

MODULE_PARM_DESC(swapping_debug, "PSCNV swapping debug level: 0-2.");
int pscnv_swapping_debug = 0;
module_param_named(swapping_debug, pscnv_swapping_debug, int, 0400);

MODULE_PARM_DESC(pause_debug, "Pause/ continue debug level: 0-2.");
int pscnv_pause_debug = 0;
module_param_named(pause_debug, pscnv_pause_debug, int, 0400);

MODULE_PARM_DESC(swapin, "Enable the broken swapIN: 0-1");
int pscnv_enable_swapin = 0;
module_param_named(swapin, pscnv_enable_swapin, int, 0400);

MODULE_PARM_DESC(requested_chunk_size, "Chunk size as multiple of 128kB: default 32, use 0 to disable chunking");
/* Note: 128kB is Large Page Table Entry size. With chunk sizes that are not
 * a multiple of 128kB, VRAM_LARGE would be more difficult to implement */
int pscnv_requested_chunk_size = 32;
module_param_named(chunk_size, pscnv_requested_chunk_size, int, 0400);

int nouveau_fbpercrtc;


#ifdef PSCNV_KAPI_DRM_IOCTL_DEF_DRV
static struct drm_ioctl_desc nouveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(PSCNV_GETPARAM, pscnv_ioctl_getparam, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_GEM_NEW, pscnv_ioctl_gem_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_GEM_INFO, pscnv_ioctl_gem_info, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_VSPACE_NEW, pscnv_ioctl_vspace_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_VSPACE_FREE, pscnv_ioctl_vspace_free, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_VSPACE_MAP, pscnv_ioctl_vspace_map, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_VSPACE_UNMAP, pscnv_ioctl_vspace_unmap, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_CHAN_NEW, pscnv_ioctl_chan_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_CHAN_FREE, pscnv_ioctl_chan_free, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_OBJ_VDMA_NEW, pscnv_ioctl_obj_vdma_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_FIFO_INIT, pscnv_ioctl_fifo_init, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_OBJ_ENG_NEW, pscnv_ioctl_obj_eng_new, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_FIFO_INIT_IB, pscnv_ioctl_fifo_init_ib, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PSCNV_COPY_TO_HOST, pscnv_ioctl_copy_to_host, DRM_UNLOCKED),
};
#elif defined(PSCNV_KAPI_DRM_IOCTL_DEF)
static struct drm_ioctl_desc nouveau_ioctls[] = {
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
	DRM_IOCTL_DEF(DRM_PSCNV_COPY_TO_HOST, pscnv_ioctl_copy_to_host, DRM_UNLOCKED),
};
#else
#error "Unknown IOCTLDEF method."
#endif

static struct pci_device_id pciidlist[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID),
		.class = PCI_BASE_CLASS_DISPLAY << 16,
		.class_mask  = 0xff << 16,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA_SGS, PCI_ANY_ID),
		.class = PCI_BASE_CLASS_DISPLAY << 16,
		.class_mask  = 0xff << 16,
	},
	{}
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static struct drm_driver driver;

static int __devinit
nouveau_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
#ifdef PSCNV_KAPI_DRM_GET_DEV
		return drm_get_dev(pdev, ent, &driver);
#else
		return drm_get_pci_dev(pdev, ent, &driver);
#endif
}

static void
nouveau_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

int
nouveau_pci_suspend(struct pci_dev *pdev, pm_message_t pm_state)
{
	return -ENODEV;
}

int
nouveau_pci_resume(struct pci_dev *pdev)
{
	return -ENODEV;
}

#ifndef PSCNV_KAPI_DRM_DRIVER_FOPS
static const struct file_operations nouveau_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = pscnv_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
#ifdef CONFIG_COMPAT
	.compat_ioctl = nouveau_compat_ioctl,
#endif
#ifdef PSCNV_KAPI_NOOP_LLSEEK
	.llseek = noop_llseek,
#endif
};
#endif

static struct drm_driver driver = {
	.driver_features =
		DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
		DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM,
	.load = nouveau_load,
	.firstopen = nouveau_firstopen,
	.lastclose = nouveau_lastclose,
	.open = pscnv_client_open,
	.postclose = pscnv_client_postclose,
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
	.reclaim_buffers = drm_core_reclaim_buffers,
#ifdef PSCNV_KAPI_MAP_OFS
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
#endif
	.ioctls = nouveau_ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(nouveau_ioctls),

#ifdef PSCNV_KAPI_DRM_DRIVER_FOPS
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.unlocked_ioctl = drm_ioctl,
		.mmap = pscnv_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
#if defined(CONFIG_COMPAT)
		.compat_ioctl = nouveau_compat_ioctl,
#endif
	},
#else
	.fops = &nouveau_driver_fops,
#endif

#ifdef PSCNV_KAPI_DRM_INIT
	.pci_driver = {
		.name = DRIVER_NAME,
		.id_table = pciidlist,
		.probe = nouveau_pci_probe,
		.remove = nouveau_pci_remove,
		.suspend = nouveau_pci_suspend,
		.resume = nouveau_pci_resume
	},
#endif

	.gem_close_object = pscnv_gem_close_object,
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

#ifndef PSCNV_KAPI_DRM_INIT
static struct pci_driver nouveau_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = nouveau_pci_probe,
	.remove = nouveau_pci_remove,
	.suspend = nouveau_pci_suspend,
	.resume = nouveau_pci_resume
};
#endif

static int __init nouveau_init(void)
{
	if (nouveau_modeset == -1) {
#ifdef CONFIG_VGA_CONSOLE
		if (vgacon_text_force())
			nouveau_modeset = 0;
		else
#endif
			nouveau_modeset = 1;
	}

	if (nouveau_modeset == 1) {
		driver.driver_features |= DRIVER_MODESET;
#if defined(CONFIG_FRAMEBUFFER_CONSOLE_MODULE)
		request_module("fbcon");
#elif !defined(CONFIG_FRAMEBUFFER_CONSOLE)
		printk(KERN_INFO "CONFIG_FRAMEBUFFER_CONSOLE was not enabled. You won't get any console output.\n");
#endif
		nouveau_register_dsm_handler();
	}

#ifdef PSCNV_KAPI_DRM_INIT
	return drm_init(&driver);
#else
	return drm_pci_init(&driver, &nouveau_pci_driver);
#endif
}

static void __exit nouveau_exit(void)
{
#ifdef PSCNV_KAPI_DRM_INIT
	drm_exit(&driver);
#else
	drm_pci_exit(&driver, &nouveau_pci_driver);
#endif
	nouveau_unregister_dsm_handler();
}

module_init(nouveau_init);
module_exit(nouveau_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
