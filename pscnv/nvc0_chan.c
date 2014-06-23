#include "drm.h"
#include "nouveau_drv.h"
#include "nvc0_chan.h"
#include "pscnv_chan.h"
#include "pscnv_ib_chan.h"
#include "nvc0_vm.h"
#include "nvc0_fifo.h"
#include <linux/mm.h>

/*******************************************************************************
 * Channel pause / continue
 ******************************************************************************/

/* this is run in a workqueue */
static void
nvc0_chan_pause_fence(struct work_struct *ws)
{
	struct nvc0_chan *ch = container_of(ws, struct nvc0_chan, pause_work);
	struct drm_device *dev = ch->base.dev;
	struct nvc0_fifo_ctx *fifo_ctx = ch->base.engdata[PSCNV_ENGINE_FIFO];
	int ret;
	
	struct pscnv_ib_chan *ibch = fifo_ctx->ib_chan;
	
	ret = pscnv_ib_wait_steady(ibch);
	if (ret) {
		goto fail;
	}
	
	/* we save the ib_get here and restore on continue operation */
	ch->old_ib_get = ibch->ib_get;
	
	pscnv_ib_membar(ibch);
	pscnv_ib_fence_write(ibch, GDEV_SUBCH_NV_COMPUTE);
		
	ret = pscnv_ib_fence_wait(ibch);
	if (ret) {
		NV_ERROR(dev, "nvc0_chan_pause_fence: fence_wait returned %d\n",
			ret);
		goto fail;
	}
		
	pscnv_chan_set_state(&ch->base, PSCNV_CHAN_PAUSED);
	complete_all(&ch->base.pause_completion); /* destroys completion */
	
	return;
	
fail:
	pscnv_chan_fail(&ch->base);
	complete_all(&ch->base.pause_completion);
}

static int
nvc0_chan_ctrl_fault(struct pscnv_chan *ch_base, struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct nvc0_chan *ch = nvc0_ch(ch_base);
	struct drm_device *dev = ch->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	uint32_t ib_put;
	
	if (pscnv_pause_debug >= 2) {
		char comm[TASK_COMM_LEN];
		get_task_comm(comm, current);
		
		NV_INFO(dev, "%s (%d) caught in nvc0_chan_ctrl_fault for channel %d\n",
			comm, current->pid, ch->base.cid);
	}
	
	/* we may spin here for a little while, while the other process copies
	 * the page to shadow
	 * Note that we do not use the irqsave version here, als zap_vma_ptes
	 * causes a tlb_flush and tlb_flushing with disabled interrupts may
	 * cause deadlocks on smp systems, due to ipi */
	spin_lock(&ch->ctrl_shadow_lock);
	
	BUG_ON(ch->ctrl_pte_present);
	
	if (ch->ctrl_is_shadowed) {
		remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(ch->ctrl_shadow) >> 12,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
		
		ch->ctrl_pte_present = true;
		spin_unlock(&ch->ctrl_shadow_lock);
	} else {
		if (!ch->ib_pte_present) {
			/* process hit this fault handler before the ib fault
			 * handler. That's bad, as it may want's to update the
			 * ib_put pointer to a value that is not in sync
			 * with the GPU and as soon as we reset the mapping
			 * we loose all control.
			 * consequently we serve the shadow page again. The
			 * ib fault handler will zap the page again as soon
			 * as it runs and then we finally can restore the
			 * correct mapping */
			spin_lock(&ch->ib_shadow_lock);
			remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(ch->ctrl_shadow) >> 12,
				vma->vm_end - vma->vm_start, PAGE_SHARED);
		
			ch->ctrl_pte_present = true;
			ch->ctrl_restore_delayed = true;
			spin_unlock(&ch->ib_shadow_lock);
			spin_unlock(&ch->ctrl_shadow_lock);
			
			if (pscnv_pause_debug >= 2) {
				NV_INFO(dev, "nvc0_chan_ctrl_fault: delaying "
					"restore of channel %d\n", ch->base.cid);
			}
		} else {
			/* restore mapping to device memory */
			remap_pfn_range(vma, vma->vm_start,
				(dev_priv->fb_phys + nvc0_fifo_ctrl_offs(dev, ch->base.cid)) >> 12,
				vma->vm_end - vma->vm_start, PAGE_SHARED);

			ch->ctrl_restore_delayed = false;
			ch->ctrl_pte_present = true;

			/* again copy ib_put. Just in case that this process
			 * wrote to it, after the continue call */
			ib_put = ch->ctrl_shadow[0x8c/4];
			nv_wv32(fifo->ctrl_bo, (ch->base.cid << 12) + 0x8c, ib_put);
		
			spin_unlock(&ch->ctrl_shadow_lock);
		
			if (pscnv_pause_debug >= 2) {
				NV_INFO(dev, "nvc0_chan_ctrl_fault: writing ib_put=0x%x"
				" into channel %d\n", ib_put, ch->base.cid);
			}

			dev_priv->vm->bar_flush(dev);
		}
	}
	
	/* no page means that the kernel shall not care about vmf->page
	* and simply go on, because this handler has already set up things
	* correctly */
	return VM_FAULT_NOPAGE;
}

