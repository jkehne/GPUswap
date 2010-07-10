#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nv50_vm.h"
#include "pscnv_vm.h"

int
nv50_vm_flush(struct drm_device *dev, int unit) {
	nv_wr32(dev, 0x100c80, unit << 16 | 1);
	if (!nouveau_wait_until(dev, 2000000000ULL, 0x100c80, 1, 0)) {
		NV_ERROR(dev, "TLB flush fail on unit %d!\n", unit);
		return -EIO;
	}
	return 0;
}
