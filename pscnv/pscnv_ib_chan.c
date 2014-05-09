#include "pscnv_ib_chan.h"

#include <linux/highmem.h>
#include <linux/jiffies.h>

#include "pscnv_mem.h"
#include "pscnv_chan.h"

void
pscnv_ib_fence_write(struct pscnv_ib_chan *chan, const int subch)
{
	const uint64_t vm_addr = chan->fence_addr;
	const sequence = chan->fence_seq++;
	
	BUG_ON(!chan->fence)
	switch (subch) {
	case GDEV_SUBCH_NV_COMPUTE:
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_COMPUTE, 0x110, 1);
		OUT_RING(chan, 0); /* SERIALIZE */
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_COMPUTE, 0x1b00, 4);
		OUT_RING(chan, vm_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(chan, vm_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(chan, sequence); /* QUERY_SEQUENCE */
		OUT_RING(chan, 0); /* QUERY_GET */
		break;
	case GDEV_SUBCH_NV_M2MF:
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_M2MF, 0x32c, 3);
		OUT_RING(chan, vm_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(chan, vm_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(chan, sequence); /* QUERY_SEQUENCE */
		break;
	case GDEV_SUBCH_NV_PCOPY0:
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY0, 0x338, 3);
		OUT_RING(chan, vm_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(chan, vm_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(chan, sequence); /* QUERY_COUNTER */
		break;
	case GDEV_SUBCH_NV_PCOPY1:
		BEGIN_NVC0(chan, GDEV_SUBCH_NV_PCOPY1, 0x338, 3);
		OUT_RING(chan, vm_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(chan, vm_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(chan, sequence); /* QUERY_COUNTER */
		break;
	default:
		BUG();
	}

	FIRE_RING(chan);
}

int
pscnv_ib_fence_wait(struct pscnv_ib_chan *chan)
{
	struct drm_device *dev = ib_chan->dev;
	
	/* relax polling after 10ms */
	const unsigned long time_relax = jiffies + HZ/100;
	/* give up after 2s */
	const unsigned long timeout = jiffies + 2*HZ;

	while (chan->fence_seq != *(chan->fence_map)) {
		if (time_after(jiffies, time_relax)) {
			schedule();
		}
		if (time_after(jiffies, timeout)) {
			NV_INFO(dev, "pscnv_ib_fence_wait: timeout waiting for "
				"seq %u on channel %d\n", chan->fence_seq,
				chan->chan->cid);
			return -ETIME;
		}
	}

	return 0;
}

int
pscnv_ib_add_fence(struct pscnv_ib_chan *ib_chan)
{
	struct drm_device *dev = ib_chan->dev;
	
	if (ib_chan->fence) {
		NV_INFO(dev, "pscnv_ib_add_fence: channel %d (vs %d) already has a fence\n",
			ib_chan->chan->cid, ib_chan->chan->vs->vid);
		return -EINVAL;
	}
	
        ib_chan->fence = pscnv_mem_alloc_and_map(dma->vs,
			0x1000, /* size */
			PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_MAPPABLE,
			0xa4de77,
			&dma->fence_addr);
	
	if (!ib_chan->fence) {
		NV_INFO(dev, "pscnv_ib_add_fence: channel %d (vs %d): unable
			to allocate fence buffer\n",
			ib_chan->chan->cid, ib_chan->chan->vs->vid);
		return -ENOSPC;
	}
	
	ib_chan->fence_map = kmap(ib_chan->fence->pages[0]);
	ib_chan->fence_seq = 1;
	
	return 0;
}

int
pscnv_ib_push(struct pscnv_ib_chan *ch, uint32_t start, uint32_t end, int flags)
{
	struct drm_device *dev = ch->dev;
	
	const unsigned long timeout = jiffies + 2*HZ;
	
	uint64_t w = base | (uint64_t)len << 40 | (uint64_t)flags << 40;
	while (((ch->ib_put + 1) & ch->ib_mask) == ch->ib_get) {
		uint32_t old = ch->ib_get;
		ch->ib_get = pscnv_ib_r32(ch, 0x88);
		if (old == ch->ib_get) {
			schedule();
		}
		if (time_after(jiffies, timeout)) {
			NV_INFO(dev, "pscnv_ib_push: timeout waiting for "
				"get pointer, seems frozen at %x on channel %d\n",
				ch->ib_get, ch->chan->cid);
			return -ETIME;
		}
	}
	ch->ib_map[ch->ib_put * 2] = w;
	ch->ib_map[ch->ib_put * 2 + 1] = w >> 32;
	ch->ib_put++;
	ch->ib_put &= ch->ib_mask;
	ch->chmap[0x8c/4] = ch->ib_put;
}

int
pscnv_ib_update_pb_get(struct pscnv_ib_chan *ch)
{
	uint32_t lo = pscnv_ib_r32(ch, 0x58);
	uint32_t hi = pscnv_ib_r32(ch, 0x5c);
	if (hi & 0x80000000) {
		uint64_t mg = ((uint64_t)hi << 32 | lo) & 0xffffffffffull;
		ch->pb_get = mg - ch->pb_base;
	} else {
		ch->pb_get = 0;
	}
}

struct pscnv_ib_chan*
pscnv_ib_chan_new(struct pscnv_vspace *vs, int fake)
{
	struct drm_device *dev = vs->dev;
	struct pscnv_ib_chan *rr;
	int res;
    
	rr = kzalloc(sizeof(struct pscnv_ib_chan), GFP_KERNEL);
	if (!rr) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate"
			" memory for pscnv_ib_chan\n", vs->vid);
		return NULL;
	}
	
	if (!(rr->chan = pscnv_chan_new(dev, dma->vs, fake))) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not create "
			"channel\n", vs->vid);
		goto fail_get_chan;
	}
	
	pscnv_chan_ref(dma->chan);
    
	rr->ib = pscnv_mem_alloc_and_map(dev,
			PSCNV_IB_SIZE,
			PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_MAPPABLE,
			PSCNV_IB_COOKIE,
			&rr->ib_vm_base);

	if (!rr->ib) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate "
			"indirect buffer\n", vs->vid);
		goto fail_ib:
	}

	rr->pb = pscnv_mem_alloc_and_map(dev,
			PSCNV_PB_SIZE,
			PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_MAPPABLE,
			PSCNV_PB_COOKIE,
			&rr->pb_vm_base);
	
	if (!rr->pb) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate "
			"push buffer\n", vs->vid);
		goto fail_pb;
	}

	res = dev_priv->fifo->chan_init_ib(rr->chan,
	 		0, /* pb_handle ??, ignored */
			0, /* flags */
			1, /* slimask */
			rr->ib_vm_base, PSCNV_IB_ORDER);
	
	if (res) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: failed to start
			channel", vs->vid);
		goto fail_init_ib;
	}

	rr->dev = dev;
	rr->chan = chan;
	rr->ib_map = kmap(rr->ib->pages[0]);
	rr->pb_map = kmap(rr->pb->pages[0]);
	
	return rr;

fail_init_ib:
fail_pb:
	pscnv_mem_free(rr->ib);

fail_ib:
	pscnv_chan_unref(rr->chan);

fail_get_chan:
	kfree(rr);

	return NULL;
}