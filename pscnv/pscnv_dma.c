#include "pscnv_dma.h"
#include "pscnv_mm.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"
#include "pscnv_ib_chan.h"
#include "pscnv_client.h"

static int
pscnv_dma_setup_flags(int flags)
{
	if (pscnv_dma_debug >= 1) {
		flags |= PSCNV_DMA_VERBOSE;
	}
	
	if (pscnv_dma_debug >= 2) {
		flags |= PSCNV_DMA_DEBUG;
	}
	
	if (flags & PSCNV_DMA_DEBUG) {
		flags |= PSCNV_DMA_VERBOSE;
	}
	
	return flags;
}

static void
nvc0_memcpy_m2mf(struct pscnv_ib_chan *chan, const uint64_t dst_addr,
		 const uint64_t src_addr, const uint32_t size, int flags)
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
	
	if (flags & PSCNV_DMA_VERBOSE) {
		char size_str[16];
		pscnv_mem_human_readable(size_str, size);
		NV_INFO(chan->dev, "DMA: M2MF- copy %s from %llx to %llx\n",
			size_str, src_addr, dst_addr);
	}

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

static void
nvc0_memcpy_pcopy0(struct pscnv_ib_chan *chan, const uint64_t dst_addr,
		 const uint64_t src_addr, const uint32_t size, int flags)
{
	static const uint32_t mode = 0x3110; /* QUERY_SHORT|QUERY|SRC_LINEAR|DST_LINEAR */
	static const uint32_t pitch = 0x8000;
	const uint32_t ycnt = size / pitch;
	const uint32_t rem_size = size - ycnt * pitch;
	
	uint64_t dst_pos = dst_addr;
	uint64_t src_pos = src_addr;
	
	if (flags & PSCNV_DMA_VERBOSE) {
		char size_str[16];
		pscnv_mem_human_readable(size_str, size);
		NV_INFO(chan->dev, "DMA: PCOPY0- copy %s from %llx to %llx\n",
			size_str, src_addr, dst_addr);
	}

	if (ycnt) {
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY0, 0x30c, 6);
		OUT_RING(chan, src_pos >> 32);  /* SRC_ADDR_HIGH */
		OUT_RING(chan, src_pos);	/* SRC_ADDR_LOW */
		OUT_RING(chan, dst_pos >> 32);  /* DST_ADDR_HIGH */
		OUT_RING(chan, dst_pos);	/* DST_ADDR_LOW */
		OUT_RING(chan, pitch);		/* SRC_PITCH_IN */
		OUT_RING(chan, pitch);		/* DST_PITCH_IN */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY0, 0x324, 2);
		OUT_RING(chan, pitch);		/* XCNT */
		OUT_RING(chan, ycnt);		/* YCNT */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY0, 0x300, 1);
		OUT_RING(chan, mode);		/* EXEC */
		FIRE_RING(chan);
	}
	
	dst_pos += ycnt * pitch;
	src_pos += ycnt * pitch;
	
	if (rem_size) {
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY0, 0x30c, 6);
		OUT_RING(chan, src_pos >> 32);  /* SRC_ADDR_HIGH */
		OUT_RING(chan, src_pos);	/* SRC_ADDR_LOW */
		OUT_RING(chan, dst_pos >> 32);  /* DST_ADDR_HIGH */
		OUT_RING(chan, dst_pos);	/* DST_ADDR_LOW */
		OUT_RING(chan, rem_size);	/* SRC_PITCH_IN */
		OUT_RING(chan, rem_size);	/* DST_PITCH_IN */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY0, 0x324, 2);
		OUT_RING(chan, rem_size);	/* XCNT */
		OUT_RING(chan, 1);		/* YCNT */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY0, 0x300, 1);
		OUT_RING(chan, mode);		/* EXEC */
		FIRE_RING(chan);
	}
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
	
	subch = GDEV_SUBCH_NV_COMPUTE | GDEV_SUBCH_NV_M2MF | GDEV_SUBCH_NV_PCOPY0;
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

	//No need to undo pscnv_ib_init_subch, since it only performs
	//subchannel configuration on a channel we are about to close anyways...
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
	
	struct timespec start, start_real, end, end_real;
	s64 start_ns, start_real_ns, duration, duration_real;
	
	int ret;
	
	flags = pscnv_dma_setup_flags(flags);
	
	if (!dma) {
		NV_ERROR(dev, "DMA: not available\n");
		return -ENOENT;
	}
	
	BUG_ON(tgt->dev != src->dev);
	
	mutex_lock(&dma->lock);
	
	getnstimeofday(&start_real);
	start_real_ns = timespec_to_ns(&start_real);
	
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
	
	getnstimeofday(&start);
	start_ns = timespec_to_ns(&start);
	
	pscnv_ib_membar(dma->ib_chan);
	
	if (flags & PSCNV_DMA_ASYNC) {
		pscnv_ib_fence_write(dma->ib_chan, GDEV_SUBCH_NV_PCOPY0);
	
		nvc0_memcpy_pcopy0(dma->ib_chan, tgt_node->start, src_node->start, size, flags);
	} else {
		pscnv_ib_fence_write(dma->ib_chan, GDEV_SUBCH_NV_M2MF);
	
		nvc0_memcpy_m2mf(dma->ib_chan, tgt_node->start, src_node->start, size, flags);
	}
	
	
	ret = pscnv_ib_fence_wait(dma->ib_chan);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to wait for fence completion\n");
		goto fail_fence_wait;
	}
	
	getnstimeofday(&end);
	
	/* no return here, always unmap memory */

