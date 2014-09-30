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

#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_mem.h"
#include "pscnv_vm.h"
#ifdef __linux__
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#endif

#include "pscnv_vram.h"
#include "pscnv_sysram.h"
#include "pscnv_client.h"
#include "pscnv_swapping.h"

void
pscnv_bo_memset(struct pscnv_bo* bo, uint32_t val)
{
	uint64_t i = 0;
	
	for (i = 0; i < bo->size; i += 4) {
		nv_wv32(bo, i, val);
	}
}

void
pscnv_mem_human_readable(char *buf, uint64_t val)
{
	if (val >= (1 << 20)) {
		snprintf(buf, 16, "%lluMB", val >> 20);
	} else if (val >= (1 << 10)) {
		snprintf(buf, 16, "%llukB", val >> 10);
	} else {
		snprintf(buf, 16, "%llu Byte", val);
	}
}

uint64_t
pscnv_chunk_size(struct pscnv_chunk *cnk)
{
	struct pscnv_bo *bo = pscnv_chunk_bo(cnk);
	struct drm_nouveau_private *dev_priv = bo->dev->dev_private;
	
	if (cnk->idx + 1 == bo->n_chunks) { /* this is the last chunk */
		/* this implicitly handels the chunking-disabled case */
		return bo->size - cnk->idx * dev_priv->chunk_size;
	} else {
		return dev_priv->chunk_size;
	}
}

/* get the index ot the chunk that holds data at `offset` */
uint32_t
pscnv_chunk_at_offset(struct drm_device *dev, uint64_t offset)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint64_t chunk_size = dev_priv->chunk_size;
	
	return (chunk_size > 0) ? offset/chunk_size : 0;
}

static const char *
pscnv_chunk_alloc_type_str(uint32_t at)
{
	switch (at) {
		case PSCNV_CHUNK_UNALLOCATED:	return "UNALLOCATED";
		case PSCNV_CHUNK_VRAM: 		return "VRAM";
		case PSCNV_CHUNK_SYSRAM:	return "SYSRAM";
		case PSCNV_CHUNK_SWAPPED:	return "SWAPPED";
		default:			return "(UNKNOWN)";
	}
}

static const char *
pscnv_bo_memtype_str(uint32_t flags)
{
	switch (flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:	return "VRAM_SMALL";
		case PSCNV_GEM_VRAM_LARGE:	return "VRAM_LARGE";
		case PSCNV_GEM_SYSRAM_SNOOP:	return "SYSRAM_SNOOP";
		case PSCNV_GEM_SYSRAM_NOSNOOP:	return "SYSRAM_NOSNOOP";
		default:			return "(UNKNOWN)";
	}
}

void
pscnv_chunk_warn_wrong_alloc_type(struct pscnv_chunk *cnk, uint32_t expected, const char *fname)
{
	struct pscnv_bo *bo = pscnv_chunk_bo(cnk);
	struct drm_device *dev = bo->dev;
	
	NV_WARN(dev, "%s: Chunk %08x/%d-%u has alloc_type = %s but expected %s\n",
			fname, bo->cookie, bo->serial, cnk->idx,
			pscnv_chunk_alloc_type_str(cnk->alloc_type),
			pscnv_chunk_alloc_type_str(expected));
}

int
pscnv_mem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;
	char buf[16];
	
	int dma_bits = 32;
	
	if (pscnv_requested_chunk_size > 0) {
		dev_priv->chunk_size = pscnv_requested_chunk_size << 17;
		pscnv_mem_human_readable(buf, dev_priv->chunk_size);
		NV_INFO(dev, "MEM: chunk size set to %s\n", buf);
	} else {
		dev_priv->chunk_size = 0;
		NV_INFO(dev, "MEM: buffer chunking disabled\n");
	}

	if (dev_priv->card_type >= NV_50 && pci_dma_supported(dev->pdev, DMA_BIT_MASK(40)))
		dma_bits = 40;

	ret = pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(dma_bits));
	if (ret) {
		NV_ERROR(dev, "Error setting DMA mask: %d\n", ret);
		return ret;
	}

	dev_priv->dma_mask = DMA_BIT_MASK(dma_bits);

	spin_lock_init(&dev_priv->pramin_lock);
	mutex_init(&dev_priv->vram_mutex);
	
	switch (dev_priv->card_type) {
		case NV_50:
			ret = nv50_vram_init(dev);
			break;
		case NV_D0:
		case NV_C0:
			ret = nvc0_vram_init(dev);
			break;
		default:
			NV_ERROR(dev, "No VRAM allocator for NV%02x!\n", dev_priv->chipset);
			ret = -ENOSYS;
	}
	if (ret)
		return ret;

	dev_priv->fb_mtrr = drm_mtrr_add(drm_get_resource_start(dev, 1),
					 drm_get_resource_len(dev, 1),
					 DRM_MTRR_WC);

	return 0;
}

