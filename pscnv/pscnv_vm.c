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
	}
	return 0;
}

static int
pscnv_vspace_fill_pd_slot (struct pscnv_vspace *vs, uint32_t pdenum) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	struct list_head *pos;
	uint32_t chan_pd;
	vs->pt[pdenum] = pscnv_vram_alloc(vs->dev, NV50_VM_SPTE_COUNT * 8, PSCNV_VO_CONTIG, 0, 0xa9e7ab1e);
	if (!vs->pt[pdenum]) {
		return -ENOMEM;
	}
	/* XXX: try map here */

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

	/* XXX: try map here */

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

	mutex_unlock(&vs->lock);
	return res;
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
	NV_INFO (vs->dev, "%llx %llx %llx %llx %llx %llx %llx %llx %llx %d %d\n", node->start, node->size, node->maxgap,
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
	NV_INFO(vs->dev, "Mapping VO %x/%d at %llx-%llx.\n", vo->cookie, vo->serial, node->start,
			node->start + node->size);
	pscnv_vspace_do_map(vs, vo, node->start);
	*res = node;
	mutex_unlock(&vs->lock);
	return 0;
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
