#ifndef __NV50_VM_H__
#define __NV50_VM_H__

#include "drmP.h"
#include "drm.h"

int nv50_vm_flush (struct drm_device *dev, int unit);
int nv50_vspace_do_map (struct pscnv_vspace *vs, struct pscnv_vo *vo, uint64_t offset);
int nv50_vspace_do_unmap (struct pscnv_vspace *vs, uint64_t offset, uint64_t length);
void nv50_vspace_free(struct pscnv_vspace *vs);
void nv50_vm_trap(struct drm_device *dev);

#endif /* __NV50_VM_H__ */
