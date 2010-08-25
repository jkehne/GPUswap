/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_mem.h"
#include "pscnv_vm.h"
#include "pscnv_ramht.h"
#include "pscnv_chan.h"
#include "nv50_chan.h"

struct pscnv_chan *
pscnv_chan_new (struct pscnv_vspace *vs, int fake) {
	struct pscnv_chan *res = kzalloc(sizeof *res, GFP_KERNEL);
	if (!res)
		return 0;
	if (fake)
		res->cid = -fake;
	else
		res->cid = 0;
	mutex_lock(&vs->lock);
	res->vspace = vs;
	kref_get(&vs->ref);
	spin_lock_init(&res->instlock);
	spin_lock_init(&res->ramht.lock);
	kref_init(&res->ref);
	list_add(&res->vspace_list, &vs->chan_list);

	if (nv50_chan_new (res)) {
		list_del(&res->vspace_list);
		mutex_unlock(&vs->lock);
		kfree(res);
		return 0;
	}

	mutex_unlock(&vs->lock);
	return res;
}

void
pscnv_chan_free(struct pscnv_chan *ch) {
	struct drm_nouveau_private *dev_priv = ch->vspace->dev->dev_private;
	if (ch->cid) {
		int i;
		for (i = 0; i < PSCNV_ENGINES_NUM; i++)
			if (ch->engdata[i]) {
				struct pscnv_engine *eng = dev_priv->engines[i];
				eng->chan_kill(eng, ch);
				eng->chan_free(eng, ch);
			}
	}
	mutex_lock(&ch->vspace->lock);
	list_del(&ch->vspace_list);
	mutex_unlock(&ch->vspace->lock);
	if (ch->cache)
		pscnv_mem_free(ch->cache);
	pscnv_mem_free(ch->bo);
	kref_put(&ch->vspace->ref, pscnv_vspace_ref_free);
	kfree(ch);
}

/* needs vm_mutex held */
struct pscnv_chan *
pscnv_get_chan(struct drm_device *dev, struct drm_file *file_priv, int cid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (cid < 128 && cid >= 0 && dev_priv->chans[cid] && dev_priv->chans[cid]->filp == file_priv) {
		return dev_priv->chans[cid];
	}
	return 0;
}

int pscnv_ioctl_chan_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_chan_new *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cid = -1;
	struct pscnv_vspace *vs;
	struct pscnv_chan *ch;
	int i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	vs = pscnv_get_vspace(dev, file_priv, req->vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	for (i = 1; i < 128; i++)
		if (!dev_priv->chans[i]) {
			cid = i;
			break;
		}

	if (cid == -1) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOSPC;
	}

	ch = dev_priv->chans[cid] = pscnv_chan_new(vs, 0);
	if (!dev_priv->chans[cid]) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOMEM;
	}
	ch->cid = cid;

	ch->filp = file_priv;
	
	req->cid = cid;
	req->map_handle = 0xc0000000 | cid << 16;

	nv50_chan_init(ch);

	NV_INFO(dev, "Allocating FIFO %d\n", cid);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

static void pscnv_chan_ref_free(struct kref *ref) {
	struct pscnv_chan *ch = container_of(ref, struct pscnv_chan, ref);
	int cid = ch->cid;
	struct drm_nouveau_private *dev_priv = ch->vspace->dev->dev_private;

	NV_INFO(ch->vspace->dev, "Freeing FIFO %d\n", cid);

	pscnv_chan_free(ch);

	dev_priv->chans[cid] = 0;
}

int pscnv_ioctl_chan_free(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_chan_free *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cid = req->cid;
	struct pscnv_chan *ch;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);
	ch = pscnv_get_chan(dev, file_priv, cid);
	if (!ch) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	ch->filp = 0;
	kref_put(&ch->ref, pscnv_chan_ref_free);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_obj_vdma_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_obj_vdma_new *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	int ret;
	uint32_t oclass, inst;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	oclass = req->oclass;

	if (oclass != 2 && oclass != 3 && oclass != 0x3d)
		return -EINVAL;

	mutex_lock (&dev_priv->vm_mutex);

	ch = pscnv_get_chan(dev, file_priv, req->cid);
	if (!ch) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	inst = nv50_chan_dmaobj_new(ch, 0x7fc00000 | oclass, req->start, req->size);
	if (!inst) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOMEM;
	}

	ret = pscnv_ramht_insert (&ch->ramht, req->handle, inst >> 4);

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}

static void pscnv_chan_vm_open(struct vm_area_struct *vma) {
	struct pscnv_chan *ch = vma->vm_private_data;
	kref_get(&ch->ref);
}

static void pscnv_chan_vm_close(struct vm_area_struct *vma) {
	struct pscnv_chan *ch = vma->vm_private_data;
	struct drm_nouveau_private *dev_priv = ch->vspace->dev->dev_private;
	mutex_lock (&dev_priv->vm_mutex);
	kref_put(&ch->ref, pscnv_chan_ref_free);
	mutex_unlock (&dev_priv->vm_mutex);
}

static struct vm_operations_struct pscnv_chan_vm_ops = {
	.open = pscnv_chan_vm_open,
	.close = pscnv_chan_vm_close,
};	

int pscnv_chan_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cid;
	struct pscnv_chan *ch;

	if ((vma->vm_pgoff * PAGE_SIZE & ~0x7f0000ull) == 0xc0000000) {
		if (vma->vm_end - vma->vm_start > 0x2000)
			return -EINVAL;
		mutex_lock (&dev_priv->vm_mutex);
		cid = (vma->vm_pgoff * PAGE_SIZE >> 16) & 0x7f;
		ch = pscnv_get_chan(dev, filp->private_data, cid);
		if (!ch) {
			mutex_unlock (&dev_priv->vm_mutex);
			return -ENOENT;
		}
		kref_get(&ch->ref);
		mutex_unlock (&dev_priv->vm_mutex);

		vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
		vma->vm_ops = &pscnv_chan_vm_ops;
		vma->vm_private_data = ch;
		return remap_pfn_range(vma, vma->vm_start, 
			(dev_priv->mmio_phys + 0xc00000 + cid * 0x2000) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
	}
	return -EINVAL;
}

void pscnv_chan_cleanup(struct drm_device *dev, struct drm_file *file_priv) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cid;
	struct pscnv_chan *ch;

	mutex_lock (&dev_priv->vm_mutex);
	for (cid = 0; cid < 128; cid++) {
		ch = pscnv_get_chan(dev, file_priv, cid);
		if (!ch)
			continue;
		ch->filp = 0;
		kref_put(&ch->ref, pscnv_chan_ref_free);
	}
	mutex_unlock (&dev_priv->vm_mutex);
}
