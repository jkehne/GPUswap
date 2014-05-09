#ifndef PSCNV_DMA_H
#define PSCNV_DMA_H

#include "nouveau_drv.h"

struct pscnv_dma {
    struct pscnv_vspace *vs;
	struct pscnv_ib_chan *ib_chan;
};

/*
 * copy the VRAM to the SYSRAM */




int 
pscnv_dma_init(struct drm_device *dev);

#endif /* end of include guard: PSCNV_DMA_H */
