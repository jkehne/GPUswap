#ifndef __NVC0_CHAN_H__
#define __NVC0_CHAN_H__

#include "drm.h"
#include "pscnv_chan.h"

#define nvc0_ch_eng(x) container_of(x, struct nvc0_chan_engine, base)

#define nvc0_ch(x) container_of(x, struct nvc0_chan, base)

struct nvc0_chan_engine {
	struct pscnv_chan_engine base;
};

struct nvc0_chan {
	struct pscnv_chan base;
	
	/* page with shadow copy of the control BO */
	uint32_t *ctrl_shadow;
	spinlock_t ctrl_shadow_lock;
	bool ctrl_is_shadowed;
	
};

#endif /* __NVC0_CHAN_H__ */
