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

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */
 
#include <linux/console.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nv50_display.h"

#include "drm_pciids.h"

int nouveau_ctxfw = 0;

int nouveau_noagp = 0;		    /* no agp need fix !!!!*/

static int nouveau_modeset = 0; /* kms */

char *nouveau_vbios;

int nouveau_vram_pushbuf = 1;

int nouveau_vram_notify = 1;

int nouveau_duallink = 1;

int nouveau_uscript_lvds = -1;

int nouveau_uscript_tmds = -1;

int nouveau_ignorelid = 0;

int nouveau_noaccel = 0;

int nouveau_nofbaccel = 0;

int nouveau_override_conntype = 0;

int nouveau_tv_disable = 0;

char *nouveau_tv_norm;

int nouveau_reg_debug;

int nouveau_fbpercrtc;

#define PCI_ANY_ID (~0)
#define PCI_BASE_CLASS_DISPLAY		0x03
#define PCI_VENDOR_ID_NVIDIA			0x10de
#define PCI_VENDOR_ID_NVIDIA_SGS	0x12d2


static void *nouveau_statep;

static int nouveau_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int nouveau_attach(dev_info_t *, ddi_attach_cmd_t);
static int nouveau_detach(dev_info_t *, ddi_detach_cmd_t);
static int nouveau_quiesce(dev_info_t *);

extern struct cb_ops drm_cb_ops;

static struct dev_ops nouveau_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	nouveau_info,		/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	nouveau_attach,		/* devo_attach */
	nouveau_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&drm_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	NULL,			/* power */
	nouveau_quiesce,		/* devo_quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,		/* drv_modops */
	"Nouveau DRM driver",	/* drv_linkinfo */
	&nouveau_dev_ops,		/* drv_dev_ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &modldrv, NULL
};



static struct pci_device_id pciidlist[] = {
	{PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16, 0xff << 16, 0},
	{PCI_VENDOR_ID_NVIDIA_SGS, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16, 0xff << 16, 0},
	{}
};

static struct drm_driver driver = {
	.driver_features =
		DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
		DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM,
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
	.reclaim_buffers = drm_core_reclaim_buffers,
	.ioctls = nouveau_ioctls,
	.gem_init_object = nouveau_gem_object_new,
	.gem_free_object = nouveau_gem_object_del,

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

static int
nouveau_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct drm_device *dev;
	int ret, item;

	item = ddi_get_instance(dip);

	switch (cmd) {
	case DDI_ATTACH:
		if (ddi_soft_state_zalloc(nouveau_statep, item) != DDI_SUCCESS) {
			DRM_ERROR("failed to alloc softstate, item = %d", item);
			return (DDI_FAILURE);
		}

		dev = ddi_get_soft_state(nouveau_statep, item);
		if (!dev) {
			DRM_ERROR("cannot get soft state");
			return (DDI_FAILURE);
		}

		dev->devinfo = dip;

		ret = drm_init(dev, &driver);
		if (ret != DDI_SUCCESS)
			(void) ddi_soft_state_free(nouveau_statep, item);

		return (ret);

	case DDI_RESUME:
		dev = ddi_get_soft_state(nouveau_statep, item);
		if (!dev) {
			DRM_ERROR("cannot get soft state");
			return (DDI_FAILURE);
		}
		DRM_ERROR("not supprot resume");
		return (DDI_FAILURE);
	}

	DRM_ERROR("only supports attach or resume");
	return (DDI_FAILURE);
}

static int
nouveau_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct drm_device *dev;
	int item;

	item = ddi_get_instance(dip);
	dev = ddi_get_soft_state(nouveau_statep, item);
	if (!dev) {
		DRM_ERROR("cannot get soft state");
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_DETACH:
		drm_exit(dev);
		(void) ddi_soft_state_free(nouveau_statep, item);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		DRM_ERROR("not support suspend");
		return (DDI_FAILURE);
	}

	DRM_ERROR("only supports detach or suspend");
	return (DDI_FAILURE);
}

static int
nouveau_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	struct drm_minor *minor;

	minor = idr_find(&drm_minors_idr, DRM_DEV2MINOR((dev_t)arg));
	if (!minor)
		return (DDI_FAILURE);
	if (!minor->dev || !minor->dev->devinfo)
		return (DDI_FAILURE);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)minor->dev->devinfo;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)ddi_get_instance(minor->dev->devinfo);
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

static int
nouveau_quiesce(dev_info_t *dip)
{
	struct drm_device *dev;

	dev = ddi_get_soft_state(nouveau_statep, ddi_get_instance(dip));
	if (!dev)
		return (DDI_FAILURE);

	nouveau_irq_uninstall(dev);

	return (DDI_SUCCESS);
}

static int __init nouveau_init(void)
{
	driver.num_ioctls = nouveau_max_ioctl;

//	driver.driver_features |= DRIVER_MODESET;

	return 0;
}

static void __exit nouveau_exit(void)
{

}

int
_init(void)
{
	int ret;

	ret = ddi_soft_state_init(&nouveau_statep,
	    sizeof (struct drm_device), DRM_MAX_INSTANCES);
	if (ret)
		return (ret);

	ret = nouveau_init();
	if (ret) {
		ddi_soft_state_fini(&nouveau_statep);
		return (ret);
	}

	ret = mod_install(&modlinkage);
	if (ret) {
		nouveau_exit();
		ddi_soft_state_fini(&nouveau_statep);
		return (ret);
	}

	return (ret);
}

int
_fini(void)
{
	int ret;

	ret = mod_remove(&modlinkage);
	if (ret)
		return (ret);

	nouveau_exit();

	ddi_soft_state_fini(&nouveau_statep);

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

