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
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mutex.h>

#define PSCNV_VRAM_DEBUG

static inline uint64_t
pscnv_roundup (uint64_t x, uint32_t y)
{
	return (x + y - 1) / y * y;
}

static inline struct pscnv_vram_region *
pscnv_vram_global_next (struct drm_device *dev, struct pscnv_vram_region *reg)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (reg->global_list.next == &dev_priv->vram_global_list)
		return 0;
	return list_entry(reg->global_list.next, struct pscnv_vram_region, global_list);
}

static inline struct pscnv_vram_region *
pscnv_vram_global_prev (struct drm_device *dev, struct pscnv_vram_region *reg)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (reg->global_list.prev == &dev_priv->vram_global_list)
		return 0;
	return list_entry(reg->global_list.prev, struct pscnv_vram_region, global_list);
}

static inline struct pscnv_vram_region *
pscnv_vram_free_next (struct drm_device *dev, struct pscnv_vram_region *reg)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (reg->local_list.next == &dev_priv->vram_free_list)
		return 0;
	return list_entry(reg->local_list.next, struct pscnv_vram_region, local_list);
}

static inline struct pscnv_vram_region *
pscnv_vram_free_prev (struct drm_device *dev, struct pscnv_vram_region *reg)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (reg->local_list.prev == &dev_priv->vram_free_list)
		return 0;
	return list_entry(reg->local_list.prev, struct pscnv_vram_region, local_list);
}

/* splits off a new region starting from left side of existing region */
static struct pscnv_vram_region *
pscnv_vram_split_left (struct drm_device *dev, struct pscnv_vram_region *reg, uint64_t size)
{
	struct pscnv_vram_region *left = kmalloc (sizeof *left, GFP_KERNEL);
	if (!left)
		return 0;
	left->type = reg->type;
	left->start = reg->start;
	left->size = size;
	list_add_tail(&left->local_list, &reg->local_list);
	list_add_tail(&left->global_list, &reg->global_list);
	reg->size -= left->size;
	reg->start += left->size;
#ifdef PSCNV_VRAM_DEBUG
	NV_INFO(dev, "Split left type %d: %llx:%llx:%llx\n", reg->type,
			left->start, reg->start, reg->start + reg->size);
#endif
	return left;
}

/* splits off a new region starting from right side of existing region */
static struct pscnv_vram_region *
pscnv_vram_split_right (struct drm_device *dev, struct pscnv_vram_region *reg, uint64_t size)
{
	struct pscnv_vram_region *right = kmalloc (sizeof *right, GFP_KERNEL);
	if (!right)
		return 0;
	right->type = reg->type;
	right->start = reg->start + reg->size - size;
	right->size = size;
	list_add(&right->local_list, &reg->local_list);
	list_add(&right->global_list, &reg->global_list);
	reg->size -= right->size;
#ifdef PSCNV_VRAM_DEBUG
	NV_INFO(dev, "Split right type %d: %llx:%llx:%llx\n", reg->type,
			reg->start, right->start, right->start + right->size);
#endif
	return right;
}

/* try to merge two regions, returning the merged region, or first region if merge failed. */
static struct pscnv_vram_region *
pscnv_vram_try_merge (struct drm_device *dev, struct pscnv_vram_region *a, struct pscnv_vram_region *b)
{
	struct pscnv_vram_region *c, *d;
	if (a->start < b->start)
		c = a, d = b;
	else
		c = b, d = a;
	if (c->start + c->size != d->start) {
		NV_ERROR(dev, "internal error: tried to merge non-adjacent regions at %llx-%llx and %llx-%llx!\n", a->start, a->start + a->size, b->start, b->start + b->size);
		return a;
	}
	if (a->type != b->type)
		return a;
#ifdef PSCNV_VRAM_DEBUG
	NV_INFO(dev, "Merging type %d: %llx:%llx:%llx\n", a->type,
			c->start, d->start, d->start + d->size);
#endif
	c->size += d->size;
	list_del(&d->global_list);
	list_del(&d->local_list);
	kfree(d);
	return c;
}

