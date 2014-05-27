#ifndef PSCNV_DMA_H
#define PSCNV_DMA_H

#define PSCNV_DMA_VSPACE 126
#define PSCNV_DMA_CHAN   126

#include "nouveau_drv.h"

struct pscnv_dma {
	struct drm_device *dev;
	struct pscnv_vspace *vs;
	struct pscnv_ib_chan *ib_chan;
	struct mutex lock;
};

/*
 * copy the VRAM to the SYSRAM */




int 
pscnv_dma_init(struct drm_device *dev);

int
pscnv_dma_bo_to_bo(struct pscnv_bo *tgt, struct pscnv_bo *src);

#endif /* end of include guard: PSCNV_DMA_H */