static struct nvc0_chan*
nvc0_chan_lookup_ib(struct pscnv_bo *ib)
{
	struct drm_device *dev = ib->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct pscnv_chan *ch = NULL;
	struct nvc0_fifo_ctx *fifo_ctx = NULL;
	int chid;
	
	spin_lock(&dev_priv->chan->ch_lock);
	
	
	for (chid = 0; chid < 128; chid++) {
		ch = dev_priv->chan->chans[chid];
		
		if (!ch) {
			continue;
		}
		
		fifo_ctx = ch->engdata[PSCNV_ENGINE_FIFO];
		if (!fifo_ctx) {
			continue;
		}
		
		if (fifo_ctx->ib == ib) {
			spin_unlock(&dev_priv->chan->ch_lock);
			return nvc0_ch(ch);
		}
		
	}
	
	spin_unlock(&dev_priv->chan->ch_lock);

	return NULL;
}

static int
nvc0_chan_ib_fault(struct pscnv_bo *ib, struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_device *dev = ib->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_chan *ch;
	struct vm_area_struct *ctrl_vma;
	struct nvc0_fifo_ctx *fifo_ctx;
	int i, ret;
	
	BUG_ON(!ib);
	
	ch = nvc0_chan_lookup_ib(ib);
	
	if (!ch) {
		NV_ERROR(dev, "nvc0_chan_ib_fault: about to handle IB %08x/%d, "
			"but can not find respective channel\n",
			ib->cookie, ib->serial);
		return VM_FAULT_SIGBUS;
	}
	
	ctrl_vma = ch->base.vma;
	fifo_ctx = ch->base.engdata[PSCNV_ENGINE_FIFO];
	
	BUG_ON(!fifo_ctx);
	
	if (pscnv_pause_debug >= 2) {
		char comm[TASK_COMM_LEN];
		get_task_comm(comm, current);
		
		NV_INFO(dev, "%s (%d) caught in nvc0_chan_ib_fault for channel %d\n",
			comm, current->pid, ch->base.cid);
	}
	
	
	spin_lock(&ch->ib_shadow_lock);
	
	BUG_ON(ch->ib_pte_present);
	
	if (ch->ib_is_shadowed) {
		remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(ch->ib_shadow) >> 12,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
		
		ch->ib_pte_present = true;
		spin_unlock(&ch->ib_shadow_lock);
	} else {
		/* be aware that the ib may be sysram or vram */
		switch (ib->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			remap_pfn_range(vma, vma->vm_start, 
				(dev_priv->fb_phys + ib->map1->start) >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start, PAGE_SHARED);
			break;
		case PSCNV_GEM_SYSRAM_SNOOP:
		case PSCNV_GEM_SYSRAM_NOSNOOP:
			remap_pfn_range(vma, vma->vm_start,
				page_to_pfn(ib->pages[0]),
				vma->vm_end - vma->vm_start, PAGE_SHARED);
			break;
		default:
			spin_unlock(&ch->ib_shadow_lock);
			BUG();
		}

		ch->ib_pte_present = true;
		/* copy back whole ib once again, just in case that it was
		 * modified by the process */
		for (i = 0; i < 0x1000; i += 4) {
			nv_wv32(ib, i, ch->ib_shadow[i/4]);
		}
		
		spin_unlock(&ch->ib_shadow_lock);
		
		dev_priv->vm->bar_flush(dev);
		
		spin_lock(&ch->ctrl_shadow_lock);
		if (ctrl_vma && ch->ctrl_restore_delayed && ch->ctrl_pte_present) {
			if (pscnv_pause_debug >= 2) {
				NV_INFO(dev, "nvc0_chan_ib_fault: again clearing"
					" ctrl vma on chnannel %d\n", ch->base.cid);
			}
			ret = zap_vma_ptes(ctrl_vma, ctrl_vma->vm_start,
					   ctrl_vma->vm_end - ctrl_vma->vm_start);	
			if (ret) {
				spin_unlock(&ch->ctrl_shadow_lock);
				NV_INFO(dev, "nvc0_chan_ib_fault: zap_vma_ptes failed\n");
				return ret;
			}
			ch->ctrl_pte_present = false;
		}
		spin_unlock(&ch->ctrl_shadow_lock);
	}
	
	return VM_FAULT_NOPAGE;
}

