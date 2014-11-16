#include <linux/module.h>
#include "gdev_interface.h"
#include "nouveau_drv.h"
#include "pscnv_chan.h"
#include "pscnv_fifo.h"
#include "pscnv_gem.h"
#include "pscnv_ioctl.h"
#include "pscnv_mem.h"
#include "pscnv_vm.h"
#include "pscnv_sysram.h"

#define VS_START 0x20000000
#define VS_END (1ull << 40)

struct drm_device *pscnv_drm = NULL;
void (*gdev_callback_notify)(int subc, uint32_t data) = NULL;

int gdev_drv_vspace_alloc(struct drm_device *drm, uint64_t size, struct gdev_drv_vspace *drv_vspace)
{
	struct pscnv_vspace *vspace;

	if (!(vspace = pscnv_vspace_new(drm, size, 0, 0)))
		return -ENOMEM;

	vspace->filp = NULL; /* we don't need drm_filp in Gdev. */
	drv_vspace->priv = vspace;
	
	return 0;
}
EXPORT_SYMBOL(gdev_drv_vspace_alloc);

int gdev_drv_vspace_free(struct gdev_drv_vspace *drv_vspace)
{
	pscnv_vspace_unref(drv_vspace->priv);
	
	return 0;
}
EXPORT_SYMBOL(gdev_drv_vspace_free);

int gdev_drv_chan_alloc(struct drm_device *drm, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_chan *drv_chan)
{
	struct drm_nouveau_private *dev_priv = drm->dev_private;
	struct pscnv_vspace *vspace = (struct pscnv_vspace *)drv_vspace->priv;
	struct pscnv_chan *chan;
	struct pscnv_bo *ib_bo, *pb_bo, *ctrl_bo;
	uint32_t cid;
	volatile uint32_t *regs;
	uint32_t *ib_map, *pb_map;
	uint32_t ib_order, pb_order;
	uint64_t ib_base, pb_base;
	uint32_t ib_mask, pb_mask;
	uint32_t pb_size;
	int ret;
	
	ctrl_bo = nvc0_fifo_eng(dev_priv->fifo)->ctrl_bo;
	
	if (!ctrl_bo || !ctrl_bo->drm_map) {
		WARN_ON(1);
		return -ENOMEM;
	}
	
	if (!(chan = pscnv_chan_new(drm, vspace, 0))) {
		NV_ERROR(drm, "gdev_drv_chan_alloc: pscnv_chan_new failed\n");
		ret = -ENOMEM;
		goto fail_chan;
	}
	
	chan->filp = NULL; /* we don't need drm_filp in Gdev. */
	
	/* channel ID. */
	cid = chan->cid;

	/* FIFO indirect buffer setup. */
	ib_order = 9; /* it's hardcoded. */
	ib_bo = pscnv_mem_alloc_and_map(vspace, 8 << ib_order,
		PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_VM_KERNEL,
		0x1f18cca0, &ib_base);
	if (!ib_bo) {
		NV_ERROR(drm, "gdev_drv_chan_alloc: failed to allocate IB\n");
		ret = -ENOMEM;
		goto fail_ib;
	}
	ib_map = ib_bo->vmap;
	ib_mask = (1 << ib_order) - 1;

	/* FIFO push buffer setup. */
	pb_order = 20; /* it's hardcoded. */
	pb_size = (1 << pb_order);
	pb_bo = pscnv_mem_alloc_and_map(vspace, pb_size,
		PSCNV_GEM_SYSRAM_SNOOP | PSCNV_GEM_VM_KERNEL,
		0x1f18cca1, &pb_base);
		
	if (!pb_bo) {
		NV_ERROR(drm, "gdev_drv_chan_alloc: failed to allocate PB\n");
		ret = -ENOMEM;
		goto fail_pb;
	}
	pb_map = pb_bo->vmap;
	pb_mask = (1 << pb_order) - 1;

	/* FIFO init. */
	ret = dev_priv->fifo->chan_init_ib(chan, 0, 0, 1, ib_base, ib_order);
	if (ret) {
		NV_ERROR(drm, "gdev_drv_chan_alloc: chan_init_ib failed\n");
		goto fail_fifo_init;
	}

	switch (dev_priv->chipset & 0xf0) {
	case 0xc0:
		/* FIFO command queue registers. */
		regs = ctrl_bo->drm_map->handle + nvc0_fifo_ctrl_offs(drm, cid);
		break;
	default:
		ret = -EINVAL;
		goto fail_fifo_reg;
	}

	drv_chan->priv = chan;
	drv_chan->cid = cid;
	drv_chan->regs = regs;
	drv_chan->ib_bo = ib_bo;
	drv_chan->ib_map = ib_map;
	drv_chan->ib_order = ib_order;
	drv_chan->ib_base = ib_base;
	drv_chan->ib_mask = ib_mask;
	drv_chan->pb_bo = pb_bo;
	drv_chan->pb_map = pb_map;
	drv_chan->pb_order = pb_order;
	drv_chan->pb_base = pb_base;
	drv_chan->pb_mask = pb_mask;
	drv_chan->pb_size = pb_size;
	drv_chan->regs = regs;

	return 0;

fail_fifo_reg:
fail_fifo_init:
	pscnv_vspace_unmap(vspace, pb_base);
	pscnv_mem_free(pb_bo);
fail_pb:
	pscnv_vspace_unmap(vspace, ib_base);
	pscnv_mem_free(ib_bo);
fail_ib:
	pscnv_chan_unref(chan);
fail_chan:
	return ret;
}
EXPORT_SYMBOL(gdev_drv_chan_alloc);