/* try to merge adjacent regions into given region, returning the merged result */
static struct pscnv_vram_region *
pscnv_vram_try_merge_adjacent (struct drm_device *dev, struct pscnv_vram_region *reg)
{
	struct pscnv_vram_region *next, *prev;
	next = pscnv_vram_global_next(dev, reg);
	if (next)
		reg = pscnv_vram_try_merge (dev, reg, next);
	prev = pscnv_vram_global_prev(dev, reg);
	if (prev)
		reg = pscnv_vram_try_merge (dev, reg, prev);
	return reg;
}

/* given a typed free region, try to convert to an untyped region, splitting
 * out the middle if needed */
static int
pscnv_vram_try_untype(struct drm_device *dev, struct pscnv_vram_region *reg) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint64_t split = pscnv_roundup (reg->start, dev_priv->vram_rblock_size);
	uint64_t finalsize;
	/* can we fit one full rblock to untype? */
	if (split + dev_priv->vram_rblock_size > reg->start + reg->size)
		/* if not, return */
		return 0;
	/* okay. proceed with untyping. check if we need to cut off the
	 * left part. */
	if (split != reg->start) {
		if (!pscnv_vram_split_left(dev, reg, split - reg->start))
			return -ENOMEM;
	}
	/* compute what the final size of the new region will be */
	finalsize = reg->size / dev_priv->vram_rblock_size * dev_priv->vram_rblock_size;
	/* if needed, cut off the right part too */
	if (finalsize != reg->size) {
		if (!pscnv_vram_split_right(dev, reg, reg->size - finalsize))
			return -ENOMEM;
	}
	/* ok, we can untype the region now. */
	reg->type = PSCNV_VRAM_FREE_UNTYPED;
	pscnv_vram_try_merge_adjacent(dev, reg);
	return 0;
}

