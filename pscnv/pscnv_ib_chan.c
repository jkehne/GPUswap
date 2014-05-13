#include "pscnv_ib_chan.h"

#include <linux/highmem.h>
#include <linux/jiffies.h>

#include "pscnv_mem.h"
#include "pscnv_chan.h"
#include "pscnv_fifo.h"
#include "nvc0_fifo.h"

static void
pscnv_ib_dump_pointers(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	dev_priv->vm->bar_flush(dev);
	
	//ch->ib_get = pscnv_ib_r32(ch, 0x88);
	ch->ib_get = pscnv_ib_ctrl_r32(ch, 0x88);
	//ch->ib_put = pscnv_ib_r32(ch, 0x8c);
	ch->ib_put = pscnv_ib_ctrl_r32(ch, 0x8c);
	pscnv_ib_update_pb_get(ch);
	
	NV_INFO(dev, "channel %d: ib_get==%x, ib_put==%x, pb_get==%x, pb_put==%x, pb_pos==%x\n",
		ch->chan->cid, ch->ib_get, ch->ib_put, ch->pb_get, ch->pb_put, ch->pb_pos);
}

static void
pscnv_ib_dump_pb(struct pscnv_ib_chan *ch, uint64_t offset)
{
	struct drm_device *dev = ch->dev;
	
	int i;
	uint32_t words[5];
	
	for (i = 0; i < 5; i++) {
		words[i] = nv_rv32(ch->pb, offset + 4*i);
	}
	
	NV_INFO(dev, "channel %d PB:      %x %x %x %x %x...\n", ch->chan->cid,
			words[0], words[1], words[2], words[3], words[4]);
}

static void
pscnv_ib_dump_ib(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;
	
	dev_priv->vm->bar_flush(dev);
	
	for (i = 0; i < 10; i++) {
		uint32_t lo, hi, len;
		uint64_t start;
		lo = nv_rv32(ch->ib, 4*(i * 2));
		hi = nv_rv32(ch->ib, 4*(i * 2 + 1));
		
		start = ((uint64_t)hi & 0xff) << 32 | lo;
		len = hi >> 10;
		NV_INFO(dev, "channel %d IB: %llx  +%u\n", ch->chan->cid, start, len);
		
		if (start != 0) {
			pscnv_ib_dump_pb(ch, start - ch->pb_vm_base);
		}
	}
}

