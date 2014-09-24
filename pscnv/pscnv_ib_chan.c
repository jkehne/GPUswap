#include "pscnv_ib_chan.h"

#include <linux/jiffies.h>

#include "pscnv_mem.h"
#include "pscnv_chan.h"
#include "pscnv_fifo.h"
#include "nvc0_fifo.h"
#include "nvc0_graph.h"

static void
pscnv_ib_dump_fence(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	
	if (!ch->fence) {
		return;
	}
	
	NV_INFO(dev, "channel %d: fence_addr=%08llx fence_seq=0x%x read(fence)=0x%x\n",
		ch->chan->cid, ch->fence_addr, ch->fence_seq, nv_rv32(ch->fence, 0));
}

static void
pscnv_ib_dump_pointers(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	dev_priv->vm->bar_flush(dev);
	
	ch->ib_get = pscnv_ib_ctrl_r32(ch, 0x88);
	ch->ib_put = pscnv_ib_ctrl_r32(ch, 0x8c);
	pscnv_ib_update_pb_get(ch);
	
	NV_INFO(dev, "channel %d: ib_get==%x, ib_put==%x, pb_get==%x, pb_put==%x, pb_pos==%x\n",
		ch->chan->cid, ch->ib_get, ch->ib_put, ch->pb_get, ch->pb_put, ch->pb_pos);
}

static void
pscnv_ib_dump_pb_scan_zero(struct pscnv_ib_chan *ch, uint32_t offset, uint32_t* i, uint32_t len)
{
	struct drm_device *dev = ch->dev;
	uint32_t n_zero = 1;
	
	*i += 1;
	
	while (*i < len) {
		uint32_t val = nv_rv32(ch->pb, offset + 4* (*i));
		if (val == 0) {
			n_zero++;
			*i += 1;
		} else {
			break;
		}
	}
	
	NV_INFO(dev, "channel %d PB:      %d times 0\n", ch->chan->cid, n_zero);
}

static const char*
pscnv_ib_subch_str(uint32_t subch)
{
	switch (subch) {
		case 0: return "SUBCH0";
		case 1: return "COMPUTE";
		case 2: return "M2MF";
		case 3: return "PCOPY0";
		case 4: return "PCOPY1";
		case 5: return "SUBCH5";
		case 6: return "SUBCH6";
		case 7: return "SUBCH7";
	}
	return "???";
}
		

static void
pscnv_ib_dump_pb_cmd(struct pscnv_ib_chan *ch, uint64_t offset, uint32_t* i, uint32_t len, const char *prefix)
{
	struct drm_device *dev = ch->dev;
	char buf[128];
	int pos = 0;
	uint32_t arg;
	
	uint32_t header = nv_rv32(ch->pb, offset + 4* (*i));
	uint32_t mthd   = (header & 0x1FFF) << 2;
	uint32_t argc   = (header >> 16) & 0xFFF;
	/* make sure we don't read too much crap */
	uint32_t myargc = (argc < 10) ? argc : 10;
	uint32_t subch  = (header >> 13) & 0x7;
	
	const char *subch_str = pscnv_ib_subch_str(subch);
	
	
	myargc = (argc < 10) ? argc : 10;
	
	*i += 1;
	
	for (arg = 0; arg < myargc && *i < len; arg++) {
		uint32_t argval = nv_rv32(ch->pb, offset + 4* (*i));
		
		pos += snprintf(buf + pos, 128-pos, "0x%x ", argval);
		
		*i += 1;
	}
	
	if (arg < argc) {
		pos += snprintf(buf + pos, 128-pos, "... +%d", argc - arg);
		
		*i += (argc - arg);
	}
	
	NV_INFO(dev, "channel %d PB:      %s%s(0x%x) %s\n", ch->chan->cid,
		prefix, subch_str, mthd, buf);
}

static void
pscnv_ib_dump_pb_unknown(struct pscnv_ib_chan *ch, uint64_t offset, uint32_t* i, uint32_t len)
{
	struct drm_device *dev = ch->dev;
	
	char buf[128];
	uint32_t n_jump = 1;
	int pos = 0;
	
	*i += 1;
	
	while (*i < len) {
		uint32_t val = nv_rv32(ch->pb, offset + 4* (*i));
		if (val == 0 || (val >> 28) == 0x2 || (val >> 28) == 0x6) {
			break;
		} else {
			n_jump++;
			*i += 1;
			
			if (n_jump < 7) {
				pos += snprintf(buf + pos, 128-pos, "0x%x ", val);
			}
		}
	}
	
	if (n_jump >= 8) {
		pos += snprintf(buf + pos, 128-pos, "... %d", n_jump - 7);
	}
	
	NV_INFO(dev, "channel %d PB:      %s\n", ch->chan->cid, buf);
}

