#ifndef __NV50_VM_H__
#define __NV50_VM_H__

#include "drmP.h"
#include "drm.h"

int nv50_vm_flush (struct drm_device *dev, int unit);
void nv50_vm_trap(struct drm_device *dev);

#endif /* __NV50_DISPLAY_H__ */