void
pscnv_mem_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	pscnv_vram_takedown(dev);

	if (dev_priv->fb_mtrr >= 0) {
		drm_mtrr_del(dev_priv->fb_mtrr, drm_get_resource_start(dev, 1),
			     drm_get_resource_len(dev, 1), DRM_MTRR_WC);
		dev_priv->fb_mtrr = 0;
	}
}

struct pscnv_bo *
pscnv_mem_alloc(struct drm_device *dev,
		uint64_t size, int flags, int tile_flags, uint32_t cookie, struct pscnv_client *client)
{
	static int serial = 0;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_bo *res;
	uint32_t n_chunks;
	uint32_t i;
	int ret;

	/* avoid all sorts of integer overflows possible otherwise. */
	if (size >= (1ULL << 40))
		return 0;
	if (!size)
		return 0;

	size = (size + PSCNV_MEM_PAGE_SIZE - 1) & ~(PSCNV_MEM_PAGE_SIZE - 1);
	size = roundup(size, 0x1000);
	if ((flags & PSCNV_GEM_MEMTYPE_MASK) == PSCNV_GEM_VRAM_LARGE) {
		size = roundup(size, 0x20000);
	}
	
	n_chunks = (dev_priv->chunk_size > 0) ? DIV_ROUND_UP(size, dev_priv->chunk_size) : 1; 

	res = kzalloc (sizeof(struct pscnv_bo) + n_chunks*sizeof(struct pscnv_chunk), GFP_KERNEL);
	if (!res) {
		NV_ERROR(dev, "failed to allocate struct pscnv_bo\n");
		return 0;
	}
	
	res->dev = dev;
	res->size = size;
	res->flags = flags;
	res->tile_flags = tile_flags;
	res->cookie = cookie;
	res->gem = 0;
	res->client = client;
	res->n_chunks = n_chunks;
	
	kref_init(&res->ref);

	/* XXX: another mutex? */
	mutex_lock(&dev_priv->vram_mutex);
	res->serial = serial++;
	mutex_unlock(&dev_priv->vram_mutex);
	
	for (i = 0; i < n_chunks; i++) {
		res->chunks[i].idx = i;
		/* allocation_type already set to UNALLOCATED */
		WARN_ON_ONCE(pscnv_chunk_bo(&res->chunks[i]) != res);
	}
	
	if (res->client) {
		switch (res->flags & PSCNV_GEM_MEMTYPE_MASK) {
			case PSCNV_GEM_VRAM_SMALL:
			case PSCNV_GEM_VRAM_LARGE:
				pscnv_swapping_add_bo(res);
		}
	}

	if (pscnv_mem_debug >= 1) {
		char size_str[16];
		pscnv_mem_human_readable(size_str, res->size);
		NV_INFO(dev, "MEM: alloc %08x/%d, %s (%u chunks) %s%s\n",
				res->cookie, res->serial, size_str, res->n_chunks,
				(flags & PSCNV_GEM_CONTIG ? "contig, " : ""),
				pscnv_bo_memtype_str(res->flags));
	}
	
	switch (res->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			ret = pscnv_vram_alloc(res);
			break;
		case PSCNV_GEM_SYSRAM_SNOOP:
		case PSCNV_GEM_SYSRAM_NOSNOOP:
			ret = pscnv_sysram_alloc(res);
			break;
		default:
			ret = -ENOSYS;
	}
	if (ret) {
		kfree(res);
		return 0;
	}
	
	if (res->flags & PSCNV_MAP_KERNEL) {
		dev_priv->vm->map_kernel(res);

		if (!res->map3) {
			NV_ERROR(dev, "pscnv_mem_alloc: BO %08x/%d map_kernel() failed\n",
				res->cookie, res->serial);
			pscnv_mem_free(res);
			return NULL;
		}
	}
	if (res->flags & PSCNV_MAP_USER) {
		if (pscnv_bo_map_bar1(res)) {
			pscnv_mem_free(res);
			return NULL;
		}
	}
	if (res->flags & PSCNV_ZEROFILL) {
		pscnv_bo_memset(res, 0);
		dev_priv->vm->bar_flush(dev);
	}
	
	return res;
}