static int
nvc0_chan_pause_ctrl_shadow(struct nvc0_chan *ch)
{
	struct drm_device *dev = ch->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	struct vm_area_struct *vma = ch->base.vma;
	uint32_t ib_get, ib_put;
	int i, ret;
	
	BUG_ON(ch->ctrl_is_shadowed);
	
	if (!vma && pscnv_pause_debug >= 1) {
		NV_INFO(dev, "nvc0_chan_pause: strange, channel %d not mapped "
			"into process\n", ch->base.cid);
	}
	if (!vma) {
		return 0; /* nothing to do */
	}
	
	spin_lock(&ch->ctrl_shadow_lock);
	
	if (ch->ctrl_pte_present) {
		/* make process pagefault on next access to the fifo regs */
		ret = zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);	
		if (ret) {
			spin_unlock(&ch->ctrl_shadow_lock);
			NV_INFO(dev, "nvc0_chan_pause: zap_vma_ptes failed\n");
			return ret;
		}
		ch->ctrl_pte_present = false;
	}
	
	for (i = 0; i < 0x1000; i += 4) {
		ch->ctrl_shadow[i/4] = nv_rv32(fifo->ctrl_bo, (ch->base.cid << 12) + i);
	}
	ib_get = ch->ctrl_shadow[0x88/4];
	ib_put = ch->ctrl_shadow[0x8c/4];

	ch->ctrl_is_shadowed = true;
	
	spin_unlock(&ch->ctrl_shadow_lock);
	
	if (pscnv_pause_debug >= 2) {
		NV_INFO(dev, "nvc0_chan_pause: pausing channel %d at ib_get=0x%x"
			", ib_put=0x%x\n", ch->base.cid, ib_get, ib_put);
	}

	return 0;
}

static int
nvc0_chan_pause_ib_shadow(struct nvc0_chan *ch)
{
	struct drm_device *dev = ch->base.dev;
	
	struct nvc0_fifo_ctx *fifo_ctx = ch->base.engdata[PSCNV_ENGINE_FIFO];
	struct pscnv_bo *ib;
	struct vm_area_struct *vma;
	int i, ret;
	
	BUG_ON(ch->ib_is_shadowed);
	BUG_ON(!fifo_ctx);
	
	ib = fifo_ctx->ib;
	BUG_ON(!ib);
	
	vma = ib->vma;
	if (!vma && pscnv_pause_debug >= 1) {
		NV_INFO(dev, "nvc0_chan_pause: strange, IB of channel %d not "
			"mapped into process\n", ch->base.cid);
	}
	if (!vma) {
		return 0; /* nothing to do */
	}
	
	ib->vm_fault = nvc0_chan_ib_fault;
	
	spin_lock(&ch->ib_shadow_lock);
	
	if (ch->ib_pte_present) {
		/* make process pagefault on next access to the IB */
		ret = zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);	
		if (ret) {
			spin_unlock(&ch->ib_shadow_lock);
			NV_INFO(dev, "nvc0_chan_pause: zap_vma_ptes failed\n");
			return ret;
		}
		ch->ib_pte_present = false;
	}
	
	for (i = 0; i < 0x1000; i += 4) {
		ch->ib_shadow[i/4] = nv_rv32(ib, i);
	}

	ch->ib_is_shadowed = true;
	
	spin_unlock(&ch->ib_shadow_lock);

	return 0;
}

