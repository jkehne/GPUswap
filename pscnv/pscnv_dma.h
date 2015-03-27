#ifndef PSCNV_DMA_H
#define PSCNV_DMA_H

#define PSCNV_DMA_VSPACE 126
#define PSCNV_DMA_CHAN   126

#include "nouveau_drv.h"

/* flags for DMA transfer */
#define PSCNV_DMA_VERBOSE 0x1
#define PSCNV_DMA_DEBUG   0x2 /* output additional debug messages */
#define PSCNV_DMA_ASYNC   0x4 /* use PCOPY0 instead of M2MF */

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

void
pscnv_dma_exit(struct drm_device *dev);

int
pscnv_dma_bo_to_bo(struct pscnv_bo *tgt, struct pscnv_bo *src, int flags);

int
pscnv_dma_chunk_to_chunk(struct pscnv_chunk *from, struct pscnv_chunk *to, int flags);

#endif /* end of include guard: PSCNV_DMA_H */
