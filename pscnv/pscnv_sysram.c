#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_mem.h"
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mutex.h>

int
pscnv_sysram_alloc(struct pscnv_bo *bo)
{
	return -ENOSYS;
}

int
pscnv_sysram_free(struct pscnv_bo *bo)
{
	return -ENOSYS;
}