static void
pscnv_ib_dump_pb(struct pscnv_ib_chan *ch, uint64_t offset, uint32_t len)
{
	uint32_t i = 0;
	
	while (i < len) {
		uint32_t header = nv_rv32(ch->pb, offset + 4*i);
		
		if (header == 0) {
			pscnv_ib_dump_pb_scan_zero(ch, offset, &i, len);
		} else if ((header >> 28) == 0x2) {
			pscnv_ib_dump_pb_cmd(ch, offset, &i, len, "");
		} else if ((header >> 28) == 0x6) {
			pscnv_ib_dump_pb_cmd(ch, offset, &i, len, "CONST ");
		} else {
			pscnv_ib_dump_pb_unknown(ch, offset, &i, len);
		}
		
	}
}

static void
pscnv_ib_dump_ib(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t start = (ch->ib_put > 5) ? ch->ib_put - 5 : 0;
	uint32_t i;
	
	dev_priv->vm->bar_flush(dev);
	
	for (i = start; i < ch->ib_put; i++) {
		uint32_t lo, hi, len;
		uint64_t pb_begin;
		uint64_t offset;
		lo = nv_rv32(ch->ib, 4*(i * 2));
		hi = nv_rv32(ch->ib, 4*(i * 2 + 1));
		
		pb_begin = ((uint64_t)hi & 0xff) << 32 | lo;
		offset = pb_begin - ch->pb_vm_base;
		len = hi >> 10;
		NV_INFO(dev, "channel %d IB[0x%x]: 0x%llx  +%u\n", ch->chan->cid, 
			start + i, pb_begin, len);
		
		if (offset >= PSCNV_PB_SIZE) {
			NV_ERROR(dev, "channel %d: PB OUT OF RANGE\n", ch->chan->cid);
			pscnv_chan_fail(ch->chan);
			return;
		}
		
		if (pb_begin != 0) {
			pscnv_ib_dump_pb(ch, offset, len);
		}
	}
}

static uint32_t
pscnv_ib_get_mp_count(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_engine *nvc0_graph;
	
	nvc0_graph = NVC0_GRAPH(dev_priv->engines[PSCNV_ENGINE_GRAPH]);
	return nvc0_graph->tpc_total; /* MPs == TPs */
}

/* call once at initialization time, can except multiple subchannels or'd together*/
void
pscnv_ib_init_subch(struct pscnv_ib_chan *ch, int subch)
{
	int i;
	
	/* clean the FIFO. */
	for (i = 0; i < 128/4; i++) {
		OUT_RING(ch, 0);
	}
	FIRE_RING(ch);

	/* setup subchannels. */
	if (subch & GDEV_SUBCH_NV_M2MF) {
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_M2MF, 0, 1);
		OUT_RING(ch, 0x9039); /* M2MF */
	}
	if (subch & GDEV_SUBCH_NV_COMPUTE) {
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0, 1);
		OUT_RING(ch, 0x90c0); /* COMPUTE */
	}
	if (subch & GDEV_SUBCH_NV_PCOPY0) {
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_PCOPY0, 0, 1);
		OUT_RING(ch, 0x490b5); /* PCOPY0 */
	}
	if (subch & GDEV_SUBCH_NV_PCOPY1) {
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_PCOPY1, 0, 1);
		OUT_RING(ch, 0x590b8); /* PCOPY1 */
	}
	
	FIRE_RING(ch);

	if (subch & GDEV_SUBCH_NV_COMPUTE) {
		/* the blob places NOP at the beginning. */
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x100, 1);
		OUT_RING(ch, 0); /* GRAPH_NOP */
	
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x758, 1);
		OUT_RING(ch, pscnv_ib_get_mp_count(ch)); /* MP_LIMIT */
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0xd64, 1);
		OUT_RING(ch, 0xf); /* CALL_LIMIT_LOG: hardcoded for now */

		/* grid/block initialization. the blob does the following, but not 
		   really sure if they are necessary... */
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x2a0, 1);
		OUT_RING(ch, 0x8000); /* ??? */
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x238, 2);
		OUT_RING(ch, (1 << 16) | 1); /* GRIDDIM_YX */
		OUT_RING(ch, 1); /* GRIDDIM_Z */
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x3ac, 2);
		OUT_RING(ch, (1 << 16) | 1); /* BLOCKDIM_YX */
		OUT_RING(ch, 1); /* BLOCKDIM_X */
		
		FIRE_RING(ch);
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

