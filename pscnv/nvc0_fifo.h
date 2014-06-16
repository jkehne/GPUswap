#ifndef NVC0_FIFO_H
#define NVC0_FIFO_H

#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_fifo.h"

struct nvc0_fifo_engine {
	struct pscnv_fifo_engine base;

	struct pscnv_bo *playlist[2];
	int cur_playlist;
	struct pscnv_bo *ctrl_bo;
	struct drm_local_map *fifo_ctl;
};

/* per channel fifo context */
struct nvc0_fifo_ctx {
	/* indirect buffer for this channel */
	struct pscnv_bo *ib;
	
	/* driver data structure to manipulate this ib */
	struct pscnv_ib_chan *ib_chan;
};

#define nvc0_fifo_eng(x) container_of(x, struct nvc0_fifo_engine, base)

#endif /* end of include guard: NVC0_FIFO_H */