struct pscnv_bo*
pscnv_mem_alloc_and_map(struct pscnv_vspace *vs, uint64_t size, uint32_t flags, uint32_t cookie, uint64_t *vm_base)
{
	struct drm_device *dev = vs->dev;
	struct pscnv_mm_node *map;
	struct pscnv_bo *bo;
	int ret;
	
	bo = pscnv_mem_alloc(dev, size, flags, 0 /* tile flags */, cookie, NULL);
	
	if (!bo) {
		NV_INFO(dev, "Failed to allocate buffer object of size %llx"
			" in vspace %d, cookie=%x\n", size, vs->vid, cookie);
		return NULL;
	}
	
	ret = pscnv_vspace_map(vs, bo,
			0x20000000, /* start */
			1ull << 40, /* end */
			0, /* back, nonsense? */
			&map);
	
	if (ret) {
		NV_INFO(dev, "failed to map buffer object of size %llx in"
			" vspace %d, cookie=%x\n", size, vs->vid, cookie);
		goto fail_map;
	}
	
	if (vm_base) {
		*vm_base = map->start;
	}
	
	return bo;
	
fail_map:
	pscnv_mem_free(bo);
	
	return NULL;
}

static void
pscnv_chunk_free(struct pscnv_chunk *cnk)
{
	struct pscnv_bo *bo = pscnv_chunk_bo(cnk);
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	uint64_t size = pscnv_chunk_size(cnk);
	
	switch (cnk->alloc_type) {
		case PSCNV_CHUNK_UNALLOCATED:
			NV_INFO(dev, "Freeing UNALLOCATED(??) Chunk %08x/%d-%u\n",
				bo->cookie, bo->serial, cnk->idx);
			break;
		case PSCNV_CHUNK_VRAM:
			atomic64_sub(size, &dev_priv->vram_demand);
			if (bo->client) {
				atomic64_sub(size, &bo->client->vram_demand);
			}
			
			mutex_lock(&dev_priv->vram_mutex);
			pscnv_vram_free_chunk(cnk);
			mutex_unlock(&dev_priv->vram_mutex);
			break;
		case PSCNV_CHUNK_SYSRAM:
		case PSCNV_CHUNK_SWAPPED:
			pscnv_sysram_free_chunk(cnk);
	}
}

int
pscnv_mem_free(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t i;
	
	if (bo->gem) {
		NV_ERROR(bo->dev, "MEM: freeing %08x/%d, with DRM- Wrapper still attached!\n", bo->cookie, bo->serial);
	}
	
	if (pscnv_mem_debug >= 1) {
		char size_str[16];
		pscnv_mem_human_readable(size_str, bo->size);
		NV_INFO(dev, "MEM: free %08x/%d, %s (%u chunks) %s%s\n",
				bo->cookie, bo->serial, size_str, bo->n_chunks,
				(bo->flags & PSCNV_GEM_CONTIG ? "contig, " : ""),
				pscnv_bo_memtype_str(bo->flags));
	}

	pscnv_swapping_remove_bo(bo);

	if (bo->drm_map) {
		drm_rmmap(dev, bo->drm_map);
	}

	if (dev_priv->vm_ok && bo->map1)
		pscnv_vspace_unmap_node(bo->map1);
	if (dev_priv->vm_ok && bo->map3)
		pscnv_vspace_unmap_node(bo->map3);
	
	
	/*if (bo->backing_store) {
		atomic64_sub(bo->size, &dev_priv->vram_swapped);
		if (bo->client) {
			atomic64_sub(bo->size, &bo->client->vram_swapped);
		}
		if (bo->backing_store != bo) {
			pscnv_bo_unref(bo->backing_store);
		
			kfree(bo);
			//the memory handled by this bo has already been free'd
			//   on swapping
			return 0;
		}
	}*/
	
	for (i = 0; i < bo->n_chunks; i++) {
		pscnv_chunk_free(&bo->chunks[i]);
	}
	
	kfree (bo);
	return 0;
}