int
pscnv_vram_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vram_region *allmem;
	uint32_t r0, r4, rc, ru;
	int parts, i, colbits, rowbitsa, rowbitsb, banks;
	uint64_t rowsize, predicted;
	INIT_LIST_HEAD(&dev_priv->vram_global_list);
	INIT_LIST_HEAD(&dev_priv->vram_free_list);
	mutex_init(&dev_priv->vram_mutex);
	spin_lock_init(&dev_priv->pramin_lock);

	if (dev_priv->card_type != NV_50) {
		NV_ERROR(dev, "Sorry, no memory allocator for NV%02x. Bailing.\n",
				dev_priv->chipset);
		return -EINVAL;
	}

	r0 = nv_rd32(dev, 0x100200);
	r4 = nv_rd32(dev, 0x100204);
	rc = nv_rd32(dev, 0x10020c);
	ru = nv_rd32(dev, 0x1540);
	NV_INFO (dev, "Memory config regs: %08x %08x %08x %08x\n", r0, r4, rc, ru);

	parts = 0;
	for (i = 0; i < 8; i++)
		if (ru & (1 << (i + 16)))
			parts++;
	colbits = (r4 >> 12) & 0xf;
	rowbitsa = ((r4 >> 16) & 0xf) + 8;
	rowbitsb = ((r4 >> 20) & 0xf) + 8;
	banks = ((r4 & 1 << 24) ? 8 : 4);
	rowsize = parts * banks * (1 << colbits) * 8;
	predicted = rowsize << rowbitsa;
	if (r0 & 4)
		predicted += rowsize << rowbitsb;

	dev_priv->vram_size = (rc & 0xfffff000) | ((uint64_t)rc & 0xff) << 32;
	if (!dev_priv->vram_size) {
		NV_ERROR(dev, "Memory controller claims 0 VRAM - aborting.\n");
		return -ENODEV;
	}
	if (dev_priv->vram_size != predicted) {
		NV_WARN(dev, "Memory controller reports VRAM size of 0x%llx, inconsistent with our calculation of 0x%llx!\n", dev_priv->vram_size, predicted);
	}
	if (dev_priv->chipset == 0xaa || dev_priv->chipset == 0xac)
		dev_priv->vram_sys_base = (uint64_t)nv_rd32(dev, 0x100e10) << 12;

	/* XXX: this is still not entirely correct.
	 *
	 * There seem to be two kinds of LSR, called the POT way and the NPOT way.
	 * The problem is that we don't know which one the card uses.
	 *
	 * the POT way: reordering units are 1/4 the row size [ie. 1 or 2 banks].
	 * Reordering block is 12 units long [ie. 3 rows], pattern is: 1 0 3 2 5 4 8 10 6 11 7 9
	 *
	 * the NPOT way: reordering unit is a single bank
	 * Roerdering block is 4 or 8 units long, depending on bank count [ie. 1 row].
	 * Pattern is 2 1 3 0 for 4 banks, 4 6 1 3 5 7 0 2 for 8 banks.
	 *
	 * The POT way has been observed so far only on cards with 1 or 2
	 * memory partitions [ie. 64-bit or 128-bit memory bus]. The NPOT way
	 * has been observed on cards with 6 and 7 memory partitions
	 * [NV50, NVA0], and seems to be used on them even if all but 1, 2 or 4
	 * memory partitions are manually disabled. So there are several
	 * possibilities:
	 *
	 *  - There's a switch somewhere in PFB that chooses the LSR mode
	 *  - POT way was added on NV84+ and is auto-enabled when partition count
	 *    is a POT. I didn't notice that because I did my test on NV50.
	 *  - POT/NPOT way selection depends on the chipset and nothing else.
	 *    chipsets with max partition count of 1/2 just happen to use
	 *    the POT way.
	 *
	 * So we don't really know how to determine the LSR kind used. However,
	 * since period of the POT way is conveniently a multiple of NPOT
	 * way's period, we just use the larger one for safety.
	 */
	dev_priv->vram_rblock_size = rowsize * 3;

	NV_INFO(dev, "VRAM: size 0x%llx, LSR period %x\n",
			dev_priv->vram_size, dev_priv->vram_rblock_size);

	allmem = kmalloc (sizeof *allmem, GFP_KERNEL);
	if (!allmem)
		return -ENOMEM;
	allmem->type = PSCNV_VRAM_FREE_SANE;
	allmem->start = 0x40000;
	allmem->size = dev_priv->vram_size - 0x40000 - 0x2000;
	list_add(&allmem->global_list, &dev_priv->vram_global_list);
	list_add(&allmem->local_list, &dev_priv->vram_free_list);
	pscnv_vram_try_untype(dev, allmem);
	return 0;
}

int
pscnv_vram_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct list_head *pos, *next;
restart:
	list_for_each_safe(pos, next, &dev_priv->vram_global_list) {
		struct pscnv_vram_region *reg = list_entry(pos, struct pscnv_vram_region, local_list);
		if (reg->type > PSCNV_VRAM_LAST_FREE) {
			NV_ERROR(dev, "VO %d of type %08x still exists at takedown!\n",
					reg->vo->serial, reg->vo->cookie);
			pscnv_vram_free(reg->vo);
			goto restart;
		}
	}
	list_for_each_safe(pos, next, &dev_priv->vram_global_list) {
		struct pscnv_vram_region *reg = list_entry(pos, struct pscnv_vram_region, local_list);
		kfree (reg);
	}
	return 0;
}

