#include "pscnv_dma.h"
#include "pscnv_mm.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"
#include "pscnv_ib_chan.h"

#define PSCNV_DMA_VSPACE -1
#define PSCNV_DMA_CHAN   -2

static void nvc0_memcpy_m2mf(struct pscnv_ib_chan *chan, const uint64_t dst_addr,
							 const uint64_t src_addr, const uint32_t size)
{
	const uint32_t mode1 = 0x102110; /* QUERY_SHORT|QUERY_YES|SRC_LINEAR|DST_LINEAR */
	const uint32_t mode2 = 0x100110; /* QUERY_SHORT|SRC_LINEAR|DST_LINEAR */
	const uint32_t page_size = PSCNV_MEM_PAGE_SIZE;
	const uint32_t page_count = size / page_size;
	const uint32_t rem_size = size - page_size * page_count;
	
	

	while (page_count) {
		int line_count = (page_count > 2047) ? 2047 : page_count;
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x238, 2);
		OUT_RING(chan, dst_addr >> 32); /* OFFSET_OUT_HIGH */
		OUT_RING(chan, dst_addr); /* OFFSET_OUT_LOW */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x30c, 6);
		OUT_RING(chan, src_addr >> 32); /* OFFSET_IN_HIGH */
		OUT_RING(chan, src_addr); /* OFFSET_IN_LOW */
		OUT_RING(chan, page_size); /* SRC_PITCH_IN */
		OUT_RING(chan, page_size); /* DST_PITCH_IN */
		OUT_RING(chan, page_size); /* LINE_LENGTH_IN */
		OUT_RING(chan, line_count); /* LINE_COUNT */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x300, 1);
		if (page_count == line_count && rem_size == 0)
			OUT_RING(chan, mode1); /* EXEC */
		else
			OUT_RING(chan, mode2); /* EXEC */
		page_count -= line_count;
		dst_addr += (page_size * line_count);
		src_addr += (page_size * line_count);
	}
	if (rem_size) {
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x238, 2);
		OUT_RING(chan, dst_addr >> 32); /* OFFSET_OUT_HIGH */
		OUT_RING(chan, dst_addr); /* OFFSET_OUT_LOW */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x30c, 6);
		OUT_RING(chan, src_addr >> 32); /* OFFSET_IN_HIGH */
		OUT_RING(chan, src_addr); /* OFFSET_IN_LOW */
		OUT_RING(chan, rem_size); /* SRC_PITCH_IN */
		OUT_RING(chan, rem_size); /* DST_PITCH_IN */
		OUT_RING(chan, rem_size); /* LINE_LENGTH_IN */
		OUT_RING(chan, 1); /* LINE_COUNT */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x300, 1);
		OUT_RING(chan, mode1); /* EXEC */
	}

	FIRE_RING(chan);
}

static struct pscnv_vspace*
pscnv_dma_get_vspace(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct pscnv_vspace *vs = dev_priv->vm->fake_vspaces[-(PSCNV_DMA_VSPACE)];
	
	if (vs == NULL) {
		NV_INFO(dev, "DMA: PSCNV_DMA_VSPACE %d does not exists!\n", PSCNV_DMA_VSPACE);
		return NULL;
	}
	
	if (vs->vid != PSCNV_DMA_VSPACE) {
		NV_INFO(dev, "DMA: PSCNV_DMA_VSPACE has vid %d but should have %d\n",
				vs->vid, PSCNV_DMA_VSPACE);
		return NULL;
	}
}

static void
pscnv_dma_init_m2mf(struct pscnv_dma* dma)
{
	struct pscnv_ib_chan *chan = dma->ib_chan;
	
	for (i = 0; i < 128/4; i++) {
		OUT_RING(chan, 0);
	}
	FIRE_RING(chan);
	BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0, 1);
	OUT_RING(chan, 0x9039); /* M2MF */
	FIRE_RING(chan);
}