int
pscnv_bo_map_bar1(struct pscnv_bo* bo)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;
	
	if (bo->map1) {
		NV_INFO(dev, "pscnv_bo_map_bar1: BO %08x/%d already mapped to BAR1\n",
			bo->cookie, bo->serial);
		return -EINVAL;
	}
	
	dev_priv->vm->map_user(bo);

	if (!bo->map1) {
		NV_ERROR(dev, "pscnv_bo_map_bar1: BO %08x/%d map_user() failed\n",
			bo->cookie, bo->serial);
		return -ENOMEM;
	}
	
	ret = drm_addmap(dev, drm_get_resource_start(dev, 1) +	bo->map1->start,
			bo->size, _DRM_REGISTERS, _DRM_KERNEL | _DRM_DRIVER,
			&bo->drm_map);
	if (ret) {
		NV_ERROR(dev, "pscnv_bo_map_bar1: BO %08x/%d drm_addmap() failed with code %d\n",
			bo->cookie, bo->serial, ret);
		return ret;
	}
	
	return 0;
}

void
pscnv_bo_ref_free(struct kref *ref)
{
	struct pscnv_bo *bo = container_of(ref, struct pscnv_bo, ref);
	pscnv_mem_free(bo);
}

static uint32_t
nv_rv32_vram_slowpath(struct pscnv_chunk *cnk, unsigned offset)
{
	struct pscnv_bo *bo = pscnv_chunk_bo(cnk);
	struct drm_device *dev = bo->dev;
	
	if (!cnk->vram_node) {
		WARN_ON(1);
		return 42;
	}
	
	if (cnk->vram_node->size <= offset) {
		NV_ERROR(dev, "nv_rv32: BUG! offset %x is not within bounds of first"
				"mm-node for chunk %08x/%d-%u. That case is not "
				"supported, yet", offset, bo->cookie, bo->serial,
				cnk->idx);
		return 42;
	}

	return nv_rv32_pramin(dev, offset + cnk->vram_node->start);
}

static uint32_t
nv_rv32_chunk(struct pscnv_chunk *cnk, unsigned offset)
{
	struct pscnv_bo *bo = pscnv_chunk_bo(cnk);
	struct drm_device *dev = bo->dev;
	
	if (offset >= pscnv_chunk_size(cnk)) {
		NV_ERROR(dev, "nv_rv32_chunk: access at %x is ouf of bounds for"
				" chunk %08x/%d-%u, size = %llx\n",
				offset, bo->cookie, bo->serial, cnk->idx,
				pscnv_chunk_size(cnk));
		return 42;
	}
	
	switch (cnk->alloc_type) {
		case PSCNV_CHUNK_UNALLOCATED:
			NV_ERROR(dev, "nv_rv32_chunk: reading from UNALLOCATED"
				"chunk %08x/%d-%u at offset %x\n",
				bo->cookie, bo->serial, cnk->idx, offset);
			return 42;
		case PSCNV_CHUNK_VRAM:
			return nv_rv32_vram_slowpath(cnk, offset);
		case PSCNV_CHUNK_SYSRAM:
		case PSCNV_CHUNK_SWAPPED:
			return nv_rv32_sysram(cnk, offset);
	}
	
	WARN_ON(1);
	return 42;
}

uint32_t
nv_rv32(struct pscnv_bo *bo, unsigned offset)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	uint32_t cnk_idx = pscnv_chunk_at_offset(dev, offset);
	
	unsigned offset_in_chunk = offset - cnk_idx * dev_priv->chunk_size;
	
	if (offset >= bo->size || cnk_idx >= bo->n_chunks) {
		NV_ERROR(dev, "nv_rv32: access at %x is out of bounds for BO "
			"%08x/%d (size %llx). Access to chunk %d of %d\n",
			offset, bo->cookie, bo->serial, bo->size, cnk_idx+1,
			bo->n_chunks);
		return 42;
	}
	
	if (bo->chunks[cnk_idx].alloc_type == PSCNV_CHUNK_VRAM  && dev_priv->vm && dev_priv->vm_ok) {
		/* try fastpath */	 
		if (bo->drm_map) {
			return le32_to_cpu(DRM_READ32(bo->drm_map, offset));
		} else if (bo->map3) {
			return le32_to_cpu(DRM_READ32(dev_priv->ramin, bo->map3->start - dev_priv->vm_ramin_base + offset));
		}
	}
	
	return nv_rv32_chunk(&bo->chunks[cnk_idx], offset_in_chunk);
}

