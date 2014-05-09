#include "pscnv_dma.h"

static int
pscnv_vram_to_host(struct pscnv_bo* bo)
{
	int res;
	
	res = pscnv_sysram_alloc(bo);
	
	if (res) {
		NV_INFO(bo->dev, "copy_to_host: failed to allocate SYSRAM!");
		return res;
	}
	
	//res = pscnv_dma_vram_to_host(bo);
    res = -1;
	
	if (res) {
		NV_INFO(bo->dev, "copy_to_host: failed to DMA- Transfer!");
		return res;
	}
	
	return 0;
}

int
pscnv_bo_copy_to_host(struct pscnv_bo* bo)
{
	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			if (bo->pages) {
				NV_INFO(bo->dev, "copy_to_host: %x is VRAM, but has Sysram already allocated, wtf\n", bo->cookie);
				return -EINVAL;
			}
		
			return pscnv_vram_to_host(bo);
		case PSCNV_GEM_SYSRAM_SNOOP:
		case PSCNV_GEM_SYSRAM_NOSNOOP:
			NV_INFO(bo->dev, "copy_to_host: %x is already sysram!, doing nothing\n", bo->cookie);
			return -EINVAL;
		default:
			NV_INFO(bo->dev, "copy_to_host: %x has unknown storage type, doing nothing\n", bo->cookie);
			return -ENOSYS;
	}
}