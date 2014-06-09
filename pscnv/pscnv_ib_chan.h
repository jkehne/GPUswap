#ifndef PSCNV_IB_CHAN_H
#define PSCNV_IB_CHAN_H

#include "nouveau_drv.h"
#include "pscnv_chan.h"

#define GDEV_SUBCH_NV_COMPUTE 1
#define GDEV_SUBCH_NV_M2MF    2
#define GDEV_SUBCH_NV_PCOPY0  4
#define GDEV_SUBCH_NV_PCOPY1  8

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
	struct pscnv_bo *ctrl_bo;
	uint32_t ctrl_offset;

	/* FIFO indirect buffer setup. */
	struct pscnv_bo *ib;
	uint64_t ib_vm_base;
	uint32_t ib_put;
	uint32_t ib_get;

	/* FIFO push buffer setup. */
	struct pscnv_bo *pb;
	uint64_t pb_vm_base;
	uint32_t pb_pos;
	uint32_t pb_put;
	uint32_t pb_get;
	
	/* Fence buffer */
	struct pscnv_bo *fence;
	uint64_t fence_addr;
	uint32_t fence_seq;

};

struct pscnv_ib_chan*
pscnv_ib_chan_new(struct pscnv_vspace *vs, int fake);

void
pscnv_ib_chan_free(struct pscnv_ib_chan *ib_chan);

int
pscnv_ib_add_fence(struct pscnv_ib_chan *ib_chan);

/* call once at initialization time, can accept multiple subchannels or'd together*/
void
pscnv_ib_init_subch(struct pscnv_ib_chan *ib_chan, int subch);

void
pscnv_ib_fence_write(struct pscnv_ib_chan *chan, const int subch);

int
pscnv_ib_fence_wait(struct pscnv_ib_chan *chan);

void
pscnv_ib_membar(struct pscnv_ib_chan *chan);

int
pscnv_ib_push(struct pscnv_ib_chan *ch, uint32_t start, uint32_t len, int flags);

void
pscnv_ib_update_pb_get(struct pscnv_ib_chan *ch);

void
pscnv_ib_fail(struct pscnv_ib_chan *ch);

static inline void
pscnv_ib_ctrl_w32(struct pscnv_ib_chan *chan_ib, uint32_t offset, uint32_t val)
{
	nv_wv32(chan_ib->ctrl_bo, chan_ib->ctrl_offset + offset, val);
}

static inline uint32_t
pscnv_ib_ctrl_r32(struct pscnv_ib_chan *chan_ib, uint32_t offset)	
{
	return nv_rv32(chan_ib->ctrl_bo, chan_ib->ctrl_offset + offset);
}

static inline int
pscnv_ib_subch_idx(int subch)
{
	switch (subch)
	{
		case GDEV_SUBCH_NV_COMPUTE: return 1;
		case GDEV_SUBCH_NV_M2MF:    return 2;
		case GDEV_SUBCH_NV_PCOPY0:  return 3;
		case GDEV_SUBCH_NV_PCOPY1:  return 4;
		default: BUG();
	}
}

void
FIRE_RING(struct pscnv_ib_chan *ch);

void
OUT_RING(struct pscnv_ib_chan *ch, uint32_t word);

static inline void
BEGIN_NVC0(struct pscnv_ib_chan *ch, int subch, int mthd, int len)
{
	int subch_idx = pscnv_ib_subch_idx(subch);
	OUT_RING(ch, (0x2<<28) | (len<<16) | (subch_idx<<13) | (mthd>>2));
}

static inline void
BEGIN_NVC0_CONST(struct pscnv_ib_chan *ch, int subch, int mthd, int len)
{
	int subch_idx = pscnv_ib_subch_idx(subch);
	OUT_RING(ch, (0x6<<28) | (len<<16) | (subch_idx<<13) | (mthd>>2));
}

#endif /* end of include guard: PSCNV_IB_CHAN_H */