int gdev_drv_chan_free(struct gdev_drv_vspace *drv_vspace, struct gdev_drv_chan *drv_chan)
{
	struct pscnv_vspace *vspace = (struct pscnv_vspace *)drv_vspace->priv;
	struct pscnv_chan *chan = (struct pscnv_chan *)drv_chan->priv;
	struct pscnv_bo *ib_bo = (struct pscnv_bo *)drv_chan->ib_bo;
	struct pscnv_bo *pb_bo = (struct pscnv_bo *)drv_chan->pb_bo;
	uint64_t ib_base = drv_chan->ib_base;
	uint64_t pb_base = drv_chan->pb_base;

	pscnv_vspace_unmap(vspace, pb_base);
	pscnv_mem_free(pb_bo);
	pscnv_vspace_unmap(vspace, ib_base);
	pscnv_mem_free(ib_bo);

	pscnv_chan_unref(chan);

	return 0;
}
EXPORT_SYMBOL(gdev_drv_chan_free);

int gdev_drv_bo_alloc(struct drm_device *drm, uint64_t size, uint32_t drv_flags, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo)
{
	struct pscnv_bo *bo;
	struct pscnv_mm_node *mm;
	struct pscnv_vspace *vspace = (struct pscnv_vspace *)drv_vspace->priv;
	uint32_t flags = 0;
	int ret;

	/* set memory type. */
	if (drv_flags & GDEV_DRV_BO_VRAM)
		flags |= PSCNV_GEM_VRAM_SMALL;
	if (drv_flags & GDEV_DRV_BO_SYSRAM)
		flags |= PSCNV_GEM_SYSRAM_SNOOP;
	if (drv_flags & GDEV_DRV_BO_MAPPABLE) {
		flags |= PSCNV_GEM_MAPPABLE;
		if (drv_flags & GDEV_DRV_BO_SYSRAM) {
			flags |= PSCNV_GEM_VM_KERNEL;
		} else {
			flags |= PSCNV_MAP_USER;
		}
	}

	/* allocate physical memory space. */
	if (!(bo = pscnv_mem_alloc(drm, size, flags, 0, 0, NULL))) {
		ret = -ENOMEM;
		goto fail_bo;
	}

	drv_bo->addr = 0;
	/* allocate virtual address space, if requested. */
	if (drv_flags & GDEV_DRV_BO_VSPACE) {
		if (pscnv_vspace_map(vspace, bo, VS_START, VS_END, 0, &mm))
			goto fail_map;
		drv_bo->addr = mm->start;
	}

	drv_bo->map = NULL;
	drv_bo->size = bo->size;
	drv_bo->priv = bo;
	
	/* address, size, and map. */
	if (drv_flags & GDEV_DRV_BO_MAPPABLE) {
		gdev_drv_bo_map(drm, drv_bo);
	}

	return 0;

fail_map:
	pscnv_mem_free(bo);
fail_bo:
	return ret;

}
EXPORT_SYMBOL(gdev_drv_bo_alloc);