static int
nvc0_chan_continue_ctrl_shadow(struct nvc0_chan *ch)
{
	struct drm_device *dev = ch->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	struct vm_area_struct *vma = ch->base.vma;
	uint32_t ib_put;
	int ret;
	
	BUG_ON(!ch->ctrl_is_shadowed);
	
	if (!vma) {
		return 0; /* nothing to do, message already issued */
	}
	
	spin_lock(&ch->ctrl_shadow_lock);
	
	if (ch->ctrl_pte_present) {
		/* zap_vma_ptes results in 'freeing invalid memtype' if pte's
		 * are still zero. So if process has not pagefaulted yet, we
		 * must not call it again */
		ret = zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);	
		if (ret) {
			spin_unlock(&ch->ctrl_shadow_lock);
			NV_INFO(dev, "nvc0_chan_continue: zap_vma_ptes failed\n");
			return ret;
		}
		ch->ctrl_pte_present = false;
	}

	/* we copy the ib_put pointer once here, to get the GPU running again
	 * as quickly as possible (process may not access (=pagefault) the 
	 * ctrl_bo for long time and instead spinlock on fence....)
	 *
	 * Note that we do not copy the whole shadow page back. These are
	 * mmio registers and it is not clear how the gpu reacts on write 
	 * access to such registers. */
	ib_put = ch->ctrl_shadow[0x8c/4];
	nv_wv32(fifo->ctrl_bo, (ch->base.cid << 12) + 0x8c, ib_put);

	ch->ctrl_is_shadowed = false;

	spin_unlock(&ch->ctrl_shadow_lock);
	
	if (pscnv_pause_debug >= 2) {
		NV_INFO(dev, "nvc0_chan_continue: writing ib_put=0x%x into "
			"channel %d\n", ib_put, ch->base.cid);
	}
	
	return 0;
}

static int
nvc0_chan_continue_ib_shadow(struct nvc0_chan *ch)
{
	struct drm_device *dev = ch->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_ctx *fifo_ctx = ch->base.engdata[PSCNV_ENGINE_FIFO];
	struct vm_area_struct *vma = ch->base.vma;
	struct pscnv_bo *ib;
	int i, ret;
	
	BUG_ON(!ch->ib_is_shadowed);
	BUG_ON(!fifo_ctx);
	
	ib = fifo_ctx->ib;
	BUG_ON(!ib);
	
	vma = ib->vma;
	if (!vma) {
		return 0; /* nothing to do, message already issued */
	}
	
	spin_lock(&ch->ib_shadow_lock);
	
	if (ch->ib_pte_present) {
		ret = zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);	
		if (ret) {
			spin_unlock(&ch->ib_shadow_lock);
			NV_INFO(dev, "nvc0_chan_continue: zap_vma_ptes failed\n");
			return ret;
		}
		ch->ib_pte_present = false;
	}

	/* we have to copy the IB back here, as we are about to set the
	 * ib_put pointer in continue_ctrl_shadow. So make sure that all
	 * values are valid here */
	for (i = 0; i < 0x1000; i += 4) {
		nv_wv32(ib, i, ch->ib_shadow[i/4]);
	}
	
	dev_priv->vm->bar_flush(dev);

	ch->ib_is_shadowed = false;

	spin_unlock(&ch->ib_shadow_lock);

	return 0;
}

static int
nvc0_chan_pause(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;
	
	if (ch->flags & PSCNV_CHAN_KERNEL) {
		NV_INFO(dev, "channel %d is a special channel managed by kernel, "
			"won't pause!\n", ch->cid);
		
		return -EINVAL;
	}
	
	if (pscnv_pause_debug >= 1) {
		char comm[TASK_COMM_LEN];
		get_task_comm(comm, current);
		
		NV_INFO(dev, "%s (%d) pausing channel %d\n",
			comm, current->pid, ch->cid);
	}
	
	init_completion(&ch->pause_completion);
	
	ret = nvc0_chan_pause_ctrl_shadow(nvc0_ch(ch));
	if (ret) {
		NV_ERROR(dev, "nvc0_chan_pause: pause_ctrl_shadow failed "
			"for channel %d\n", ch->cid);
		return ret;
	}
	ret = nvc0_chan_pause_ib_shadow(nvc0_ch(ch));
	if (ret) {
		NV_ERROR(dev, "nvc0_chan_pause: pause_ib_shadow failed "
			"for channel %d\n", ch->cid);
		return ret;
	}
	
	/* we do the rest in a workqueue as we may have to wait some time */
	INIT_WORK(&nvc0_ch(ch)->pause_work, nvc0_chan_pause_fence);
	queue_work(dev_priv->wq, &nvc0_ch(ch)->pause_work);
	
	return 0;
}

