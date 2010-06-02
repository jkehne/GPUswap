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
	if (res) {
		res->dev = dev;
		mutex_init(&res->lock);
		INIT_LIST_HEAD(&res->chan_list);
	}
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
