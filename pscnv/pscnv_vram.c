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
	
	if (pscnv_chunk_expect_alloc_type(cnk, PSCNV_CHUNK_UNALLOCATED,
						"pscnv_vram_alloc_chunk")) {
		return -EINVAL;
	}
	
	WARN_ON(cnk->vram_node);
	
	ret = pscnv_mm_alloc(dev_priv->vram_mm, size, flags,
				0, dev_priv->vram_size, &cnk->vram_node);
	if (ret) {
		char buf[16];
		
		pscnv_mem_human_readable(buf, size);
		NV_INFO(dev, "pscnv_vram_alloc_chunk: unable to allocate %s "
			"for CHUNK %08x/%d-%u. Failed with code %d\n",
			buf, bo->cookie, bo->serial, cnk->idx, ret);
		return ret;
	}
	
	cnk->alloc_type = PSCNV_CHUNK_VRAM;
	cnk->vram_node->bo = bo;
	cnk->vram_node->chunk = cnk;
	
	atomic64_add(size, &dev_priv->vram_usage);
	if (bo->client) {
		atomic64_add(size, &bo->client->vram_usage);
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
	
	if (!cnk->vram_node) {
		WARN_ON(1);
		return;
	}
	
	pscnv_mm_free(cnk->vram_node);
	
	cnk->alloc_type = PSCNV_CHUNK_UNALLOCATED;
	cnk->vram_node = NULL;
	
	atomic64_sub(size, &dev_priv->vram_usage);
	if (bo->client) {
		atomic64_sub(size, &bo->client->vram_usage);
	}
}


int
pscnv_vram_alloc(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int flags = 0;
	int ret = 0;
	uint64_t will_free;
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
	
	mutex_lock(&dev_priv->vram_mutex);
	atomic64_add(bo->size, &dev_priv->vram_demand);
	if (bo->client) {
		atomic64_add(bo->size, &bo->client->vram_demand);
	}
	
	while ((bo->flags & PSCNV_GEM_USER) && pscnv_swapping_required(dev)) {
		uint64_t req = atomic64_read(&dev_priv->vram_demand) - dev_priv->vram_limit;
		mutex_unlock(&dev_priv->vram_mutex);
		
		if (swap_retries >= 3) {
			NV_ERROR(dev, "pcsvnv_vram_alloc: can not get enough vram "
				      " after %d retries\n", swap_retries);
			return -EBUSY;
		}
		
		ret = pscnv_swapping_reduce_vram(dev,req, &will_free);
		if (ret) {
			NV_ERROR(dev, "pscnv_vram_alloc: failed to swap. ret=%d\n", ret);
			/* complain but try again */
		}
		mutex_lock(&dev_priv->vram_mutex);
		/* some one else meight be faster in this lock and steel the
		   memory right in front of us */
		swap_retries++;
	}
	
	for (i = 0; i < (int)bo->n_chunks; i++) {
		if (bo->chunks[i].alloc_type != PSCNV_CHUNK_UNALLOCATED) {
			/* it is perfectly legal that the alloc_type is
			 * not CHUNK_UNALLOCATED, as the swapping code called
			 * above may allocate this memory as SYSRAM */
			continue;
		}
		
		ret = pscnv_vram_alloc_chunk(&bo->chunks[i], flags);
		if (ret) {
			for (i--; i >= 0; i--) {
				pscnv_chunk_free(&bo->chunks[i]);
			}
			break;
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