int gdev_drv_bo_free(struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo)
{
	struct pscnv_vspace *vspace = (struct pscnv_vspace *)drv_vspace->priv;
	struct pscnv_bo *bo = (struct pscnv_bo *)drv_bo->priv;
	uint64_t addr = drv_bo->addr;

	if (addr)
		pscnv_vspace_unmap(vspace, addr);

	pscnv_mem_free(bo);

	return 0;
}
EXPORT_SYMBOL(gdev_drv_bo_free);

int gdev_drv_bo_bind(struct drm_device *drm, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo)
{
	struct pscnv_vspace *vspace = (struct pscnv_vspace *)drv_vspace->priv;
	struct pscnv_bo *bo = (struct pscnv_bo *)drv_bo->priv;
	struct pscnv_mm_node *mm;
	
	drv_bo->size = bo->size;
	drv_bo->map = NULL;

	if (pscnv_vspace_map(vspace, bo, VS_START, VS_END, 0,&mm))
		goto fail_map;
	
	drv_bo->addr = mm->start;

	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
		/* do not map VRAM, or we will quickly run out of BAR1- space */
		case PSCNV_GEM_SYSRAM_SNOOP:
		case PSCNV_GEM_SYSRAM_NOSNOOP:
			gdev_drv_bo_map(drm, drv_bo);
	}

	return 0;

fail_map:
	return -ENOMEM;
}
EXPORT_SYMBOL(gdev_drv_bo_bind);

int gdev_drv_bo_unbind(struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo)
{
	struct pscnv_vspace *vspace = (struct pscnv_vspace *)drv_vspace->priv;

	pscnv_vspace_unmap(vspace, drv_bo->addr);	
	
	return 0;
}
EXPORT_SYMBOL(gdev_drv_bo_unbind);

int gdev_drv_bo_map(struct drm_device *drm, struct gdev_drv_bo *drv_bo)
{
	struct pscnv_bo *bo = (struct pscnv_bo *)drv_bo->priv;
	struct drm_nouveau_private *dev_priv = drm->dev_private;

	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			if (!bo->drm_map) {
				if (dev_priv->vm->map_user(bo))
					return -EIO;
			}
			WARN_ON(!bo->drm_map);
			drv_bo->map = bo->drm_map->handle;
			break;
		case PSCNV_GEM_SYSRAM_SNOOP:
		case PSCNV_GEM_SYSRAM_NOSNOOP:
			if (!bo->vmap) {
				pscnv_sysram_vmap(bo);
			}
			WARN_ON(!bo->vmap);
			drv_bo->map = bo->vmap;
			break;
		default:
			WARN_ON(1);
	}
	
	return (drv_bo->map == NULL) ? -EINVAL : 0;
}
EXPORT_SYMBOL(gdev_drv_bo_map);

int gdev_drv_bo_unmap(struct gdev_drv_bo *drv_bo)
{
	/* actual unmapping is delayed until BO is freed */
	drv_bo->map = NULL;

	return 0;
}
EXPORT_SYMBOL(gdev_drv_bo_unmap);

int gdev_drv_read32(struct drm_device *drm, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo, uint64_t offset, uint32_t *p)
{
	struct pscnv_bo *bo = (struct pscnv_bo *)drv_bo->priv;

	*p = nv_rv32(bo, offset);

	return 0;
}
EXPORT_SYMBOL(gdev_drv_read32);

int gdev_drv_write32(struct drm_device *drm, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo, uint64_t offset, uint32_t val)
{
	struct pscnv_bo *bo = (struct pscnv_bo *)drv_bo->priv;

	nv_wv32(bo, offset, val);

	return 0;
}
EXPORT_SYMBOL(gdev_drv_write32);