struct pscnv_vo *
pscnv_vram_alloc(struct drm_device *dev,
		uint64_t size, int flags, int tile_flags, uint32_t cookie)
{
	static int serial = 0;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int lsr;
	struct pscnv_vo *res;
	struct pscnv_vram_region *cur, *next;
	switch (tile_flags) {
		case 0:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x20:
		case 0x21:
		case 0x22:
		case 0x23:
		case 0x24:
		case 0x25:
		case 0x26:
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46:
		case 0x54:
		case 0x55:
		case 0x56:
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x64:
		case 0x65:
		case 0x66:
		case 0x68:
		case 0x69:
		case 0x6a:
		case 0x6b:
		case 0x70:
		case 0x74:
		case 0x78:
		case 0x79:
		case 0x7c:
		case 0x7d:
			lsr = 0;
			break;
		case 0x18:
		case 0x19:
		case 0x1a:
		case 0x1b:
		case 0x28:
		case 0x29:
		case 0x2a:
		case 0x2b:
		case 0x2c:
		case 0x2d:
		case 0x2e:
		case 0x47:
		case 0x48:
		case 0x49:
		case 0x4a:
		case 0x4b:
		case 0x4c:
		case 0x4d:
		case 0x6c:
		case 0x6d:
		case 0x6e:
		case 0x6f:
		case 0x72:
		case 0x76:
		case 0x7a:
		case 0x7b:
			lsr = 1;
			break;
		default:
			return 0;
	}

	/* avoid all sorts of integer overflows possible otherwise. */
	if (size >= (1ULL << 40))
		return 0;

	res = kmalloc (sizeof *res, GFP_KERNEL);
	if (!res)
		return 0;
	size = ALIGN(size, PSCNV_VRAM_PAGE_SIZE);
	res->dev = dev;
	res->size = size;
	res->flags = flags;
	res->tile_flags = tile_flags;
	res->cookie = cookie;
	INIT_LIST_HEAD(&res->regions);

	mutex_lock(&dev_priv->vram_mutex);
	res->serial = serial++;
#ifdef PSCNV_VRAM_DEBUG
	NV_INFO(dev, "Allocating %d, %#llx-byte %sVO of type %08x, tile_flags %x\n", res->serial, size,
			(flags & PSCNV_VO_CONTIG ? "contig " : ""), cookie, tile_flags);
#endif
	if (list_empty(&dev_priv->vram_free_list)) {
		mutex_unlock(&dev_priv->vram_mutex);
		return 0;
	}
	if (!lsr)
		cur = list_entry(dev_priv->vram_free_list.next, struct pscnv_vram_region, local_list);
	else
		cur = list_entry(dev_priv->vram_free_list.prev, struct pscnv_vram_region, local_list);

	/* go through the list, looking for free region that might fit the bill */
	while (cur) {
		if (!lsr)
			next = pscnv_vram_free_next(dev, cur);
		else
			next = pscnv_vram_free_prev(dev, cur);
		/* if contig VO is wanted, skip too small regions */
		if (cur->size >= size || !(flags & PSCNV_VO_CONTIG)) {
			/* if region is untyped, we can use it but we need to
			 * convert to typed first.
			 */
			/* XXX here: if we have adjacent typed and untyped
			 * free regions, we should treat them as a single large
			 * typed region for purposes of allocating memory.
			 * With the current code, we'll get these blocks
			 * allocated in sequence, but represented by two
			 * separate regions in the region list. This kills
			 * contig optimisations. And if asked for contig
			 * allocation, it'll just ignore the first region.
			 *
			 * Fix this later.
			 */
			if (cur->type == PSCNV_VRAM_FREE_UNTYPED) {
				uint64_t ssize = pscnv_roundup(size, dev_priv->vram_rblock_size);
				if (ssize > cur->size)
					ssize = cur->size;
				if (ssize != cur->size) {
					if (!lsr)
						pscnv_vram_split_right(dev, cur, cur->size - ssize);
					else
						pscnv_vram_split_left(dev, cur, cur->size - ssize);
				}
				if (!lsr)
					cur->type = PSCNV_VRAM_FREE_SANE;
				else
					cur->type = PSCNV_VRAM_FREE_LSR;
				/* now fall through. */
			}
			if (cur->type == (lsr?PSCNV_VRAM_FREE_LSR:PSCNV_VRAM_FREE_SANE)) {
				if (cur->size > size) {
					if (lsr)
						cur = pscnv_vram_split_right(dev, cur, size);
					else
						cur = pscnv_vram_split_left(dev, cur, size);
				}
				cur->type = (lsr?PSCNV_VRAM_USED_LSR:PSCNV_VRAM_USED_SANE);
				list_del(&cur->local_list);
				if (lsr)
					list_add(&cur->local_list, &res->regions);
				else
					list_add_tail(&cur->local_list, &res->regions);
#ifdef PSCNV_VRAM_DEBUG
				NV_INFO (dev, "Using block at %llx-%llx\n",
						cur->start, cur->start + cur->size);
#endif
				if (flags & PSCNV_VO_CONTIG)
					res->start = cur->start;
				cur->vo = res;
				size -= cur->size;
			}
			if (!size) {
				mutex_unlock(&dev_priv->vram_mutex);
				return res;
			}
		}
		cur = next;
	}
	/* no free blocks. remove what we managed to alloc and fail. */
	mutex_unlock(&dev_priv->vram_mutex);
	pscnv_vram_free(res);
	return 0;
}