static int
nvc0_chan_continue(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_ctx *fifo_ctx = ch->engdata[PSCNV_ENGINE_FIFO];
	int ret;
	
	if (!fifo_ctx) {
		goto no_fifo_ctx;
	}
	
	if (pscnv_pause_debug >= 2) {
		NV_INFO(dev, "nvc0_chan_continue: moving ib_get of channel %d "
			"from %x to %x\n",
			ch->cid, fifo_ctx->ib_chan->ib_get,
			nvc0_ch(ch)->old_ib_get);
	}
	
	ret = pscnv_ib_wait_steady(fifo_ctx->ib_chan);
	if (ret) {
		NV_ERROR(dev, "nvc0_chan_continue: wait_steady returned %d", ret);
		return ret;
	}
	
	ret = pscnv_ib_move_ib_get(fifo_ctx->ib_chan, nvc0_ch(ch)->old_ib_get);
	if (ret) {
		NV_ERROR(dev, "nvc0_chan_continue: failed to move ib_get of "
			"channel %d to %x\n",
			ch->cid, nvc0_ch(ch)->old_ib_get);
		return ret;
	}
	
	ret = nvc0_chan_continue_ib_shadow(nvc0_ch(ch));
	if (ret) {
		NV_ERROR(dev, "nvc0_chan_continue: continue_ib_shadow failed for "
			"channel %d\n", ch->cid);
		return ret;
	}

no_fifo_ctx:
	ret = nvc0_chan_continue_ctrl_shadow(nvc0_ch(ch));
	if (ret) {
		NV_ERROR(dev, "nvc0_chan_continue: continue_ctrl_shadow failed "
			"for channel %d\n", ch->cid);
		return ret;
	}
	
	dev_priv->vm->bar_flush(dev);
	
	return 0;
}

/*******************************************************************************
 * Channel construction and destruction
 ******************************************************************************/

static struct pscnv_chan *
nvc0_chan_alloc(struct drm_device *dev)
{
	struct nvc0_chan *res = kzalloc(sizeof *res, GFP_KERNEL);
	if (!res) {
		return NULL;
	}
	return &res->base;
}

static int
nvc0_chan_new(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = ch->dev->dev_private;
	struct pscnv_vspace *vs = ch->vspace;
	
	unsigned long flags;
	int i, ret;

	/* ch->bo holds the configuration of the channel, including the
	 * - page directory
	 * - location and size of the indirect buffer
	 * - location of the engine configurations (that are available on this
	 *   channel)
	 * - location of the so-called fifo-regs.
	 *
	 * It does NOT store the PUT/GET- Pointers, these are stored in
	 * a page of the fifo regs. This page is at:
	 * dev_priv->fifo->ctrl_bo->start + (ch->cid << 12);
	 *
	 * It is this page, not the ch->bo, that can be mmap()'d by the
	 * user. */

	ch->bo = pscnv_mem_alloc(ch->dev, 0x1000, PSCNV_GEM_CONTIG,
			0, (ch->cid < 0 ? 0xc5a2ba7 : 0xc5a2f1f0), NULL);
	if (!ch->bo) {
		NV_ERROR(dev, "nvc0_chan_new: failed to allocate ch->bo for "
				"channel %d\n", ch->cid);
		ret = -ENOMEM;
		goto fail_ch_bo;
	}
	
	spin_lock_init(&nvc0_ch(ch)->ctrl_shadow_lock);
	nvc0_ch(ch)->ctrl_is_shadowed = false;
	nvc0_ch(ch)->ctrl_restore_delayed = false;
	nvc0_ch(ch)->ctrl_pte_present = true;
	nvc0_ch(ch)->ctrl_shadow = (uint32_t*) __get_free_page(GFP_KERNEL);
	if (!nvc0_ch(ch)->ctrl_shadow) {
		NV_ERROR(dev, "nvc0_chan_new: failed to allocate ctrl_shadow"
				" for channel %d\n", ch->cid);
		ret = -ENOMEM;
		goto fail_ctrl_shadow;
	}
	
	spin_lock_init(&nvc0_ch(ch)->ib_shadow_lock);
	nvc0_ch(ch)->ib_is_shadowed = false;
	nvc0_ch(ch)->ib_pte_present = true;
	nvc0_ch(ch)->ib_shadow = (uint32_t*) __get_free_page(GFP_KERNEL);
	if (!nvc0_ch(ch)->ib_shadow) {
		NV_ERROR(dev, "nvc0_chan_new: failed to allocate ib_shadow"
				" for channel %d\n", ch->cid);
		ret = -ENOMEM;
		goto fail_ib_shadow;
	}

	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	ch->handle = ch->bo->start >> 12;
	spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);

	if (vs->vid != -3)
		dev_priv->vm->map_kernel(ch->bo);
	
	for (i = 0; i < 0x1000; i += 4) {
		nv_wv32(ch->bo, i, 0);
	}

	nv_wv32(ch->bo, 0x200, lower_32_bits(nvc0_vs(vs)->pd->start));
	nv_wv32(ch->bo, 0x204, upper_32_bits(nvc0_vs(vs)->pd->start));
	nv_wv32(ch->bo, 0x208, lower_32_bits(vs->size - 1));
	nv_wv32(ch->bo, 0x20c, upper_32_bits(vs->size - 1));

	if (ch->cid >= 0) {
		nv_wr32(ch->dev, 0x3000 + ch->cid * 8, (0x4 << 28) | ch->bo->start >> 12);
		spin_lock(&dev_priv->chan->ch_lock);
		ch->handle = ch->bo->start >> 12;
		spin_unlock(&dev_priv->chan->ch_lock);
	}
	dev_priv->vm->bar_flush(ch->dev);
	
	/* install the nvc0 fault handler */
	ch->vm_fault = nvc0_chan_ctrl_fault;
	
	return 0;

