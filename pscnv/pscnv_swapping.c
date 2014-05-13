#include "pscnv_dma.h"

static void
pscnv_swapping_memdump(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	uint32_t i;
	uint32_t *mem;
	
	if (!bo->pages || !bo->pages[0]) {
		NV_INFO(dev, "pscnv_swapping_memdump: can not memdump bo with "
			     "cookie=%x, it has no pages attached\n", bo->cookie);
		return;
	}
	
	mem = kmap(bo->pages[0]);
	
	NV_INFO(dev, "==== DUMP BO %x\n", bo->cookie);
	for (i = 0; i < bo->size/4; i += 4) {
		NV_INFO(dev, "%08x %08x %08x %08x\n", mem[i], mem[i+1], mem[i+2], mem[i+3]);
        }
	
	kunmap(bo->pages[0]);
}

static int
pscnv_vram_to_host(struct pscnv_bo* vram)
{
	struct drm_device *dev = vram->dev;
	struct pscnv_bo *sysram;
	uint32_t cookie = (0xa1de << 16) | (vram->cookie & 0xffff);
	int res;
	
	pscnv_dma_init(dev);
	
	sysram = pscnv_mem_alloc(dev, vram->size,
			    PSCNV_GEM_SYSRAM_NOSNOOP,
			    0 /* tile flags */,
			    cookie);
	
	if (!sysram) {
		NV_INFO(dev, "pscnv_vram_to_host: failed to allocate SYSRAM!");
		return -ENOMEM;
	}
	
	res = pscnv_dma_bo_to_bo(sysram, vram);
	
	if (res) {
		NV_INFO(dev, "copy_to_host: failed to DMA- Transfer!");
		return res;
	}
	
	pscnv_swapping_memdump(sysram);
	
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