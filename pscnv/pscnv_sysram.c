#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_mem.h"

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/gfp.h>

static int
pscnv_sysram_vm_fault(struct pscnv_bo *bo, struct vm_area_struct *vma, struct vm_fault *vmf)
{
	uint64_t offset = (uint64_t)vmf->virtual_address - vma->vm_start;
	struct page *res;

	res = bo->pages[offset >> PAGE_SHIFT];
	get_page(res);
	vmf->page = res;
	return 0;
}

int
pscnv_sysram_alloc(struct pscnv_bo *bo)
{
	int numpages, i, j;
	struct drm_nouveau_private *dev_priv = bo->dev->dev_private;
	numpages = bo->size >> PAGE_SHIFT;
	if (numpages > 1 && bo->flags & PSCNV_GEM_CONTIG)
		return -EINVAL;
	bo->pages = kmalloc(numpages * sizeof *bo->pages, GFP_KERNEL);
	if (!bo->pages)
		return -ENOMEM;
	bo->dmapages = kmalloc(numpages * sizeof *bo->dmapages, GFP_KERNEL);
	if (!bo->dmapages) {
		kfree(bo->pages);
		return -ENOMEM;
	}

	for (i = 0; i < numpages; i++) {
		bo->pages[i] = alloc_pages(dev_priv->dma_mask > 0xffffffff ? GFP_DMA32 : GFP_KERNEL, 0);
		if (!bo->pages[i]) {
			for (j = 0; j < i; j++)
				put_page(bo->pages[j]);
			kfree(bo->pages);
			kfree(bo->dmapages);
			return -ENOMEM;
		}
	}
	for (i = 0; i < numpages; i++) {
		bo->dmapages[i] = pci_map_page(bo->dev->pdev, bo->pages[i], 0, PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(bo->dev->pdev, bo->dmapages[i])) {
			for (j = 0; j < i; j++)
				pci_unmap_page(bo->dev->pdev, bo->dmapages[j], PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
			for (j = 0; j < numpages; j++)
				put_page(bo->pages[j]);
			kfree(bo->pages);
			kfree(bo->dmapages);
			return -ENOMEM;
		}
	}
	
	if (!bo->vm_fault) {
		/* we should be the first to touch this BO anyways, but we
		 * definetly don't want to overwrite a more specific fault
		 * handler */
		bo->vm_fault = pscnv_sysram_vm_fault;
	}
	
	return 0;
}

int
pscnv_sysram_free(struct pscnv_bo *bo)
{
	int numpages, i;
	numpages = bo->size >> PAGE_SHIFT;

	for (i = 0; i < numpages; i++)
		pci_unmap_page(bo->dev->pdev, bo->dmapages[i], PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	for (i = 0; i < numpages; i++)
		put_page(bo->pages[i]);

	kfree(bo->pages);
	kfree(bo->dmapages);
	return 0;
}

uint32_t
nv_rv32_sysram(struct pscnv_bo *bo, unsigned offset)
{
	uint32_t *mem;
	uint32_t val;

	mem = kmap_atomic(bo->pages[offset >> PAGE_SHIFT], KM_USER0);
	val = mem[(offset & 0xfff) >> 2];
	kunmap_atomic(mem, KM_USER0);

	return val;
}

void
nv_wv32_sysram(struct pscnv_bo *bo, unsigned offset, uint32_t val)
{
	uint32_t *mem;
	
	mem = kmap_atomic(bo->pages[offset >> PAGE_SHIFT], KM_USER0);
	mem[(offset & 0xfff) >> 2] = val;
	kunmap_atomic(mem, KM_USER0);
}