int
pscnv_dma_init(struct drm_device *dev)
{	
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_dma *dma;
	int res = 0;
	
	if (dev->dma) {
		NV_INFO(dev, "DMA: already initialized!\n");
		return -EINVAL;
	}
	
	dma = kzalloc(sizeof(struct pscnv_dma), GFP_KERNEL);
	if (!dma) {
		NV_INFO(dev, "DMA: out of memory\n");
		return -ENOMEM;
	}
	
	if (!(dma->vs = pscnv_dma_get_vspace(dev))) {
		res = -ENOENT;
		goto fail_get_vspace;
	}
	
	pscnv_vspace_ref(dma->vs);
	
	if (!(dma->ib_chan = pscnv_ib_chan_new(dma->vs))) {
		NV_INFO(dev, "DMA: Could not create Indirect Buffer Channel\n");
		res = -ENOSPC;
		goto fail_ib_chan;
	}
	
	if (pscnv_ib_add_fence(dma->ib_chan)) {
		NV_INFO(dev, "DMA: Could not create the DMA fence buffer.\n");
		res = -ENOSPC;
		goto fail_fence;
        }
	dma->fence = kmap(bo->pages[0]);
	
	pscnv_dma_init_m2mf(dma);
	
	dev_priv->dma = dma;
	
	return res;

fail_fence:
	pscnv_ib_chan_free(dma->ib_chan);
	
fail_ib_chan:
	pscnv_vspace_unref(dma->vs);
	
fail_get_vspace:
	kfree(dma);
	
	return res;
}

int
pscnv_dma_bo_to_bo(struct pscnv_dma *dma, struct pscnv_bo *tgt,
					  struct pscnv_bo *src) {
	
	struct drm_device *dev = dma->dev;
	
	/* all buffer objects should be mapped to vspace -1 */
	BUG_ON(!tgt->map1);
	BUG_ON(!src->map1);
	
	uint64_t dst_addr = tgt->map1->start;
	uint64_t src_addr = src->map1->start;
	uint32_t size = tgt->size;
	
	if (tgt->size < src->size) {
		NV_INFO(dev, "source bo (cookie=%x) has size %lld, but target bo "
			"(cookie=%x) only has size %lld\n",
			src->cookie, src->size, tgt->cookie, tgt->size);
		return -ENOSPC;
	}
	
	pscnv_ib_write_fence(dma->ib_chan, GDEV_SUBCH_NV_M2MF);
	
	
	nvc0_memcpy_m2mf(dma->ib_chan, dst_addr, src_addr, size);
	
	return pscnv_ib_fence_wait(dma->ib_chan);
}

int pscnv_dma_test(struct pscnv_dma *dma) {
	int ret;
	struct pscnv_ib_bo *src;
	uint64_t write_start = 0;
	uint64_t write_end = 0;
	uint64_t read_start = 0;
	uint64_t read_end = 0;
	uint64_t dma_start = 0;
	uint64_t dma_end = 0;
	uint64_t read_dma_start = 0;
	uint64_t read_dma_end = 0;
	uint64_t size = 0x1000000;
	void *read_buffer = malloc(size);
	void *copy;
	double read, write, dma_copy, read_dma;

	ret = pscnv_ib_bo_alloc(dma->drm_fd, 0, 0, PSCNV_GEM_MAPPABLE, 0,
							size, 0, &src);
	if (ret) {
		fprintf(stderr, "pscnv_virt: Could not create test data.\n");
		return -1;
	}
	write_start = get_time();
	memset(src->map, 0x12, size);
	write_end = get_time();
	read_start = get_time();
	memcpy(read_buffer, src->map, size);
	read_end = get_time();

	dma_start = get_time();
	copy = pscnv_dma_to_sysram(dma, src->handle, size);
	if (copy == NULL) {
		fprintf(stderr, "pscnv_virt: DMA test failed.\n");
		return -1;
	}
	dma_end = get_time();
	read_dma_start = get_time();
	memcpy(read_buffer, copy, size);
	read_dma_end = get_time();
	
	read = (double)(read_end - read_start) / 1000;
	write = (double)(write_end - write_start) / 1000;
	fprintf(stderr, "without DMA: %lf ms write, %lf ms read\n", write, read);
	dma_copy = (double)(dma_end - dma_start) / 1000;
	read_dma = (double)(read_dma_end - read_dma_start) / 1000;
	fprintf(stderr, "DMA: %lf ms copy, %lf ms read\n", dma_copy, read_dma);

	fprintf(stderr, "data: %08x, %08x\n", ((uint32_t*)read_buffer)[0], ((uint32_t*)read_buffer)[0xffffff / 4]);

	pscnv_ib_bo_free(src);
	munmap(copy, size);
	free(read_buffer);
	return -1;
}

int
pscnv_dma_vram_to_host(struct pscnv_bo* bo)
{
	// unfortunately bo->chan == NULL, usually
	struct pscnv_mm_node* n = bo->map1;

	if (!n) {
		NV_INFO(bo->dev, "bo->map1 == NULL\n");
		return -EINVAL;
	}

	NV_INFO(bo->dev, "n.start == %llx, n.size == %llx\n", n->start, n->size);
	
	return 0;
}
#endif
