#ifndef PSCNV_IB_CHAN_H
#define PSCNV_IB_CHAN_H

#include "nouveau_drv.h"
#include "pscnv_chan.h"

#define GDEV_SUBCH_NV_COMPUTE 1
#define GDEV_SUBCH_NV_M2MF    2
#define GDEV_SUBCH_NV_PCOPY0  3
#define GDEV_SUBCH_NV_PCOPY1  4

#define PSCNV_IB_COOKIE 0xf1f01b
#define PSCNV_PB_COOKIE 0xf1f0
#define PSCNV_IB_ORDER 9
#define PSCNV_PB_ORDER 20
#define PSCNV_IB_MASK  ((1 << PSCNV_IB_ORDER) - 1)
#define PSCNV_IB_SIZE  ((8 << PSCNV_IB_ORDER))
#define PSCNV_PB_MASK  ((1 << PSCNV_PB_ORDER) - 1)
#define PSCNV_PB_SIZE  ((1 << PSCNV_PB_ORDER))

struct pscnv_chan;

struct pscnv_ib_chan {
	struct drm_device *dev;
	struct pscnv_chan *chan;
	
	/* Channel Control */
	struct drm_local_map *ctrl;
	uint32_t ctrl_offset;

	/* FIFO indirect buffer setup. */
	struct pscnv_bo *ib;
	uint64_t ib_vm_base;
	volatile uint32_t *ib_map;
	uint32_t ib_put;
	uint32_t ib_get;

	/* FIFO push buffer setup. */
	struct pscnv_bo *pb;
	uint64_t pb_vm_base;
	volatile uint32_t *pb_map;
	uint32_t pb_pos;
	uint32_t pb_put;
	uint32_t pb_get;
	
	/* Fence buffer */
	struct pscnv_bo *fence;
	volatile uint32_t *fence_map;
	uint64_t fence_addr;
	uint32_t fence_seq;

};

struct pscnv_ib_chan*
pscnv_ib_chan_new(struct pscnv_vspace *vs, int fake);

void
pscnv_ib_chan_free(struct pscnv_ib_chan *ib_chan);

int
pscnv_ib_add_fence(struct pscnv_ib_chan *ib_chan);

void
pscnv_ib_fence_write(struct pscnv_ib_chan *chan, const int subch);

int
pscnv_ib_fence_wait(struct pscnv_ib_chan *chan);

int
pscnv_ib_push(struct pscnv_ib_chan *ch, uint32_t start, uint32_t len, int flags);

void
pscnv_ib_update_pb_get(struct pscnv_ib_chan *ch);

#if 0
static inline void
pscnv_ib_w32(struct pscnv_ib_chan *chan_ib, uint32_t offset, uint32_t val)
{
	nv_wv32(chan_ib->chan->bo, offset, val);
}

static inline uint32_t
pscnv_ib_r32(struct pscnv_ib_chan *chan_ib, uint32_t offset)	
{
	return nv_rv32(chan_ib->chan->bo, offset);
}
#endif

static inline void
pscnv_ib_ctrl_w32(struct pscnv_ib_chan *chan_ib, uint32_t offset, uint32_t val)
{
	DRM_WRITE32(chan_ib->ctrl, chan_ib->ctrl_offset + offset, val);
}

static inline uint32_t
pscnv_ib_ctrl_r32(struct pscnv_ib_chan *chan_ib, uint32_t offset)	
{
	return DRM_READ32(chan_ib->ctrl, offset + chan_ib->ctrl_offset);
}

static inline void
FIRE_RING(struct pscnv_ib_chan *ch)
{
	if (ch->pb_pos != ch->pb_put) {
		if (ch->pb_pos > ch->pb_put) {
			pscnv_ib_push(ch, ch->pb_put, ch->pb_pos - ch->pb_put, 0);
		} else {
			pscnv_ib_push(ch, ch->pb_put, PSCNV_PB_SIZE - ch->pb_put, 0);
			if (ch->pb_pos) {
				pscnv_ib_push(ch, 0, ch->pb_pos, 0);
			}
		}
		ch->pb_put = ch->pb_pos;
	}
	// else: nothing to fire
}

static inline void
OUT_RING(struct pscnv_ib_chan *ch, uint32_t word)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	const unsigned long timeout = jiffies + 2*HZ;
		
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
	
	//ch->pb_map[ch->pb_pos/4] = word;
	nv_wv32(ch->pb, ch->pb_pos, word);
	ch->pb_pos += 4;
	ch->pb_pos &= PSCNV_PB_MASK;
	
	dev_priv->vm->bar_flush(dev);
}

static inline void
BEGIN_NVC0(struct pscnv_ib_chan *ch, int subc, int mthd, int len)
{
	OUT_RING(ch, (0x2<<28) | (len<<16) | (subc<<13) | (mthd>>2));
}


#endif /* end of include guard: PSCNV_IB_CHAN_H */
