#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_mem.h"
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/gfp.h>

int
pscnv_sysram_alloc(struct pscnv_bo *bo)
{
	int numpages, i, j;
	gfp_t gfp_flags;
	numpages = bo->size / PAGE_SIZE;
	if (numpages > 1 && bo->flags & PSCNV_GEM_CONTIG)
		return -EINVAL;
	bo->pages = kmalloc(numpages * sizeof *bo->pages, GFP_KERNEL);
	if (!bo->pages)
		return -ENOMEM;
	if (bo->dev->pdev->dma_mask > 0xffffffff)
		gfp_flags = GFP_KERNEL;
	else
		gfp_flags = GFP_DMA32;
	for (i = 0; i < numpages; i++) {
		bo->pages[i] = alloc_pages(gfp_flags, 0);
		if (!bo->pages[i]) {
			for (j = 0; j < i; j++)
				put_page(bo->pages[j]);
			kfree(bo->pages);
			return -ENOMEM;
		}
	}
	return 0;
}

int
pscnv_sysram_free(struct pscnv_bo *bo)
{
	int numpages, i;
	numpages = bo->size / PAGE_SIZE;
	for (i = 0; i < numpages; i++)
		put_page(bo->pages[i]);
	kfree(bo->pages);
	return 0;
}