void
pscnv_ib_fence_write(struct pscnv_ib_chan *ch, const int subch)
{
	if (!ch->fence) {
		NV_ERROR(ch->dev, "pscnv_ib_fence_write: no fence installed "
			"for ib_chan on channel %d\n", ch->chan->cid);
		return;
	}
	
	ch->fence_seq++;
	
	switch (subch) {
	case GDEV_SUBCH_NV_COMPUTE:
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x110, 1);
		OUT_RING(ch, 0); /* SERIALIZE */
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x1b00, 4);
		OUT_RING(ch, ch->fence_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(ch, ch->fence_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(ch, ch->fence_seq); /* QUERY_SEQUENCE */
		OUT_RING(ch, 0); /* QUERY_GET */
		break;
	case GDEV_SUBCH_NV_M2MF:
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_M2MF, 0x32c, 3);
		OUT_RING(ch, ch->fence_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(ch, ch->fence_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(ch, ch->fence_seq); /* QUERY_SEQUENCE */
		break;
	case GDEV_SUBCH_NV_PCOPY0:
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_PCOPY0, 0x338, 3);
		OUT_RING(ch, ch->fence_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(ch, ch->fence_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(ch, ch->fence_seq); /* QUERY_COUNTER */
		break;
	case GDEV_SUBCH_NV_PCOPY1:
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_PCOPY1, 0x338, 3);
		OUT_RING(ch, ch->fence_addr >> 32); /* QUERY_ADDRESS HIGH */
		OUT_RING(ch, ch->fence_addr); /* QUERY_ADDRESS LOW */
		OUT_RING(ch, ch->fence_seq); /* QUERY_COUNTER */
		break;
	default:
		NV_ERROR(ch->dev, "pscnv_ib_fence_write: unsupported "
			          "subchannel %x\n", subch);
		return;
	}

	FIRE_RING(ch);
}

int
pscnv_ib_fence_wait(struct pscnv_ib_chan *chan)
{
	struct drm_device *dev = chan->dev;
	
	/* relax polling after 10ms */
	const unsigned long time_relax = jiffies + HZ/100;
	/* give up after 2s */
	const unsigned long timeout = jiffies + 2*HZ;

	//while (chan->fence_seq != chan->fence_map[0]) {
	while (chan->fence_seq != nv_rv32(chan->fence, 0)) {
		if (time_after(jiffies, time_relax)) {
			schedule();
		}
		if (time_after(jiffies, timeout)) {
			pscnv_ib_dump_pointers(chan);
			pscnv_ib_dump_ib(chan);
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	if (ib_chan->fence) {
		NV_INFO(dev, "pscnv_ib_add_fence: channel %d (vs %d) already has a fence\n",
			ib_chan->chan->cid, ib_chan->chan->vspace->vid);
		return -EINVAL;
	}
	
        ib_chan->fence = pscnv_mem_alloc_and_map(ib_chan->chan->vspace,
			0x1000, /* size */
//			PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_MAPPABLE,
			PSCNV_GEM_CONTIG,
			0xa4de77,
			&ib_chan->fence_addr);
	
	if (!ib_chan->fence) {
		NV_INFO(dev, "pscnv_ib_add_fence: channel %d (vs %d): unable "
			"to allocate fence buffer\n",
			ib_chan->chan->cid, ib_chan->chan->vspace->vid);
		return -ENOSPC;
	}
	
	dev_priv->vm->map_kernel(ib_chan->fence);
	
	//ib_chan->fence_map = kmap(ib_chan->fence->pages[0]);
	ib_chan->fence_seq = 0;
	
	return 0;
}

int
pscnv_ib_push(struct pscnv_ib_chan *ch, uint32_t start, uint32_t len, int flags)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	const unsigned long timeout = jiffies + 2*HZ;
	
	const uint64_t base = ch->pb_vm_base + start;
	const uint64_t w = base | (uint64_t)len << 40 | (uint64_t)flags << 40;
	while (((ch->ib_put + 1) & PSCNV_IB_MASK) == ch->ib_get) {
		uint32_t old = ch->ib_get;
		//ch->ib_get = pscnv_ib_r32(ch, 0x88);
		ch->ib_get = pscnv_ib_ctrl_r32(ch, 0x88);
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
	//ch->ib_map[ch->ib_put * 2] = w;
	nv_wv32(ch->ib, 4*(ch->ib_put * 2), w);
	//ch->ib_map[ch->ib_put * 2 + 1] = w >> 32;
	nv_wv32(ch->ib, 4*(ch->ib_put * 2 + 1), w >> 32);
	ch->ib_put++;
	ch->ib_put &= PSCNV_IB_MASK;
	
	mb();
	nv_rv32(ch->pb, ch->pb_pos);
	nv_rv32(ch->ib, 4*(ch->ib_put * 2 + 1));
	dev_priv->vm->bar_flush(dev);
	
	
	//pscnv_ib_w32(ch, 0x8c, ch->ib_put);
	pscnv_ib_ctrl_w32(ch, 0x8c, ch->ib_put);
	pscnv_ib_ctrl_r32(ch, 0x8c);
	
	dev_priv->vm->bar_flush(dev);
	
	return 0;
}

void
pscnv_ib_update_pb_get(struct pscnv_ib_chan *ch)
{
	//uint32_t lo = pscnv_ib_r32(ch, 0x58);
	uint32_t lo = pscnv_ib_ctrl_r32(ch, 0x58);
	//uint32_t hi = pscnv_ib_r32(ch, 0x5c);
	uint32_t hi = pscnv_ib_ctrl_r32(ch, 0x5c);
	
	if (hi & 0x80000000) {
		uint64_t mg = ((uint64_t)hi << 32 | lo) & 0xffffffffffull;
		ch->pb_get = mg - ch->pb_vm_base;
	} else {
		ch->pb_get = 0;
	}
	NV_INFO(ch->dev, "pb_vm_base=%llx, pb_get==%x\n", ch->pb_vm_base, ch->pb_get);
}

struct pscnv_ib_chan*
pscnv_ib_chan_new(struct pscnv_vspace *vs, int fake)
{
	struct drm_device *dev = vs->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo = nvc0_fifo(dev_priv->fifo);
	struct pscnv_ib_chan *rr;
	int res;
    
	rr = kzalloc(sizeof(struct pscnv_ib_chan), GFP_KERNEL);
	if (!rr) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate"
			" memory for pscnv_ib_chan\n", vs->vid);
		return NULL;
	}
	
	rr->dev = dev;
	
	if (!(rr->chan = pscnv_chan_new(dev, vs, fake))) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not create "
			"channel\n", vs->vid);
		goto fail_get_chan;
	}
	
	pscnv_chan_ref(rr->chan);
	
	rr->ctrl = fifo->fifo_ctl;
	rr->ctrl_offset = rr->chan->cid << 12;
    
	rr->ib = pscnv_mem_alloc_and_map(vs,
			PSCNV_IB_SIZE,
//			PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_MAPPABLE,
			PSCNV_GEM_CONTIG,
			PSCNV_IB_COOKIE,
			&rr->ib_vm_base);

	if (!rr->ib) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate "
			"indirect buffer\n", vs->vid);
		goto fail_ib;
	}
	/*if (!rr->ib->pages || !rr->ib->pages[0]) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: no sysram pages "
			"allocated for indirect buffer\n", vs->vid);
		goto fail_ib;
	}*/
	
	dev_priv->vm->map_kernel(rr->ib);

	rr->pb = pscnv_mem_alloc_and_map(vs,
			PSCNV_PB_SIZE,
//			PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_MAPPABLE,
			PSCNV_GEM_CONTIG,
			PSCNV_PB_COOKIE,
			&rr->pb_vm_base);
	
	if (!rr->pb) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate "
			"push buffer\n", vs->vid);
		goto fail_pb;
	}
	/*if (!rr->pb->pages || !rr->pb->pages[0]) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: no sysram pages "
			"allocated for push buffer\n", vs->vid);
		goto fail_pb;
	}*/
	
	dev_priv->vm->map_kernel(rr->pb);

	res = dev_priv->fifo->chan_init_ib(rr->chan,
	 		0, /* pb_handle ??, ignored */
			0, /* flags */
			0, /* slimask */
			rr->ib_vm_base, PSCNV_IB_ORDER);
	
	if (res) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: failed to start "
			"channel", vs->vid);
		goto fail_init_ib;
	}

	//rr->ib_map = kmap(rr->ib->pages[0]);
	//rr->pb_map = kmap(rr->pb->pages[0]);
	
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

void
pscnv_ib_chan_free(struct pscnv_ib_chan *ib_chan)
{
	/* TODO */
}