fail_ib_shadow:
	free_page((unsigned long)nvc0_ch(ch)->ctrl_shadow);

fail_ctrl_shadow:
	pscnv_mem_free(ch->bo);

fail_ch_bo:
	return ret;
}

static void nvc0_chan_free(struct pscnv_chan *ch)
{
	struct drm_nouveau_private *dev_priv = ch->dev->dev_private;
	unsigned long flags;
	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	ch->handle = 0;
	spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
	
	free_page((unsigned long)nvc0_ch(ch)->ctrl_shadow);
	free_page((unsigned long)nvc0_ch(ch)->ib_shadow);
	
	 /* should usually free it, nobody else should be using this bo */
	pscnv_bo_unref(ch->bo);
}

static void
nvc0_chan_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_chan_engine *che = nvc0_ch_eng(dev_priv->chan);
	kfree(che);
}

/*******************************************************************************
 * Channel engine
 ******************************************************************************/

static void
nvc0_pd_dump_chan(struct drm_device *dev, struct seq_file *m, int chid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint64_t chan_bo_addr;
	uint64_t pd_addr;
	
	chan_bo_addr = (uint64_t)(nv_rd32(dev, 0x3000 + chid * 8) & 0x3FFFFF) << 12;
	
	if (!chan_bo_addr) {
		NV_INFO(dev, "nvc0_pd_dump_chan: no channel BO for channel %d\n", chid);
		return;
	}
	
	pd_addr = nv_rv32_pramin(dev, chan_bo_addr + 0x200);
	pd_addr |= (uint64_t)(nv_rv32_pramin(dev, chan_bo_addr + 0x204)) << 32;
	
	if (!pd_addr) {
		NV_ERROR(dev, "nvc0_pd_dump_chan: channel BO for channel %d"
			      "exists, but no PD, wtf\n", chid);
		return;
	}
	
	NV_INFO(dev, "DUMP PD at %08llx for channel %d\n", pd_addr, chid);
	
	dev_priv->vm->pd_dump(dev, m, pd_addr, chid); 
}

int
nvc0_chan_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_chan_engine *che = kzalloc(sizeof *che, GFP_KERNEL);
	if (!che) {
		NV_ERROR(dev, "NV_C0_CHAN: Couldn't alloc engine\n");
		return -ENOMEM;
	}
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) & 0xfffffeff);
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) | 0x00000100);
	che->base.takedown = nvc0_chan_takedown;
	che->base.do_chan_alloc = nvc0_chan_alloc;
	che->base.do_chan_new = nvc0_chan_new;
	che->base.do_chan_free = nvc0_chan_free;
	che->base.pd_dump_chan = nvc0_pd_dump_chan;
	che->base.do_chan_pause = nvc0_chan_pause;
	che->base.do_chan_continue = nvc0_chan_continue;
	dev_priv->chan = &che->base;
	spin_lock_init(&dev_priv->chan->ch_lock);
	dev_priv->chan->ch_min = 1;
	dev_priv->chan->ch_max = 126;
	return 0;
}
