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

int
pscnv_dma_init(struct drm_device *dev)
{	
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_dma *dma;
	int res = 0;
	int subch;
	
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
	
	//pscnv_vspace_ref(dma->vs); // TODO: Why do this? Might have reprecussions on cleanup in pscnv_dma_exit()
	
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
	
	subch = GDEV_SUBCH_NV_COMPUTE | GDEV_SUBCH_NV_M2MF;
	pscnv_ib_init_subch(dma->ib_chan, subch);
	
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


void
pscnv_dma_exit(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_dma *dma = dev_priv->dma;

	if (!dma) {
		NV_WARN(dev, "DMA: pscnv_dma_exit() called but DMA was never initialized!\n");
		return;
	}

	NV_INFO(dev, "DMA: Exiting...\n");

    //No need to undo pscnv_ib_init_subch, since it only seems to do some basic channel setup on the card (?)
    //No need to undo pscnv_ib_add_fence(), as pscnv_ib_chan_kill() inside pscnv_ib_chan_free() takes care of this
	pscnv_ib_chan_free(dma->ib_chan);
	pscnv_vspace_unref(dma->vs);
	//Undo pscnv_vspace_new should not be necessary, as pscnv_vspace_unref() does freeing, unless we have one reference too many
	mutex_destroy(&dma->lock);

	kfree(dma); dev_priv->dma = dma = 0;
}

int
pscnv_dma_bo_to_bo(struct pscnv_bo *tgt, struct pscnv_bo *src, int flags) {
	
	struct drm_device *dev = tgt->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_dma *dma = dev_priv->dma;
	
	struct pscnv_mm_node *tgt_node;
	struct pscnv_mm_node *src_node;
	const uint32_t size = tgt->size;
	
	struct timespec start, end;
	s64 duration;
	
	int ret;
	
	if (!dma) {
		NV_ERROR(dev, "DMA: not available\n");
		return -ENOENT;
	}
	
	BUG_ON(tgt->dev != src->dev);
	
	mutex_lock(&dma->lock);
	
	getnstimeofday(&start);
	
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
	
	if (flags & PSCNV_DMA_DEBUG) {
		dev_priv->chan->pd_dump_chan(dev, NULL /* seq_file */, PSCNV_DMA_CHAN);
	}
	
	pscnv_ib_membar(dma->ib_chan);
	
	pscnv_ib_fence_write(dma->ib_chan, GDEV_SUBCH_NV_M2MF);
	
	nvc0_memcpy_m2mf(dma->ib_chan, tgt_node->start, src_node->start, size);
	
	ret = pscnv_ib_fence_wait(dma->ib_chan);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to wait for fence completion\n");
		goto fail_fence_wait;
	}
	
	getnstimeofday(&end);
	
	duration = timespec_to_ns(&end) - timespec_to_ns(&start);
	NV_INFO(dev, "DMA: took %lld.%04lld ms\n", duration / NSEC_PER_SEC,
		(duration % NSEC_PER_SEC) / 100000);
	
	/* no return here, always unmap memory */

fail_fence_wait:
	pscnv_vspace_unmap_node(src_node);

fail_map_src:
	pscnv_vspace_unmap_node(tgt_node);
	
fail_map_tgt:
	mutex_unlock(&dma->lock);

	return ret;
}

