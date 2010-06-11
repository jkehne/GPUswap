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
#include "pscnv_chan.h"

#undef PSCNV_RB_AUGMENT

static void PSCNV_RB_AUGMENT(struct pscnv_vm_mapnode *node) {
	uint64_t maxgap = 0;
	struct pscnv_vm_mapnode *left = PSCNV_RB_LEFT(node, entry);
	struct pscnv_vm_mapnode *right = PSCNV_RB_RIGHT(node, entry);
	if (!node->vo)
		maxgap = node->size;
	if (left && left->maxgap > maxgap)
		maxgap = left->maxgap;
	if (right && right->maxgap > maxgap)
		maxgap = right->maxgap;
	node->maxgap = maxgap;
}

static int mapcmp(struct pscnv_vm_mapnode *a, struct pscnv_vm_mapnode *b) {
	if (a->start < b->start)
		return -1;
	else if (a->start > b->start)
		return 1;
	return 0;
}

PSCNV_RB_GENERATE_STATIC(pscnv_vm_maptree, pscnv_vm_mapnode, entry, mapcmp)

static int
pscnv_vspace_flush(struct pscnv_vspace *vs, int unit) {
	nv_wr32(vs->dev, 0x100c80, unit << 16 | 1);
	if (!nouveau_wait_until(vs->dev, 2000000000ULL, 0x100c80, 1, 0)) {
		NV_ERROR(vs->dev, "TLB flush fail on unit %d!\n", unit);
		return -EIO;
	}
	return 0;
}