void
pscnv_ib_membar(struct pscnv_ib_chan *ch)
{
	/* this must be a constant method. */
	BEGIN_NVC0_CONST(ch, GDEV_SUBCH_NV_COMPUTE, 0x21c, 2);
	OUT_RING(ch, 4); /* MEM_BARRIER */
	OUT_RING(ch, 0x1111); /* maybe wait for everything? */

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

	while (chan->fence_seq != nv_rv32(chan->fence, 0)) {
		if (time_after(jiffies, time_relax)) {
			schedule();
		}
		if (time_after(jiffies, timeout)) {
			pscnv_ib_dump_pointers(chan);
			pscnv_ib_dump_ib(chan);
			pscnv_ib_dump_fence(chan);
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
	int i;
	
	if (ib_chan->fence) {
		NV_INFO(dev, "pscnv_ib_add_fence: channel %d (vs %d) already has a fence\n",
			ib_chan->chan->cid, ib_chan->chan->vspace->vid);
		return -EINVAL;
	}
	
        ib_chan->fence = pscnv_mem_alloc_and_map(ib_chan->chan->vspace,
			0x1000, /* size */
			PSCNV_GEM_CONTIG | PSCNV_MAP_USER,
			0xa4de77,
			&ib_chan->fence_addr);
	
	if (!ib_chan->fence) {
		NV_INFO(dev, "pscnv_ib_add_fence: channel %d (vs %d): unable "
			"to allocate fence buffer\n",
			ib_chan->chan->cid, ib_chan->chan->vspace->vid);
		return -ENOSPC;
	}
	
	for (i = 0; i < 0x1000; i += 4) {
		nv_wv32(ib_chan->fence, i, 0);
	}
	
	ib_chan->fence_seq = 0;
	
	return 0;
}

int
pscnv_ib_wait_steady(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	
	/* relax polling after 10ms */
	const unsigned long time_relax = jiffies + HZ/100;
	/* give up after 2s */
	const unsigned long timeout = jiffies + 2*HZ;
	
	do {
		ch->ib_get = pscnv_ib_ctrl_r32(ch, 0x88);
		ch->ib_put = pscnv_ib_ctrl_r32(ch, 0x8c);
		
		if (ch->ib_get * 8 >= PSCNV_IB_SIZE) {
			NV_ERROR(dev, "pscnv_ib_wait_steady: IB=%0x OUT OF RANGE\n", ch->ib_get);
			pscnv_chan_fail(ch->chan);
			ch->ib_get = 0;
			return -EFAULT;
		}
		
		if (time_after(jiffies, time_relax)) {
			schedule();
		}
		
		if (time_after(jiffies, timeout)) {
			pscnv_ib_dump_pointers(ch);
			pscnv_ib_dump_ib(ch);
			pscnv_ib_dump_fence(ch);
			NV_INFO(dev, "pscnv_ib_wait_steady: timeout on channel %d\n",
				ch->chan->cid);
			return -ETIME;
		}
	} while (ch->ib_get != ch->ib_put);
	
	return 0;
}

int
pscnv_ib_move_ib_get(struct pscnv_ib_chan *ch, int pos)
{
	struct drm_device *dev = ch->dev;
	
	int ret;
	int n_nops;
	int i;
	
	BUG_ON(pos < 0 || pos * 8 >= PSCNV_IB_SIZE); /* out of range */

	/* use ib_put here, we may still have something else waiting */
	n_nops = pos - ch->ib_put;
	
	if (n_nops < 0) {
		n_nops += 512;
	}
	
	for (i = 0; i < n_nops; i++) {
		BEGIN_NVC0(ch, GDEV_SUBCH_NV_COMPUTE, 0x100, 1); /* 0x100 = NOP */
		OUT_RING(ch, 0);
		FIRE_RING(ch);
	}
	
	ret = pscnv_ib_wait_steady(ch);
	if (ret) {
		NV_ERROR(dev, "pscnv_ib_move_ib_get: failed to wait after move\n");
		return ret;
	}

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
	
	if (pscnv_chan_get_state(ch->chan) == PSCNV_CHAN_FAILED) {
		return -EFAULT; /* an error message has already been issued */
	}
	
	while (((ch->ib_put + 1) & PSCNV_IB_MASK) == ch->ib_get) {
		uint32_t old = ch->ib_get;
		ch->ib_get = pscnv_ib_ctrl_r32(ch, 0x88);
		
		if (ch->ib_get * 8 >= PSCNV_IB_SIZE) {
			NV_ERROR(dev, "pscnv_ib_push: IB=%0x OUT OF RANGE\n", ch->ib_get);
			pscnv_chan_fail(ch->chan);
			ch->ib_get = 0;
			return -EFAULT;
		}
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
	
	
	nv_wv32(ch->ib, 4*(ch->ib_put * 2), w);
	nv_wv32(ch->ib, 4*(ch->ib_put * 2 + 1), w >> 32);
	nv_rv32(ch->ib, 4*(ch->ib_put * 2 + 1));
	ch->ib_put++;
	ch->ib_put &= PSCNV_IB_MASK;
	
	mb();
	nv_rv32(ch->pb, 0);
	
	dev_priv->vm->bar_flush(dev);
	
	pscnv_ib_ctrl_w32(ch, 0x8c, ch->ib_put);
	
	dev_priv->vm->bar_flush(dev);
	
	return 0;
}

void
pscnv_ib_update_pb_get(struct pscnv_ib_chan *ch)
{
	struct drm_device *dev = ch->dev;
	
	uint32_t lo = pscnv_ib_ctrl_r32(ch, 0x58);
	uint32_t hi = pscnv_ib_ctrl_r32(ch, 0x5c);
	
	if (hi & 0x80000000) {
		uint64_t mg = ((uint64_t)hi << 32 | lo) & 0xffffffffffull;
		ch->pb_get = mg - ch->pb_vm_base;
	} else {
		ch->pb_get = 0;
	}
	
	NV_INFO(ch->dev, "pb_vm_base=%llx, pb_get==%x\n", ch->pb_vm_base, ch->pb_get);
	
	if (ch->pb_get >= PSCNV_PB_SIZE) {
		NV_ERROR(dev, "channel %d: PB=%0x OUT OF RANGE\n",
			ch->chan->cid, ch->pb_get);
		pscnv_chan_fail(ch->chan);
		ch->pb_get = 0;
	}
	
	return;
}


struct pscnv_ib_chan *
pscnv_ib_chan_init(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	
	struct nvc0_fifo_ctx *fifo_ctx = ch->engdata[PSCNV_ENGINE_FIFO];
	struct pscnv_ib_chan *ibch;
	
	if (!fifo_ctx) {
		NV_ERROR(dev, "pscnv_ib_chan_init: on channel %d: no fifo "
			"context! fifo_init_ib() not yet called?\n", ch->cid);
		goto fail_fifo_ctx;
	}
	
	BUG_ON(!fifo_ctx->ib);
	
	ibch = kzalloc(sizeof(struct pscnv_ib_chan), GFP_KERNEL);
	if (!ibch) {
		NV_ERROR(dev, "pscnv_ib_chan_init: on channel %d: could not allocate"
			" memory for pscnv_ib_chan\n", ch->cid);
		goto fail_kzalloc;
	}
	
	ibch->dev = dev;
	ibch->chan = ch;
	
	ibch->ctrl_bo = fifo->ctrl_bo;
	ibch->ctrl_offset = ch->cid << 12;
	
	ibch->ib = fifo_ctx->ib;
	
	ibch->pb = pscnv_mem_alloc_and_map(ch->vspace,
			PSCNV_PB_SIZE,
			PSCNV_GEM_CONTIG | PSCNV_MAP_USER,
			PSCNV_PB_COOKIE,
			&ibch->pb_vm_base);
	
	if (!ibch->pb) {
		NV_ERROR(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate "
			"push buffer\n", ch->vspace->vid);
		goto fail_pb;
	}
	
	return ibch;
	
fail_pb:
	kfree(ibch);

fail_kzalloc:
fail_fifo_ctx:
	return NULL;
}

struct pscnv_ib_chan*
pscnv_ib_chan_new(struct pscnv_vspace *vs, int fake)
{
	struct drm_device *dev = vs->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	uint64_t ib_vm_base;
	struct pscnv_bo *ib;
	struct pscnv_chan *ch;
	struct pscnv_ib_chan *ibch;
	int res;

	if (!(ch = pscnv_chan_new(dev, vs, fake))) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not create "
			"channel\n", vs->vid);
		goto fail_get_chan;
	}

	ib = pscnv_mem_alloc_and_map(vs,
			PSCNV_IB_SIZE,
			PSCNV_GEM_CONTIG | PSCNV_MAP_USER,
			PSCNV_IB_COOKIE,
			&ib_vm_base);

	if (!ib) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: could not allocate "
			"indirect buffer\n", vs->vid);
		goto fail_ib;
	}

	res = dev_priv->fifo->chan_init_ib(ch,
	 		0, /* pb_handle ??, ignored */
			0, /* flags */
			0, /* slimask */
			ib_vm_base, PSCNV_IB_ORDER);

	if (res) {
		NV_INFO(dev, "pscnv_ib_chan_new: on vspace %d: failed to start "
			"channel %d", vs->vid, ch->cid);
		goto fail_init_ib;
	}

	ibch = pscnv_ib_chan_init(ch);

	if (!ibch) {
		NV_INFO(dev, "pscnv_ib_chan_new: pscnv_ib_chan_init failed for "
			"channel %d\n", ch->cid);
		goto fail_init_ibch;
	}

	return ibch;