int gdev_drv_read(struct drm_device *drm, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo, uint64_t offset, uint64_t size, void *buf)
{
	struct pscnv_bo *bo = (struct pscnv_bo *)drv_bo->priv;
	uint32_t pos;
	uint32_t *buf32 = buf;
	
	BUG_ON((size % 4) != 0);
	
	for (pos = 0; pos < size; pos += 4) {
		buf32[pos/4] = nv_rv32(bo, offset + pos);
	}

	return 0;
}
EXPORT_SYMBOL(gdev_drv_read);

int gdev_drv_write(struct drm_device *drm, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo, uint64_t offset, uint64_t size, const void *buf)
{
	struct pscnv_bo *bo = (struct pscnv_bo *)drv_bo->priv;
	uint32_t pos;
	const uint32_t *buf32 = buf;

	BUG_ON((size % 4) != 0);
	
	for (pos = 0; pos < size; pos += 4) {
		nv_wv32(bo, offset + pos, buf32[pos/4]);
	}

	return 0;
}
EXPORT_SYMBOL(gdev_drv_write);

int gdev_drv_getdevice(int *count)
{
	*count = (pscnv_drm == NULL) ? 0 : 1;
	return 0;
}
EXPORT_SYMBOL(gdev_drv_getdevice);

int gdev_drv_getdrm(int minor, struct drm_device **pptr)
{
	*pptr = pscnv_drm;

	return (pscnv_drm == NULL) ? -ENODEV : 0;
}
EXPORT_SYMBOL(gdev_drv_getdrm);

int gdev_drv_getparam(struct drm_device *drm, uint32_t type, uint64_t *res)
{
	struct drm_pscnv_getparam getparam;
	int ret = 0;

	switch (type) {
	case GDEV_DRV_GETPARAM_MP_COUNT:
		getparam.param = PSCNV_GETPARAM_MP_COUNT;
		ret = pscnv_ioctl_getparam(drm, &getparam, NULL);
		*res = getparam.value;
		break;
	case GDEV_DRV_GETPARAM_FB_SIZE:
		getparam.param = PSCNV_GETPARAM_FB_SIZE;
		ret = pscnv_ioctl_getparam(drm, &getparam, NULL);
		*res = getparam.value;
		break;
	case GDEV_DRV_GETPARAM_AGP_SIZE:
		getparam.param = PSCNV_GETPARAM_AGP_SIZE;
		ret = pscnv_ioctl_getparam(drm, &getparam, NULL);
		*res = getparam.value;
		break;
	case GDEV_DRV_GETPARAM_CHIPSET_ID:
		getparam.param = PSCNV_GETPARAM_CHIPSET_ID;
		ret = pscnv_ioctl_getparam(drm, &getparam, NULL);
		*res = getparam.value;
		break;
	case GDEV_DRV_GETPARAM_BUS_TYPE:
		getparam.param = PSCNV_GETPARAM_BUS_TYPE;
		ret = pscnv_ioctl_getparam(drm, &getparam, NULL);
		*res = getparam.value;
		break;
	case GDEV_DRV_GETPARAM_PCI_VENDOR:
		getparam.param = PSCNV_GETPARAM_PCI_VENDOR;
		ret = pscnv_ioctl_getparam(drm, &getparam, NULL);
		*res = getparam.value;
		break;
	case GDEV_DRV_GETPARAM_PCI_DEVICE:
		getparam.param = PSCNV_GETPARAM_PCI_DEVICE;
		ret = pscnv_ioctl_getparam(drm, &getparam, NULL);
		*res = getparam.value;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(gdev_drv_getparam);

int gdev_drv_getaddr(struct drm_device *drm, struct gdev_drv_vspace *drv_vspace, struct gdev_drv_bo *drv_bo, uint64_t offset, uint64_t *addr)
{
	WARN_ON_ONCE(1); /* unsupported */
	*addr = 0x41424344;
	return 0;
}
EXPORT_SYMBOL(gdev_drv_getaddr);

int gdev_drv_setnotify(void (*func)(int subc, uint32_t data))
{
	gdev_callback_notify = func;
	return 0;
}
EXPORT_SYMBOL(gdev_drv_setnotify);

int gdev_drv_unsetnotify(void (*func)(int subc, uint32_t data))
{
	if (gdev_callback_notify != func)
		return -EINVAL;
	gdev_callback_notify = NULL;

	return 0;
}
EXPORT_SYMBOL(gdev_drv_unsetnotify);
