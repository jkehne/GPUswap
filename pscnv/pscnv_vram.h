#ifndef PSCNV_VRAM_H
#define PSCNV_VRAM_H

#include "pscnv_mem.h"

/* must be called INSIDE vram_mutex */
int
pscnv_vram_alloc_chunk(struct pscnv_chunk *cnk, int flags);

/* must be called INSIDE vram_mutex */
void
pscnv_vram_free_chunk(struct pscnv_chunk *cnk);

/* must be called OUTSIDE vram_mutex */
int
pscnv_vram_alloc(struct pscnv_bo *bo);

void
pscnv_vram_takedown(struct drm_device *dev);

#endif /* end of include guard: PSCNV_VRAM_H */