static int
pscnv_vram_free_region (struct drm_device *dev, struct pscnv_vram_region *reg) {
	int lsr;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vram_region *next, *prev;
	if (reg->type == PSCNV_VRAM_USED_LSR) {
		reg->type = PSCNV_VRAM_FREE_LSR;
		lsr = 1;
	} else if (reg->type == PSCNV_VRAM_USED_SANE) {
		reg->type = PSCNV_VRAM_FREE_SANE;
		lsr = 0;
	} else {
		NV_ERROR (dev, "Trying to free block %llx-%llx of type %d.\n",
				reg->start, reg->start+reg->size, reg->type);
		return -EINVAL;
	}
	mutex_lock(&dev_priv->vram_mutex);
#ifdef PSCNV_VRAM_DEBUG
	NV_INFO (dev, "Freeing block %llx-%llx of type %d.\n",
			reg->start, reg->start+reg->size, reg->type);
#endif
	list_del(&reg->local_list);
	next = pscnv_vram_global_next(dev, reg);
	prev = pscnv_vram_global_prev(dev, reg);
	/* search in both directions for the nearest free block. */
	while (1) {
		if (next && next->type <= PSCNV_VRAM_FREE_LSR) {
			list_add_tail(&reg->local_list, &next->local_list);
			break;
		} else if (prev && prev->type <= PSCNV_VRAM_LAST_FREE) {
			list_add(&reg->local_list, &prev->local_list);
			break;
		} else if (!next) {
			list_add_tail(&reg->local_list, &dev_priv->vram_free_list);
			break;
		} else if (!prev) {
			list_add(&reg->local_list, &dev_priv->vram_free_list);
			break;
		} else {
			next = pscnv_vram_global_next(dev, next);
			prev = pscnv_vram_global_prev(dev, prev);
		}
	}
	reg = pscnv_vram_try_merge_adjacent (dev, reg);
	pscnv_vram_try_untype (dev, reg);
	mutex_unlock(&dev_priv->vram_mutex);
	return 0;
}

int
pscnv_vram_free(struct pscnv_vo *vo)
{
	struct list_head *pos, *next;
#ifdef PSCNV_VRAM_DEBUG
	NV_INFO(vo->dev, "Freeing %d, %#llx-byte %sVO of type %08x, tile_flags %x\n", vo->serial, vo->size,
			(vo->flags & PSCNV_VO_CONTIG ? "contig " : ""), vo->cookie, vo->tile_flags);
#endif
	list_for_each_safe(pos, next, &vo->regions) {
		struct pscnv_vram_region *reg = list_entry(pos, struct pscnv_vram_region, local_list);
		pscnv_vram_free_region (vo->dev, reg);
	}
	kfree (vo);
	return 0;
}