static void
nv_wv32_vram_slowpath(struct pscnv_chunk *cnk, unsigned offset, uint32_t val)
{
	struct pscnv_bo *bo = pscnv_chunk_bo(cnk);
	struct drm_device *dev = bo->dev;
	
	if (!cnk->vram_node) {
		WARN_ON(1);
		return;
	}
	
	if (cnk->vram_node->size <= offset) {
		NV_ERROR(dev, "nv_wv32: BUG! offset %x is not within bounds of first"
				"mm-node for chunk %08x/%d-%u. That case is not "
				"supported, yet", offset, bo->cookie,
				bo->serial, cnk->idx);
		return;
	}

	return nv_wv32_pramin(dev, offset + cnk->vram_node->start, val);
}

static void
nv_wv32_chunk(struct pscnv_chunk *cnk, unsigned offset, uint32_t val)
{
	struct pscnv_bo *bo = pscnv_chunk_bo(cnk);
	struct drm_device *dev = bo->dev;
	
	if (offset >= pscnv_chunk_size(cnk)) {
		NV_ERROR(dev, "nv_wv32_chunk: access at %x is ouf of bounds for"
				" chunk %08x/%d-%u, size = %llx\n",
				offset, bo->cookie, bo->serial, cnk->idx,
				pscnv_chunk_size(cnk));
		return;
	}
	
	switch (cnk->alloc_type) {
		case PSCNV_CHUNK_UNALLOCATED:
			NV_ERROR(dev, "nv_wv32_chunk: writing to UNALLOCATED"
				"chunk %08x/%d-%u at offset %x\n",
				bo->cookie, bo->serial, cnk->idx, offset);
			return;
		case PSCNV_CHUNK_VRAM:
			nv_wv32_vram_slowpath(cnk, offset, val);
			return;
		case PSCNV_CHUNK_SYSRAM:
		case PSCNV_CHUNK_SWAPPED:
			nv_wv32_sysram(cnk, offset, val);
			return;
	}
	
	WARN_ON(1);
}

void
nv_wv32(struct pscnv_bo *bo, unsigned offset, uint32_t val)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	uint32_t cnk_idx = pscnv_chunk_at_offset(dev, offset);
	
	unsigned offset_in_chunk = offset - cnk_idx * dev_priv->chunk_size;
	
	if (offset >= bo->size || cnk_idx >= bo->n_chunks) {
		NV_ERROR(dev, "nv_wv32: access at %x is out of bounds for BO "
			"%08x/%d (size %llx). Access to chunk %d of %d\n",
			offset, bo->cookie, bo->serial, bo->size, cnk_idx+1,
			bo->n_chunks);
		return;
	}
	
	if (bo->chunks[cnk_idx].alloc_type == PSCNV_CHUNK_VRAM && dev_priv->vm && dev_priv->vm_ok) {
		/* try fastpath */	 
		if (bo->drm_map) {
			DRM_WRITE32(bo->drm_map, offset, cpu_to_le32(val));
			return;
		} else if (bo->map3) {
			DRM_WRITE32(dev_priv->ramin, bo->map3->start - dev_priv->vm_ramin_base + offset, cpu_to_le32(val));
			return;
		}
	}
	
	nv_wv32_chunk(&bo->chunks[cnk_idx], offset_in_chunk, val);
}

uint32_t
nv_rv32_pramin(struct drm_device *dev, uint64_t addr)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;
	uint64_t base = addr &   0xfffffff0000ULL;
	uint64_t offset = addr & 0x0000000ffffULL;
	uint32_t data;

	spin_lock_irqsave(&dev_priv->pramin_lock, flags);
	if (unlikely(dev_priv->pramin_start != base)) {
		nv_wr32(dev, 0x001700, base >> 16);
		dev_priv->pramin_start = base;
	}
	data = nv_rd32(dev, 0x700000 + offset);
	spin_unlock_irqrestore(&dev_priv->pramin_lock, flags);
	return data;
}

void
nv_wv32_pramin(struct drm_device *dev, uint64_t addr, uint32_t val)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;
	uint64_t base = addr   & 0xfffffff0000ULL;
	uint64_t offset = addr & 0x0000000ffffULL;

	spin_lock_irqsave(&dev_priv->pramin_lock, flags);
	if (unlikely(dev_priv->pramin_start != base)) {
		nv_wr32(dev, 0x001700, base >> 16);
		dev_priv->pramin_start = base;
	}
	nv_wr32(dev, 0x700000 + offset, val);
	spin_unlock_irqrestore(&dev_priv->pramin_lock, flags);
}