static int
pscnv_vspace_do_unmap (struct pscnv_vspace *vs, uint64_t offset, uint64_t length) {
	int ret;
	while (length) {
		uint32_t pgnum = offset / 0x1000;
		uint32_t pdenum = pgnum / NV50_VM_SPTE_COUNT;
		uint32_t ptenum = pgnum % NV50_VM_SPTE_COUNT;
		if (vs->pt[pdenum]) {
			nv_wv32(vs->pt[pdenum], ptenum * 8, 0);
		}
		offset += 0x1000;
	}
	/* XXX: determine which flushes we need here. */
	if (vs->isbar) {
		return pscnv_vspace_flush(vs, 6);
	} else {
		ret = pscnv_vspace_flush(vs, 5);
		if (ret)
			return ret;
		if (vs->engines & PSCNV_ENGINE_PGRAPH) {
			ret = pscnv_vspace_flush(vs, 0);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int
pscnv_vspace_fill_pd_slot (struct pscnv_vspace *vs, uint32_t pdenum) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	struct list_head *pos;
	int i;
	uint32_t chan_pd;
	vs->pt[pdenum] = pscnv_vram_alloc(vs->dev, NV50_VM_SPTE_COUNT * 8, PSCNV_VO_CONTIG, 0, 0xa9e7ab1e);
	if (!vs->pt[pdenum]) {
		return -ENOMEM;
	}

	if (!vs->isbar)
		pscnv_vspace_map3(vs->pt[pdenum]);

	for (i = 0; i < NV50_VM_SPTE_COUNT; i++)
		nv_wv32(vs->pt[pdenum], i * 8, 0);

	if (dev_priv->chipset == 0x50)
		chan_pd = NV50_CHAN_PD;
	else
		chan_pd = NV84_CHAN_PD;

	list_for_each(pos, &vs->chan_list) {
		struct pscnv_chan *ch = list_entry(pos, struct pscnv_chan, vspace_list);
		uint64_t pde = vs->pt[pdenum]->start | 3;
		nv_wv32(ch->vo, chan_pd + pdenum * 8 + 4, pde >> 32);
		nv_wv32(ch->vo, chan_pd + pdenum * 8, pde);
	}
	return 0;
}

static int
pscnv_vspace_do_map (struct pscnv_vspace *vs, struct pscnv_vo *vo, uint64_t offset) {
	struct list_head *pos;
	int ret;
	list_for_each(pos, &vo->regions) {
		/* XXX: beef up to use contig blocks */
		struct pscnv_vram_region *reg = list_entry(pos, struct pscnv_vram_region, local_list);
		uint64_t roff;
		for (roff = 0; roff < reg->size; roff += 0x1000, offset += 0x1000) {
			uint32_t pgnum = offset / 0x1000;
			uint32_t pdenum = pgnum / NV50_VM_SPTE_COUNT;
			uint32_t ptenum = pgnum % NV50_VM_SPTE_COUNT;
			uint64_t pte = reg->start + roff;
			pte |= (uint64_t)vo->tile_flags << 40;
			pte |= 1; /* present */
			if (!vs->pt[pdenum])
				if ((ret = pscnv_vspace_fill_pd_slot (vs, pdenum))) {
					pscnv_vspace_do_unmap (vs, offset, vo->size);
					return ret;
				}
			nv_wv32(vs->pt[pdenum], ptenum * 8 + 4, pte >> 32);
			nv_wv32(vs->pt[pdenum], ptenum * 8, pte);
		}
	}
	return 0;
}

struct pscnv_vspace *
pscnv_vspace_new (struct drm_device *dev) {
	struct pscnv_vspace *res = kzalloc(sizeof *res, GFP_KERNEL);
	struct pscnv_vm_mapnode *fmap;
	if (!res)
		return 0;
	res->dev = dev;
	mutex_init(&res->lock);
	INIT_LIST_HEAD(&res->chan_list);
	PSCNV_RB_INIT(&res->maps);
	fmap = kzalloc(sizeof *fmap, GFP_KERNEL);
	if (!fmap) {
		kfree(res);
		return 0;
	}
	fmap->vspace = res;
	fmap->start = 0;
	fmap->size = 1ULL << 40;
	fmap->maxgap = fmap->size;
	PSCNV_RB_INSERT(pscnv_vm_maptree, &res->maps, fmap);
	return res;
}

void
pscnv_vspace_free(struct pscnv_vspace *vs) {
	/* XXX: write me */
}

int
pscnv_vm_init(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *barvm = pscnv_vspace_new (dev);
	struct pscnv_chan *barch;
	struct pscnv_vm_mapnode *foo;
	int bar1dma, bar3dma;
	if (!barvm)
		return -ENOMEM;
	barvm->isbar = 1;
	barch = pscnv_chan_new (barvm);
	if (!barch)
		return -ENOMEM;
	nv_wr32(dev, 0x1704, 0x40000000 | barch->vo->start >> 12);
	bar1dma = pscnv_chan_dmaobj_new(barch, 0x7fc00000, 0, dev_priv->fb_size);
	bar3dma = pscnv_chan_dmaobj_new(barch, 0x7fc00000, dev_priv->fb_size, dev_priv->ramin_size);
	nv_wr32(dev, 0x1708, 0x80000000 | bar1dma >> 4);
	nv_wr32(dev, 0x170c, 0x80000000 | bar3dma >> 4);
	dev_priv->barvm = barvm;
	dev_priv->barch = barch;
	mutex_init(&dev_priv->vm_mutex);
	pscnv_vspace_map3(barch->vo);
	pscnv_vspace_map3(barvm->pt[0]);

	pscnv_vspace_map(barvm, barch->vo, dev_priv->fb_size, dev_priv->fb_size + dev_priv->ramin_size, 0, &foo);
	pscnv_vspace_map(barvm, barvm->pt[0], dev_priv->fb_size, dev_priv->fb_size + dev_priv->ramin_size, 0, &foo);

	return 0;
}

int
pscnv_vm_takedown(struct drm_device *dev) {
	nv_wr32(dev, 0x1708, 0);
	nv_wr32(dev, 0x170c, 0);
	nv_wr32(dev, 0x1710, 0);
	nv_wr32(dev, 0x1704, 0);
	/* XXX: write me. */
	return 0;
}

static struct pscnv_vm_mapnode *
pscnv_vspace_map_int(struct pscnv_vspace *vs, struct pscnv_vo *vo,
		uint64_t start, uint64_t end, int back,
		struct pscnv_vm_mapnode *node)
{
	struct pscnv_vm_mapnode *left, *right, *res;
	int lok, rok;
	uint64_t mstart, mend;
	left = PSCNV_RB_LEFT(node, entry);
	right = PSCNV_RB_RIGHT(node, entry);
	lok = left && left->maxgap >= vo->size && node->start > start;
	rok = right && right->maxgap >= vo->size && node->start + node->size  < end;
	if (pscnv_vm_debug >= 2)
		NV_INFO (vs->dev, "VM map: %llx %llx %llx %llx %llx %llx %llx %llx %llx %d %d\n", node->start, node->size, node->maxgap,
				left?left->start:0, left?left->size:0, left?left->maxgap:0,
				right?right->start:0, right?right->size:0, right?right->maxgap:0, lok, rok);
	if (!back && lok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, left);
		if (res)
			return res;
	}
	if (back && rok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, right);
		if (res)
			return res;
	}
	mstart = node->start;
	if (mstart < start)
		mstart = start;
	mend = node->start + node->size;
	if (mend > end)
		mend = end;
	if (mstart + vo->size <= mend && !node->vo) {
		if (back)
			mstart = mend - vo->size;
		mend = mstart + vo->size;
		if (node->start + node->size != mend) {
			struct pscnv_vm_mapnode *split = kzalloc(sizeof *split, GFP_KERNEL);
			if (!split)
				return 0;
			split->start = mend;
			split->size = node->start + node->size - mend;
			node->size = mend - node->start;
			split->maxgap = split->size;
			PSCNV_RB_INSERT(pscnv_vm_maptree, &vs->maps, split);
		}
		if (node->start != mstart) {
			struct pscnv_vm_mapnode *split = kzalloc(sizeof *split, GFP_KERNEL);
			if (!split)
				return 0;
			split->start = node->start;
			split->size = mstart - node->start;
			node->start = mstart;
			node->size = mend - node->start;
			split->maxgap = split->size;
			PSCNV_RB_INSERT(pscnv_vm_maptree, &vs->maps, split);
		}
		node->vo = vo;
		PSCNV_RB_AUGMENT(node);
		return node;
	}
	if (back && lok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, left);
		if (res)
			return res;
	}
	if (!back && rok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, right);
		if (res)
			return res;
	}
	return 0;
}

