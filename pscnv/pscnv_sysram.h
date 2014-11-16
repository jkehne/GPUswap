#ifndef PSCNV_SYSRAM_H
#define PSCNV_SYSRAM_H

#include "pscnv_mem.h"

int
pscnv_sysram_alloc_chunk(struct pscnv_chunk *cnk);

int
pscnv_sysram_alloc(struct pscnv_bo *bo);

void
pscnv_sysram_free_chunk(struct pscnv_chunk *cnk);

uint32_t
nv_rv32_sysram(struct pscnv_chunk *chunk, unsigned offset);

void
nv_wv32_sysram(struct pscnv_chunk *chunk, unsigned offset, uint32_t val);

void
pscnv_sysram_vmap(struct pscnv_bo *bo);

void
pscnv_sysram_vunmap(struct pscnv_bo *bo);

#endif /* end of include guard: PSCNV_SYSRAM_H */
