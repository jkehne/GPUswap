#include "pscnv_dma.h"
#include "pscnv_mm.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"
#include "pscnv_ib_chan.h"

static void nvc0_memcpy_m2mf(struct pscnv_ib_chan *chan, const uint64_t dst_addr,
							 const uint64_t src_addr, const uint32_t size)
{
	/* MODE 1 means fire fence */
	static const uint32_t mode1 = 0x102110; /* QUERY_SHORT|QUERY_YES|SRC_LINEAR|DST_LINEAR */
	static const uint32_t mode2 = 0x100110; /* QUERY_SHORT|SRC_LINEAR|DST_LINEAR */
	static const uint32_t page_size = PSCNV_MEM_PAGE_SIZE;
	const uint32_t page_count = size / page_size;
	const uint32_t rem_size = size - page_size * page_count;
	
	uint64_t dst_pos = dst_addr;
	uint64_t src_pos = src_addr;
	uint32_t pages_left = page_count;
	
	NV_INFO(chan->dev, "DMA: M2MF- copy 0x%x bytes from %llx to %llx\n",
		size, src_addr, dst_addr);

	while (pages_left) {
		int line_count = (pages_left > 2047) ? 2047 : pages_left;
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x238, 2);
		OUT_RING(chan, dst_pos >> 32); /* OFFSET_OUT_HIGH */
		OUT_RING(chan, dst_pos); /* OFFSET_OUT_LOW */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x30c, 6);
		OUT_RING(chan, src_pos >> 32); /* OFFSET_IN_HIGH */
		OUT_RING(chan, src_pos); /* OFFSET_IN_LOW */
		OUT_RING(chan, page_size); /* SRC_PITCH_IN */
		OUT_RING(chan, page_size); /* DST_PITCH_IN */
		OUT_RING(chan, page_size); /* LINE_LENGTH_IN */
		OUT_RING(chan, line_count); /* LINE_COUNT */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x300, 1);
		if (pages_left == line_count && rem_size == 0)
			OUT_RING(chan, mode1); /* EXEC */
		else
			OUT_RING(chan, mode2); /* EXEC */
		pages_left -= line_count;
		dst_pos += (page_size * line_count);
		src_pos += (page_size * line_count);
	}
	if (rem_size) {
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x238, 2);
		OUT_RING(chan, dst_pos >> 32); /* OFFSET_OUT_HIGH */
		OUT_RING(chan, dst_pos); /* OFFSET_OUT_LOW */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x30c, 6);
		OUT_RING(chan, src_pos >> 32); /* OFFSET_IN_HIGH */
		OUT_RING(chan, src_pos); /* OFFSET_IN_LOW */
		OUT_RING(chan, rem_size); /* SRC_PITCH_IN */
		OUT_RING(chan, rem_size); /* DST_PITCH_IN */
		OUT_RING(chan, rem_size); /* LINE_LENGTH_IN */
		OUT_RING(chan, 1); /* LINE_COUNT */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x300, 1);
		OUT_RING(chan, mode1); /* EXEC */
	}

	FIRE_RING(chan);
}

#if 0
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
	
	return vs;
}
#endif

static void
pscnv_dma_init_m2mf(struct pscnv_dma* dma)
{
	struct pscnv_ib_chan *chan = dma->ib_chan;
	int i;
	
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
	
	NV_INFO(dev, "DMA: Initializing...\n");
	
	if (dev_priv->dma) {
		NV_INFO(dev, "DMA: already initialized!\n");
		return -EINVAL;
	}
	
	dma = kzalloc(sizeof(struct pscnv_dma), GFP_KERNEL);
	if (!dma) {
		NV_INFO(dev, "DMA: out of memory\n");
		return -ENOMEM;
	}
	dma->dev = dev;
	mutex_init(&dma->lock);
	
	/*if (!(dma->vs = pscnv_dma_get_vspace(dev))) {
		res = -ENOENT;
		goto fail_get_vspace;
	}*/
	
	dma->vs = pscnv_vspace_new(dev,
				   1ull << 40, /* vspace size */
				   0, /* flags */
				   PSCNV_DMA_VSPACE);

	if (!dma->vs) {
		NV_INFO(dev, "DMA: Failed to allocate DMA vspace %d\n",
				PSCNV_DMA_VSPACE);
		res = -ENOSPC;
		goto fail_alloc_vs;
	}
	
	pscnv_vspace_ref(dma->vs);
	
	if (!(dma->ib_chan = pscnv_ib_chan_new(dma->vs, PSCNV_DMA_CHAN))) {
		NV_INFO(dev, "DMA: Could not create Indirect Buffer Channel\n");
		res = -ENOSPC;
		goto fail_ib_chan;
	}
	
	if (pscnv_ib_add_fence(dma->ib_chan)) {
		NV_INFO(dev, "DMA: Could not create the DMA fence buffer.\n");
		res = -ENOSPC;
		goto fail_fence;
        }
	
	pscnv_dma_init_m2mf(dma);
	
	dev_priv->dma = dma;
	
	return res;

fail_fence:
	pscnv_ib_chan_free(dma->ib_chan);
	
fail_ib_chan:
	pscnv_vspace_unref(dma->vs);
	
//fail_get_vspace:
fail_alloc_vs:
	kfree(dma);
	
	return res;
}

int
pscnv_dma_bo_to_bo(struct pscnv_bo *tgt, struct pscnv_bo *src) {
	
	struct drm_device *dev = tgt->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_dma *dma = dev_priv->dma;
	
	struct pscnv_mm_node *tgt_node;
	struct pscnv_mm_node *src_node;
	const uint32_t size = tgt->size;
	
	int ret;
	
	if (!dma) {
		NV_ERROR(dev, "DMA: not available\n");
		return -ENOENT;
	}
	
	BUG_ON(tgt->dev != src->dev);
	
	mutex_lock(&dma->lock);
	
	if (tgt->size < src->size) {
		NV_INFO(dev, "DMA: source bo (cookie=%x) has size %lld, but target bo "
			"(cookie=%x) only has size %lld\n",
			src->cookie, src->size, tgt->cookie, tgt->size);
		return -ENOSPC;
	}
	
	ret = pscnv_vspace_map(dma->vs, tgt,
			0x20000000, /* start */
			1ull << 40, /* end */
			0, /* back, nonsense? */
			&tgt_node);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to map target bo\n");
		goto fail_map_tgt;
	}
	
	ret = pscnv_vspace_map(dma->vs, src,
			0x20000000, /* start */
			1ull << 40, /* end */
			0, /* back, nonsense? */
			&src_node);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to map source bo\n");
		goto fail_map_src;
	}
	
	pscnv_ib_fence_write(dma->ib_chan, GDEV_SUBCH_NV_M2MF);
	
	nvc0_memcpy_m2mf(dma->ib_chan, tgt_node->start, src_node->start, size);
	
	ret = pscnv_ib_fence_wait(dma->ib_chan);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to wait for fence completion\n");
		goto fail_fence_wait;
	}
	
	/* no return here, always unmap memory */

fail_fence_wait:
	pscnv_vspace_unmap_node(src_node);

fail_map_src:
	pscnv_vspace_unmap_node(tgt_node);
	
fail_map_tgt:
	mutex_unlock(&dma->lock);

	return ret;
}

#if 0
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