int
pscnv_vspace_map(struct pscnv_vspace *vs, struct pscnv_vo *vo,
		uint64_t start, uint64_t end, int back,
		struct pscnv_vm_mapnode **res)
{
	struct pscnv_vm_mapnode *node;
	mutex_lock(&vs->lock);
	node = pscnv_vspace_map_int(vs, vo, start, end, back, PSCNV_RB_ROOT(&vs->maps));
	if (!node) {
		mutex_unlock(&vs->lock);
		return -ENOMEM;
	}
	if (pscnv_vm_debug >= 1)
		NV_INFO(vs->dev, "Mapping VO %x/%d at %llx-%llx.\n", vo->cookie, vo->serial, node->start,
				node->start + node->size);
	pscnv_vspace_do_map(vs, vo, node->start);
	*res = node;
	mutex_unlock(&vs->lock);
	return 0;
}

static int
pscnv_vspace_unmap_node_unlocked(struct pscnv_vm_mapnode *node) {
	pscnv_vspace_do_unmap(node->vspace, node->start, node->size);
	if (!node->vspace->isbar) {
		drm_gem_object_unreference(node->vo->gem);
	}
	node->vo = 0;
	node->maxgap = node->size;
	PSCNV_RB_AUGMENT(node);
	/* XXX: try merge */
	return 0;
}

int
pscnv_vspace_unmap_node(struct pscnv_vm_mapnode *node) {
	struct pscnv_vspace *vs = node->vspace;
	int ret;
	mutex_lock(&vs->lock);
	ret = pscnv_vspace_unmap_node_unlocked(node);
	mutex_unlock(&vs->lock);
	return ret;
}

int
pscnv_vspace_unmap(struct pscnv_vspace *vs, uint64_t start) {
	struct pscnv_vm_mapnode *node;
	int ret;
	mutex_lock(&vs->lock);
	node = PSCNV_RB_ROOT(&vs->maps);
	while (node) {
		if (node->start == start && node->vo) {
			ret = pscnv_vspace_unmap_node_unlocked(node);
			mutex_unlock(&vs->lock);
			return ret;
		}
		if (start < node->start)
			node = PSCNV_RB_LEFT(node, entry);
		else
			node = PSCNV_RB_RIGHT(node, entry);
	}
	mutex_unlock(&vs->lock);
	return -ENOENT;
}

