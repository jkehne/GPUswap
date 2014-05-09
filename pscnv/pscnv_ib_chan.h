#ifndef PSCNV_IB_CHAN_H
#define PSCNV_IB_CHAN_H

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
	struct pscnv_dev *dev;
	struct pscnv_chan *chan;

	/* FIFO indirect buffer setup. */
	struct pscnv_bo *ib;
	volatile uint32_t *ib_map;
	uint32_t ib_put;
	uint32_t ib_get;

	/* FIFO push buffer setup. */
	struct pscnv_bo *pb;
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
pscnv_ib_chan_new(struct pscnv_vspace *vs);

int
pscnv_ib_add_fence(struct pscnv_ib_chan *ib_chan);

void
pscnv_ib_fence_write(struct pscnv_ib_chan *chan, const int subch);

int
pscnv_ib_fence_wait(struct pscnv_ib_chan *chan);

int
pscnv_ib_push(struct pscnv_ib_chan *ch, uint32_t start, uint32_t end, int flags);

int
pscnv_ib_update_get(struct pscnv_ib_chan *ch);

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
    
static inline int
FIRE_RING(struct pscnv_ib_chan *ch)
{
	if (ch->pb_pos != ch->pb_put) {
		if (ch->pb_pos > ch->pb_put) {
			pscnv_ib_push(ch, ch->pb_put, ch->pb_pos - ch->pb_put, 0);
		} else {
			pscnv_ib_push(ch, ch->pb_put, PSCNV_PB_SIZE - ch->pb_put, 0);
			if (ch->pb_pos)
			       	pscnv_ib_push(ch, 0, ch->pb_pos, 0);
		}
		ch->pb_put = ch->pb_pos;
	}
	BUG(); // nothing to fire??
}

static inline int
OUT_RING(struct pscnv_ib_chan *ch, uint32_t word)
{
	while (((ch->pb_pos + 4) & ch->pb_mask) == ch->pb_get) {
		uint32_t old = ch->pb_get;
		FIRE_RING(ch);
		pscnv_ib_update_get(ch);
		if (old == ch->pb_get)
			schedule();
	}
	
	ch->pb_map[ch->pb_pos/4] = word;
	ch->pb_pos += 4;
	ch->pb_pos &= ch->pb_mask;
}

static inline void
BEGIN_NVC0(struct pscnv_ib_chan *ch, int subc, int mthd, int len)
{
	OUT_RING(ch, (0x2<<28) | (len<<16) | (subc<<13) | (mthd>>2));
}


#endif /* end of include guard: PSCNV_IB_CHAN_H */
