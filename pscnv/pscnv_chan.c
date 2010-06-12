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
#include "pscnv_vram.h"
#include "pscnv_vm.h"
#include "pscnv_ramht.h"
#include "pscnv_chan.h"

struct pscnv_chan *
pscnv_chan_new (struct pscnv_vspace *vs) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	struct pscnv_chan *res = kzalloc(sizeof *res, GFP_KERNEL);
	uint64_t size;
	uint32_t chan_pd;
	int i;
	if (!res)
		return 0;
	mutex_lock(&vs->lock);
	res->isbar = vs->isbar;
	res->vspace = vs;
	spin_lock_init(&res->instlock);
	spin_lock_init(&res->ramht.lock);
	kref_init(&res->ref);
	list_add(&res->vspace_list, &vs->chan_list);

	/* determine size of underlying VO... for normal channels,
	 * allocate 64kiB since they have to store the objects
	 * heap. for the BAR fake channel, we'll only need two objects,
	 * so keep it minimal
	 */
	if (!res->isbar)
		size = 0x10000;
	else if (dev_priv->chipset == 0x50)
		size = 0x6000;
	else
		size = 0x5000;
	res->vo = pscnv_vram_alloc(vs->dev, size, PSCNV_VO_CONTIG,
			0, (res->isbar ? 0xc5a2ba7 : 0xc5a2f1f0));

	if (!vs->isbar)
		pscnv_vspace_map3(res->vo);

	if (dev_priv->chipset == 0x50)
		chan_pd = NV50_CHAN_PD;
	else
		chan_pd = NV84_CHAN_PD;
	for (i = 0; i < NV50_VM_PDE_COUNT; i++) {
		if (vs->pt[i]) {
			nv_wv32(res->vo, chan_pd + i * 8, vs->pt[i]->start >> 32);
			nv_wv32(res->vo, chan_pd + i * 8 + 4, vs->pt[i]->start | 0x3);
		} else {
			nv_wv32(res->vo, chan_pd + i * 8, 0);
		}
	}
	res->instpos = chan_pd + NV50_VM_PDE_COUNT * 8;

	if (!res->isbar) {
		int i;
		res->ramht.vo = res->vo;
		res->ramht.bits = 9;
		res->ramht.offset = pscnv_chan_iobj_new(res, 8 << res->ramht.bits);
		for (i = 0; i < (8 << res->ramht.bits); i += 8)
			nv_wv32(res->ramht.vo, res->ramht.offset + i + 4, 0);

		if (dev_priv->chipset == 0x50) {
			res->ramfc = 0;
		} else {
			/* actually, addresses of these two are NOT relative to
			 * channel struct on NV84+, and can be anywhere in VRAM,
			 * but we stuff them inside the channel struct anyway for
			 * simplicity. */
			res->ramfc = pscnv_chan_iobj_new(res, 0x100);
			res->cache = pscnv_vram_alloc(vs->dev, 0x1000, PSCNV_VO_CONTIG,
					0, 0xf1f0cace);
		}
	}

	mutex_unlock(&vs->lock);
	return res;
}

void
pscnv_chan_free(struct pscnv_chan *ch) {
	/* XXX: write me */
}

int
pscnv_chan_iobj_new(struct pscnv_chan *ch, uint32_t size) {
	/* XXX: maybe do this "properly" one day?
	 *
	 * Why we don't implement _del for instance objects:
	 *  - Usually, bounded const number of them is allocated
	 *    for any given channel, and the used set doesn't change
	 *    much during channel's lifetime
	 *  - Since instance objects are stored inside the main
	 *    VO of the channel, the storage will be freed on channel
	 *    close anyway
	 *  - We cannot easily tell what objects are currently in use
	 *    by PGRAPH and maybe other execution engines -- the user
	 *    could cheat us. Caching doesn't help either.
	 */
	int res;
	size += 0xf;
	size &= ~0xf;
	spin_lock(&ch->instlock);
	if (ch->instpos + size > ch->vo->size) {
		spin_unlock(&ch->instlock);
		return 0;
	}
	res = ch->instpos;
	ch->instpos += size;
	spin_unlock(&ch->instlock);
	return res;
}

/* XXX: we'll possibly want to break down type and/or add mysterious flags5
 * when we know more. */
int
pscnv_chan_dmaobj_new(struct pscnv_chan *ch, uint32_t type, uint64_t start, uint64_t size) {
	uint64_t end = start + size - 1;
	int res = pscnv_chan_iobj_new (ch, 0x18);
	if (!res)
		return 0;
	nv_wv32(ch->vo, res + 0x00, type);
	nv_wv32(ch->vo, res + 0x04, end);
	nv_wv32(ch->vo, res + 0x08, start);
	nv_wv32(ch->vo, res + 0x0c, (end >> 32) << 24 | (start >> 32));
	nv_wv32(ch->vo, res + 0x10, 0);
	nv_wv32(ch->vo, res + 0x14, 0);
	return res;
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

	ch = dev_priv->chans[cid] = pscnv_chan_new(vs);
	if (!dev_priv->chans[i]) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOMEM;
	}

	dev_priv->chans[cid]->filp = file_priv;
	
	req->cid = cid;
	req->map_handle = 0xc0000000 | cid << 16;

	if (dev_priv->chipset != 0x50) {
		nv_wr32(dev, 0x2600 + cid * 4, (ch->vo->start + ch->ramfc) >> 8);
	} else {
		nv_wr32(dev, 0x2600 + cid * 4, ch->vo->start >> 12);
	}

	NV_INFO(dev, "Allocating FIFO %d\n", cid);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

static void pscnv_chan_ref_free(struct kref *ref) {
	struct pscnv_chan *ch = container_of(ref, struct pscnv_chan, ref);
	int cid = ch->cid;
	struct drm_nouveau_private *dev_priv = ch->vspace->dev->dev_private;

	/* XXX */
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

	inst = pscnv_chan_dmaobj_new(ch, 0x7fc00000 | oclass, req->start, req->start + req->size);
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