int pscnv_vspace_map1(struct pscnv_vo *vo) {
	struct drm_nouveau_private *dev_priv = vo->dev->dev_private;
	if (vo->map1)
		return 0;
	if (!dev_priv->barvm)
		return -ENODEV;
	return pscnv_vspace_map(dev_priv->barvm, vo, 0, dev_priv->fb_size, 0, &vo->map1);
}

int pscnv_vspace_map3(struct pscnv_vo *vo) {
	struct drm_nouveau_private *dev_priv = vo->dev->dev_private;
	if (vo->map3)
		return 0;
	if (!dev_priv->barvm)
		return -ENODEV;
	return pscnv_vspace_map(dev_priv->barvm, vo, dev_priv->fb_size, dev_priv->fb_size + dev_priv->ramin_size, 0, &vo->map3);
}

static struct vm_operations_struct pscnv_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};	

int pscnv_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct pscnv_vo *vo;
	int ret;

	if ((vma->vm_pgoff * PAGE_SIZE & ~0x7f0000ull) == 0xc0000000) {
		int cid = (vma->vm_pgoff * PAGE_SIZE >> 16) & 0x7f;
		if (vma->vm_end - vma->vm_start > 0x2000)
			return -EINVAL;
		/* XXX: check for valid process */

		vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
		return remap_pfn_range(vma, vma->vm_start, 
			(dev_priv->mmio_phys + 0xc00000 + cid * 0x2000) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
	}

	obj = drm_gem_object_lookup(dev, priv, (vma->vm_pgoff * PAGE_SIZE) >> 32);
	if (!obj)
		return -ENOENT;
	vo = obj->driver_private;
	
	if (vma->vm_end - vma->vm_start > vo->size) {
		drm_gem_object_unreference_unlocked(obj);
		return -EINVAL;
	}
	if ((ret = pscnv_vspace_map1(vo))) {
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}

	vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
	vma->vm_ops = &pscnv_vm_ops;
	vma->vm_private_data = obj;
	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

	vma->vm_file = filp;

	return remap_pfn_range(vma, vma->vm_start, 
			(dev_priv->fb_phys + vo->map1->start) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
}

/* needs vm_mutex held */
struct pscnv_vspace *
pscnv_get_vspace(struct drm_device *dev, struct drm_file *file_priv, int vid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (vid < 128 && vid >= 0 && dev_priv->vspaces[vid] && dev_priv->vspaces[vid]->filp == file_priv) {
		return dev_priv->vspaces[vid];
	}
	return 0;
}

int pscnv_ioctl_vspace_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_req *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid = -1;
	int i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	for (i = 0; i < 128; i++)
		if (!dev_priv->vspaces[i]) {
			vid = i;
			break;
		}

	if (vid == -1) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOSPC;
	}

	dev_priv->vspaces[vid] = pscnv_vspace_new(dev);
	if (!dev_priv->vspaces[i]) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOMEM;
	}

	dev_priv->vspaces[vid]->filp = file_priv;
	
	req->vid = vid;

	NV_INFO(dev, "Allocating VSPACE %d\n", vid);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_vspace_free(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_req *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid = req->vid;
	struct pscnv_vspace *vs;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);
	vs = pscnv_get_vspace(dev, file_priv, vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	NV_INFO(dev, "Freeing VSPACE %d\n", vid);

	pscnv_vspace_free(vs);

	dev_priv->vspaces[vid] = 0;

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_vspace_map(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_map *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *vs;
	struct drm_gem_object *obj;
	struct pscnv_vo *vo;
	struct pscnv_vm_mapnode *map;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	vs = pscnv_get_vspace(dev, file_priv, req->vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	obj = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!obj) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -EBADF;
	}

	vo = obj->driver_private;

	ret = pscnv_vspace_map(vs, vo, req->start, req->end, req->back, &map);
	if (map)
		req->offset = map->start;

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}

int pscnv_ioctl_vspace_unmap(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_unmap *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *vs;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	vs = pscnv_get_vspace(dev, file_priv, req->vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	ret = pscnv_vspace_unmap(vs, req->offset);

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}
