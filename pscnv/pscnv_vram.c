#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_vram.h"
#include "pscnv_client.h"
#include "pscnv_sysram.h"

/* must be called inside vram_mutex */
int
pscnv_vram_alloc_chunk(struct pscnv_chunk *cnk, int flags)
{
	struct pscnv_bo *bo = cnk->bo;
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	int ret;
	uint64_t size = pscnv_chunk_size(cnk);
	uint64_t vram_start = 0;
	
	if (bo->flags & PSCNV_GEM_USER) {
		vram_start = PSCNV_VRAM_RESERVED;
	}
	
	if (pscnv_chunk_expect_alloc_type(cnk, PSCNV_CHUNK_UNALLOCATED,
						"pscnv_vram_alloc_chunk")) {
		return -EINVAL;
	}
	
	WARN_ON(cnk->vram_node);
	WARN_ON(cnk->flags & PSCNV_CHUNK_SWAPPED);
	
	ret = pscnv_mm_alloc(dev_priv->vram_mm, size, flags,
				vram_start, dev_priv->vram_size, &cnk->vram_node);
	if (ret) {
		char buf[16];
		
		pscnv_mem_human_readable(buf, size);
		NV_INFO(dev, "pscnv_vram_alloc_chunk: unable to allocate %s "
			"for CHUNK %08x/%d-%u. Failed with code %d.\n",
			buf, bo->cookie, bo->serial, cnk->idx, ret);
		return ret;
	}
	
	cnk->alloc_type = PSCNV_CHUNK_VRAM;
	cnk->vram_node->bo = bo;
	cnk->vram_node->chunk = cnk;
	
	if (bo->flags & PSCNV_GEM_USER) {
		WARN_ON(!bo->client);
		if (bo->client) {
			atomic64_add(size, &bo->client->vram_usage);
			bo->client->vram_max = max(bo->client->vram_max,
				(uint64_t) atomic64_read(&bo->client->vram_usage));
		}
	} else {
		uint64_t vram_usage_kernel;
		atomic64_add(size, &dev_priv->vram_usage_kernel);
		
		vram_usage_kernel = atomic64_read(&dev_priv->vram_usage_kernel);
		if (vram_usage_kernel > PSCNV_VRAM_RESERVED) {
			NV_WARN(dev, "WARNING: kernel VRAM usage %lluKB > "
				"%dKB reserved memory",
				vram_usage_kernel >> 10, PSCNV_VRAM_RESERVED >> 10);
		}
	}
	
	return ret;
}

/* must be called inside vram_mutex */
void
pscnv_vram_free_chunk(struct pscnv_chunk *cnk)
{
	struct pscnv_bo *bo = cnk->bo;
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	uint64_t size = pscnv_chunk_size(cnk);
	
	if (pscnv_chunk_expect_alloc_type(cnk, PSCNV_CHUNK_VRAM,
						"pscnv_vram_free_chunk")) {
		return;
	}
	
	WARN_ON(cnk->flags & PSCNV_CHUNK_SWAPPED);
	
	if (!cnk->vram_node) {
		WARN_ON(1);
		return;
	}
	
	pscnv_mm_free(cnk->vram_node);
	
	cnk->alloc_type = PSCNV_CHUNK_UNALLOCATED;
	cnk->vram_node = NULL;
	
	if (bo->flags & PSCNV_GEM_USER) {
		WARN_ON(!bo->client);
		if (bo->client) {
			atomic64_sub(size, &bo->client->vram_usage);
		}
	} else {
		atomic64_sub(size, &dev_priv->vram_usage_kernel);
	}
}


int
pscnv_vram_alloc(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int flags = 0;
	int ret = 0;
	int swap_retries = 0;
	int i = 0;
	
	/*if (bo->tile_flags & 0xffffff00)
		return -EINVAL;*/
	
	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
	case PSCNV_GEM_VRAM_SMALL:
		break;
	case PSCNV_GEM_VRAM_LARGE:
		flags |= PSCNV_MM_LP;
		break;
	default:
		NV_ERROR(dev, "pscnv_vram_alloc: BO %08x/%d has memtype %s\n",
			bo->cookie, bo->serial, pscnv_bo_memtype_str(bo->flags));
			return -EINVAL;
	}

	if (!(bo->flags & PSCNV_GEM_CONTIG))
		flags |= PSCNV_MM_FRAGOK;
	
	dev_priv->last_mem_alloc_change_time = jiffies;
	
	mutex_lock(&dev_priv->vram_mutex);
	if (bo->client) {
		atomic64_add(bo->size, &bo->client->vram_demand);
	}
	
	while (pscnv_swapping_required(bo)) {
		mutex_unlock(&dev_priv->vram_mutex);
		
		if (swap_retries >= 3) {
			NV_ERROR(dev, "!!! pcsvnv_vram_alloc: can not get enough"
					" vram  after %d retries. Try to allocate"
					" anyhow. !!!\n", swap_retries);
			break;
		}
		
		ret = pscnv_swapping_reduce_vram(dev, bo->client);
		if (ret) {
			if (pscnv_swapping_debug >= 1) {
				NV_ERROR(dev, "pscnv_vram_alloc: failed to swap."
					" ret=%d\n", ret);
				/* complain but try again and wait for sth
				 * to happen */
				msleep(50);
			}
		}
		mutex_lock(&dev_priv->vram_mutex);
		/* some one else meight be faster in this lock and steel the
		   memory right in front of us */
		swap_retries++;
	}
	
	for (i = 0; i < (int)bo->n_chunks; i++) {
		struct pscnv_chunk *cnk = &bo->chunks[i];

		if (cnk->alloc_type != PSCNV_CHUNK_UNALLOCATED) {
			/* it is perfectly legal that the alloc_type is
			 * not CHUNK_UNALLOCATED, as the swapping code called
			 * above may allocate this memory as SYSRAM */
			continue;
		}
		
		ret = pscnv_vram_alloc_chunk(cnk, flags);
		if (ret) {
			NV_WARN(dev, "pscnv_vram_alloc: failed to allocate chunk"
				"%08x/%d-%u as VRAM. Fallback to SYSRAM.",
				bo->cookie, bo->serial, cnk->idx);
			mutex_unlock(&dev_priv->vram_mutex);
			/* updates vram_demand and vram_swapped */
			ret = pscnv_swapping_sysram_fallback(cnk);
			if (ret) {
				NV_ERROR(dev, "pscnv_vram_alloc: SYSRAM fallback "
					"failed, too\n");
				for (i--; i >= 0; i--) {
					cnk = &bo->chunks[i];
					
					/* will update vram_demand and vram_swapped */
					pscnv_chunk_free(cnk);
				}
				return ret;
			}
			mutex_lock(&dev_priv->vram_mutex);	
		}
	}
	
	mutex_unlock(&dev_priv->vram_mutex);
	
	if (ret) {
		return ret;
	}
	
	if (bo->flags & PSCNV_GEM_CONTIG) {
		bo->start = bo->chunks[0].vram_node->start;
	}
	
	return ret;
}

static void
pscnv_vram_takedown_free(struct pscnv_mm_node *node) {
	struct pscnv_bo *bo = node->bo;
	NV_ERROR(bo->dev, "BO %d of type %08x still exists at takedown!\n",
			bo->serial, bo->cookie);
	pscnv_mem_free(bo);
}

void
pscnv_vram_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	pscnv_mm_takedown(dev_priv->vram_mm, pscnv_vram_takedown_free);
}