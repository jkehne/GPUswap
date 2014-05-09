#ifndef PSCNV_SWAPPING_H
#define PSCNV_SWAPPING_H

#include "nouveau_drv.h"
#include "pscnv_mem.h"

/*
 * take a buffer in VRAM and copy its contents to a newly allocated SYSRAM
 * buffer. Then redirect the page table entries */
int
pscnv_bo_copy_to_host(struct pscnv_bo *bo);


#endif /* end of include guard: PSCNV_SWAPPING_H */