fail_init_ibch:
fail_init_ib:
	pscnv_mem_free(ib);

fail_ib:
	pscnv_chan_unref(ch);

fail_get_chan:
	return NULL;
}

/* this is the counterpart to pscnv_ib_chan_init */
void
pscnv_ib_chan_kill(struct pscnv_ib_chan *ib_chan)
{
	if (ib_chan->fence) {
		pscnv_mem_free(ib_chan->fence);
	}
	
	pscnv_mem_free(ib_chan->pb);
	kfree(ib_chan);
}

/* this is the counterpart to pscnv_ib_chan_new */
void
pscnv_ib_chan_free(struct pscnv_ib_chan *ib_chan)
{
	/* TODO: This was a quick and dirty solution by Stanislav.
	         It is probably faulty and needs revision,
	         e.g. by Jonathan */
	struct pscnv_bo *ib = ib_chan->ib;
	struct pscnv_chan *ch = ib_chan->chan;

	pscnv_ib_chan_kill(ib_chan);
	// Does  dev_priv->fifo->chan_init_ib from _new() need undo-ing? It is not undo-ed over there!
	pscnv_mem_free(ib);
	pscnv_chan_unref(ch);
}

void
FIRE_RING(struct pscnv_ib_chan *ch)
{
	if (pscnv_chan_get_state(ch->chan) == PSCNV_CHAN_FAILED) {
		return; /* an error message has already been issued */
	}
	
	if (ch->pb_pos != ch->pb_put) {
		if (ch->pb_pos > ch->pb_put) {
			pscnv_ib_push(ch, ch->pb_put, ch->pb_pos - ch->pb_put, 0);
		} else {
			pscnv_ib_push(ch, ch->pb_put, PSCNV_PB_SIZE - ch->pb_put, 0);
			if (ch->pb_pos > 0) {
				pscnv_ib_push(ch, 0, ch->pb_pos, 0);
			}
		}
		ch->pb_put = ch->pb_pos;
	}
	// else: nothing to fire
}

void
OUT_RING(struct pscnv_ib_chan *ch, uint32_t word)
{
	struct drm_device *dev = ch->dev;
	
	const unsigned long timeout = jiffies + 2*HZ;
	
	if (pscnv_chan_get_state(ch->chan) == PSCNV_CHAN_FAILED) {
		return; /* an error message has already been issued */
	}
		
	while (((ch->pb_pos + 4) & PSCNV_PB_MASK) == ch->pb_get) {
		uint32_t old = ch->pb_get;
		FIRE_RING(ch);
		pscnv_ib_update_pb_get(ch);
		if (old == ch->pb_get) {
			schedule();
		}
		if (time_after(jiffies, timeout)) {
			NV_INFO(dev, "OUT_RING: timeout waiting for PB "
				"get pointer, seems frozen at %x on channel %d\n",
				ch->pb_get, ch->chan->cid);
			return;
		}
	}
	
	nv_wv32(ch->pb, ch->pb_pos, word);
	ch->pb_pos += 4;
	ch->pb_pos &= PSCNV_PB_MASK;
}