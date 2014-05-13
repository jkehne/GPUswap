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

#define nvc0_fifo(x) container_of(x, struct nvc0_fifo_engine, base)

#endif /* end of include guard: NVC0_FIFO_H */
