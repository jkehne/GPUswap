#include "pscnv_mmap.h"
#include "pscnv_chan.h"
#include "pscnv_ioctl.h"

static int
pscnv_chan_mmap(struct file *filp, struct vm_area_struct *vma);

/*******************************************************************************
 * for ordinary BOs
 ******************************************************************************/

static void
pscnv_gem_vm_open(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct pscnv_bo *bo = obj->driver_private;
	struct drm_device *dev = obj->dev;
	
	if (pscnv_vm_debug >= 1) {
		NV_INFO(dev, "VM: mmap (open) BO %08x/%d\n", bo->cookie, bo->serial);
	}
	
	/* refcounting is done by drm directly on obj */
	drm_gem_vm_open(vma);
}

static void
pscnv_gem_vm_close(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct pscnv_bo *bo = obj->driver_private;
	struct drm_device *dev = obj->dev;
	
	if (pscnv_vm_debug >= 1) {
		NV_INFO(dev, "VM: munmap (close) BO %08x/%d\n", bo->cookie, bo->serial);
	}
	
	bo->vma = NULL;
	
	/* refcounting is done by drm directly on obj */
	drm_gem_vm_close(vma);
}

static int
pscnv_gem_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct pscnv_bo *bo = obj->driver_private;
	uint64_t offset = (uint64_t)vmf->virtual_address - vma->vm_start;
	
	if (!bo || offset >= bo->size || !bo->vm_fault) {
		return VM_FAULT_SIGBUS;
	}
	
	return bo->vm_fault(bo, vma, vmf);
}


static struct vm_operations_struct
pscnv_gem_vm_ops = {
	.open = pscnv_gem_vm_open,
	.close = pscnv_gem_vm_close,
	.fault = pscnv_gem_vm_fault,
};

int
pscnv_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct pscnv_bo *bo;
	int ret;

	if (vma->vm_pgoff * PAGE_SIZE < (1ull << 31))
		return drm_mmap(filp, vma);

	if (vma->vm_pgoff * PAGE_SIZE < (1ull << 32))
		return pscnv_chan_mmap(filp, vma);

	/* this increases the refcount on obj! */
	obj = drm_gem_object_lookup(dev, priv, (vma->vm_pgoff * PAGE_SIZE) >> 32);
	if (!obj)
		return -ENOENT;
	bo = obj->driver_private;
	
	if (pscnv_vm_debug >= 1) {
		NV_INFO(dev, "VM: mmap BO %08x/%d\n", bo->cookie, bo->serial);
	}
	
	if (bo->vma) {
		NV_INFO(dev, "pscnv_mmap: BO %08x/%d already mmap()'d at "
			"area %lx-%lx. Mapping at two locations not supported.",
			bo->cookie, bo->serial, bo->vma->vm_start, bo->vma->vm_end);
		return -EINVAL;
	}
	
	if (vma->vm_end - vma->vm_start > bo->size) {
		NV_ERROR(dev, "vma->vm_end - vma->vm_start > bo->size\n");
		drm_gem_object_unreference_unlocked(obj);
		return -EINVAL;
	}
	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
	case PSCNV_GEM_VRAM_SMALL:
	case PSCNV_GEM_VRAM_LARGE:
		if ((ret = dev_priv->vm->map_user(bo))) {
			drm_gem_object_unreference_unlocked(obj);
			return ret;
		}

		vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
		vma->vm_ops = &pscnv_gem_vm_ops;
		vma->vm_private_data = obj;
		vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

		vma->vm_file = filp;
		bo->vma = vma;

		return remap_pfn_range(vma, vma->vm_start, 
				(dev_priv->fb_phys + bo->map1->start) >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start, PAGE_SHARED);
	case PSCNV_GEM_SYSRAM_SNOOP:
	case PSCNV_GEM_SYSRAM_NOSNOOP:
		/* XXX */
		vma->vm_flags |= VM_RESERVED;
		vma->vm_ops = &pscnv_gem_vm_ops;
		vma->vm_private_data = obj;

		vma->vm_file = filp;
		bo->vma = vma;
		
		/* pte are filled in pscnv_sysram_vm_fault */

		return 0;
	default:
		drm_gem_object_unreference_unlocked(obj);
		return -ENOSYS;
	}
}

/*******************************************************************************
 * for channels
 ******************************************************************************/

static void
pscnv_chan_vm_open(struct vm_area_struct *vma) {
	struct pscnv_chan *ch = vma->vm_private_data;
	pscnv_chan_ref(ch);
}

static void
pscnv_chan_vm_close(struct vm_area_struct *vma) {
	struct pscnv_chan *ch = vma->vm_private_data;
	ch->vma = NULL;
	pscnv_chan_unref(ch);
}

static int
pscnv_chan_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct pscnv_chan *ch = vma->vm_private_data;
	uint64_t offset = (uint64_t)vmf->virtual_address - vma->vm_start;
	
	WARN_ON(!ch);
	
	if (!ch || offset >= 0x1000 || !ch->vm_fault) {
		return VM_FAULT_SIGBUS;
	}
	
	return ch->vm_fault(ch, vma, vmf);
}

static const struct vm_operations_struct
pscnv_chan_vm_ops = {
	.open = pscnv_chan_vm_open,
	.close = pscnv_chan_vm_close,
	.fault = pscnv_chan_vm_fault,
};

static int
pscnv_chan_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cid;
	struct pscnv_chan *ch;
	enum pscnv_chan_state st;
	
	if (vma->vm_end - vma->vm_start > 0x1000)
		return -EINVAL;
	cid = (vma->vm_pgoff * PAGE_SIZE >> 16) & 0x7f;
	ch = pscnv_get_chan(dev, filp->private_data, cid);
	if (!ch)
		return -ENOENT;
	
	st = pscnv_chan_get_state(ch);
	if (ch->state != PSCNV_CHAN_RUNNING && ch->state != PSCNV_CHAN_INITIALIZED) {
		NV_ERROR(dev, "pscnv_chan_mmap: channel %d is in unexpected "
			"state %s\n", ch->cid, pscnv_chan_state_str(ch->state));
		return -EINVAL;
	}
	
	if (ch->vma) {
		NV_INFO(dev, "pscnv_chan_mmap: channel %d already mmap()'d at "
			"area %lx-%lx. Mapping at two locations not supported.",
			ch->cid, ch->vma->vm_start, ch->vma->vm_end);
		return -EINVAL;
	}

	switch (dev_priv->card_type) {
	case NV_50:
		if ((vma->vm_pgoff * PAGE_SIZE & ~0x7f0000ull) == 0xc0000000) {

			vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
			vma->vm_ops = &pscnv_chan_vm_ops;
			vma->vm_private_data = ch;

			vma->vm_file = filp;
			ch->vma = vma;

			return remap_pfn_range(vma, vma->vm_start, 
				(dev_priv->mmio_phys + 0xc00000 + cid * 0x2000) >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start, PAGE_SHARED);
		}
		break;
	case NV_D0:
	case NV_C0:
		if ((vma->vm_pgoff * PAGE_SIZE & ~0x7f0000ull) == 0xc0000000) {

			vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
			vma->vm_ops = &pscnv_chan_vm_ops;
			vma->vm_private_data = ch;
			vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

			vma->vm_file = filp;
			ch->vma = vma;

			return remap_pfn_range(vma, vma->vm_start, 
					(dev_priv->fb_phys + nvc0_fifo_ctrl_offs(dev, ch->cid)) >> PAGE_SHIFT,
					vma->vm_end - vma->vm_start, PAGE_SHARED);
		}
	default:
		return -ENOSYS;
	}
	return -EINVAL;
}