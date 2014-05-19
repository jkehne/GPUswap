#include "pscnv_dma.h"
#include "pscnv_vm.h"

#if 0
static void
pscnv_swapping_memdump(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	uint32_t pagenum;
	uint32_t i;
	uint32_t *mem;
	
	if (!bo->pages || !bo->pages[0]) {
		NV_INFO(dev, "pscnv_swapping_memdump: can not memdump bo with "
			     "cookie=%x, it has no pages attached\n", bo->cookie);
		return;
	}
	
	// bo->size is a multiple of page size
	for (pagenum = 0; pagenum < bo->size >> PAGE_SHIFT; pagenum++) {
		NV_INFO(dev, "=== DUMP BO %08x/%d page %u\n", bo->cookie, bo->serial, pagenum);
		mem = kmap(bo->pages[pagenum]);
		for (i = 0; i < 256; i += 4) {
			NV_INFO(dev, "%08x %08x %08x %08x\n",
				mem[i], mem[i+1], mem[i+2], mem[i+3]);
		}
		kunmap(bo->pages[pagenum]);
        }
}
#endif

static int
pscnv_swapping_replace(struct pscnv_bo* vram, struct pscnv_bo* sysram)
{
	struct drm_device *dev = vram->dev;
	struct pscnv_mm_node *primary_node = vram->primary_node;
	struct pscnv_mm_node *swapped_node;
	struct pscnv_vspace *vs;
	uint64_t start, end;
	int res;
	
	if (!primary_node) {
		NV_INFO(dev, "pscnv_swapping_replace: BO %08x/%d has no "
			"primary node attached, nothing to do\n",
			vram->cookie, vram->serial);
		return -EINVAL;
	}
	
	BUG_ON(primary_node->tag != vram);
	vs = primary_node->tag2;
	
	start = primary_node->start;
	end = primary_node->start + primary_node->size;
	
	res = pscnv_vspace_unmap_node(primary_node);
	if (res) {
		NV_INFO(dev, "pscnv_swapping_replace: vid=%d BO %08x/%d: failed "
			"to unmap node in range %llx-%llx\n",
			vs->vid, vram->cookie, vram->serial, start, end);
		return res;
	}
	
	res = pscnv_vspace_map(vs, sysram, start, end, 0 /* back */, &swapped_node);
	if (res) {
		NV_INFO(dev, "pscnv_swapping_replace: vid=%d BO %08x/%d: failed "
			"to map in range %llx-%llx\n",
			vs->vid, sysram->cookie, sysram->serial, start, end);
		return res;
	}
	
	return 0;
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
		NV_INFO(dev, "pscnv_vram_to_host: failed to allocate SYSRAM!\n");
		return -ENOMEM;
	}
	
	res = pscnv_dma_bo_to_bo(sysram, vram);
	
	if (res) {
		NV_INFO(dev, "copy_to_host: failed to DMA- Transfer!\n");
		return res;
	}
	
	//pscnv_swapping_memdump(sysram);
	
	res = pscnv_swapping_replace(vram, sysram);
	if (res) {
		NV_INFO(dev, "copy_to_host: failed to replace mapping\n");
		return res;
	}
	
	/* free's the allocated vram, but does not remove the bo itself */
	pscnv_vram_free(vram);
	
	vram->backing_store = sysram;
	
	/* refcnt of sysram now belongs to the vram bo, it will unref it,
	   when it gets free'd itself */
	
	return 0;
}

int
pscnv_bo_copy_to_host(struct pscnv_bo* bo)
{
	if (bo->backing_store) {
		/* already swapped out, nothing to do */
		return 0;
	}
	
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