fail_fence_wait:
	pscnv_vspace_unmap_node(src_node);

fail_map_src:
	pscnv_vspace_unmap_node(tgt_node);
	
fail_map_tgt:
	mutex_unlock(&dma->lock);
	
	getnstimeofday(&end_real);
	
	if (!ret) {
		duration = timespec_to_ns(&end) - start_ns;
		duration_real = timespec_to_ns(&end_real) - start_real_ns;
		if (flags & PSCNV_DMA_VERBOSE) {
			NV_INFO(dev, "DMA: took %lld.%04lld ms (real %lld.%04lld ms)\n",
				duration / 1000000,
				(duration % 1000000) / 100,
				duration_real / 1000000,
				(duration_real % 1000000) / 100);
		}
		
		pscnv_client_track_time(src->client, start_ns, duration, size, "DMA");
		pscnv_client_track_time(src->client, start_ns, duration_real, size, "DMA_REAL");
		if (src->client)
			src->client->pause_bytes_transferred += size;
	}

	return ret;
}

int
pscnv_dma_chunk_to_chunk(struct pscnv_chunk *from, struct pscnv_chunk *to, int flags)
{
	struct drm_device *dev = from->bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_dma *dma = dev_priv->dma;
	struct pscnv_client *client = from->bo->client;
	
	struct pscnv_mm_node *from_node;
	struct pscnv_mm_node *to_node;
	const uint64_t size = pscnv_chunk_size(from);
	
	struct timespec start, start_real, end, end_real;
	s64 start_ns, start_real_ns, duration, duration_real;
	
	int ret;
	
	flags = pscnv_dma_setup_flags(flags);
	
	if (!dma) {
		NV_ERROR(dev, "DMA: not available\n");
		return -ENOENT;
	}
	
	BUG_ON(from->bo->dev != to->bo->dev);
	
	mutex_lock(&dma->lock);
	
	getnstimeofday(&start_real);
	start_real_ns = timespec_to_ns(&start_real);
	
	if (pscnv_chunk_size(to) < size) {
		NV_INFO(dev, "DMA: source chunk %08x/%d-%u has size %lld, but "
			"target chunk %08x/%d-%u only has size %lld\n",
			from->bo->cookie, from->bo->serial, from->idx,
			size, to->bo->cookie, to->bo->serial, to->idx,
			pscnv_chunk_size(to));
		return -ENOSPC;
	}
	
	ret = pscnv_vspace_map_chunk(dma->vs, to,
			0x20000000, /* start */
			1ull << 40, /* end */
			0, /* back, nonsense? */
			&to_node);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to map 'to' chunk\n");
		goto fail_map_to;
	}
	
	ret = pscnv_vspace_map_chunk(dma->vs, from,
			0x20000000, /* start */
			1ull << 40, /* end */
			0, /* back, nonsense? */
			&from_node);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to map 'from' chunk\n");
		goto fail_map_from;
	}
	
	if (flags & PSCNV_DMA_DEBUG) {
		dev_priv->chan->pd_dump_chan(dev, NULL /* seq_file */, PSCNV_DMA_CHAN);
	}
	
	getnstimeofday(&start);
	start_ns = timespec_to_ns(&start);
	
	pscnv_ib_membar(dma->ib_chan);
	
	if (flags & PSCNV_DMA_ASYNC) {
		pscnv_ib_fence_write(dma->ib_chan, GDEV_SUBCH_NV_PCOPY0);
	
		nvc0_memcpy_pcopy0(dma->ib_chan, to_node->start, from_node->start, size, flags);
	} else {
		pscnv_ib_fence_write(dma->ib_chan, GDEV_SUBCH_NV_M2MF);
	
		nvc0_memcpy_m2mf(dma->ib_chan, to_node->start, from_node->start, size, flags);
	}
	
	ret = pscnv_ib_fence_wait(dma->ib_chan);
	
	if (ret) {
		NV_INFO(dev, "DMA: failed to wait for fence completion\n");
		goto fail_fence_wait;
	}
	
	getnstimeofday(&end);
	
	/* no return here, always unmap memory */

fail_fence_wait:
	pscnv_vspace_unmap_node(from_node);

fail_map_from:
	pscnv_vspace_unmap_node(to_node);
	
fail_map_to:
	mutex_unlock(&dma->lock);
	
	getnstimeofday(&end_real);
	
	if (!ret) {
		duration = timespec_to_ns(&end) - start_ns;
		duration_real = timespec_to_ns(&end_real) - start_real_ns;
		if (flags & PSCNV_DMA_VERBOSE) {
			NV_INFO(dev, "DMA: took %lld.%04lld ms (real %lld.%04lld ms)\n",
				duration / 1000000,
				(duration % 1000000) / 100,
				duration_real / 1000000,
				(duration_real % 1000000) / 100);
		}
		pscnv_client_track_time(client, start_ns, duration, size, "DMA");
		pscnv_client_track_time(client, start_ns, duration_real, size, "DMA_REAL");
		if (client)
			client->pause_bytes_transferred += size;

	}

	return ret;
}

