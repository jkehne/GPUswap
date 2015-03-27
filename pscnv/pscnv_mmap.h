#ifndef PSCNV_MMAP_H
#define PSCNV_MMAP_H

#include "nouveau_drv.h"

int
pscnv_mmap(struct file *filp, struct vm_area_struct *vma);

#endif /* end of include guard: PSCNV_MMAP_H */
