#include "drm.h"
#include "nouveau_drv.h"
#include "nvc0_chan.h"
#include "pscnv_chan.h"
#include "nvc0_vm.h"
#include "nvc0_fifo.h"
#include <linux/mm.h>

/*******************************************************************************
 * Channel pause continue
 ******************************************************************************/

static int
nvc0_chan_ctrl_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct nvc0_chan *ch = vma->vm_private_data;
	struct drm_device *dev = ch->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	unsigned long flags;
	uint32_t ib_put;
	
	if (pscnv_pause_debug >= 2) {
		char comm[TASK_COMM_LEN];
		get_task_comm(comm, current);
		
		NV_INFO(dev, "%s (%d) caught in nvc0_chan_ctrl_fault for channel %d\n",
			comm, current->pid, ch->base.cid);
	}
	
	/* we may spin here for a little while, while the other process copies
	 * the page to shadow */
	spin_lock_irqsave(&ch->ctrl_shadow_lock, flags);
	
	if (ch->ctrl_is_shadowed) {
		remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(ch->ctrl_shadow) >> 12,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
		
		spin_unlock_irqrestore(&ch->ctrl_shadow_lock, flags);
	} else {
		/* restore mapping to device memory */
		remap_pfn_range(vma, vma->vm_start,
			(dev_priv->fb_phys + nvc0_fifo_ctrl_offs(dev, ch->base.cid)) >> 12,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
		
		/* again copy ib_put. Just in case that this process wrote to
		 * it, after the continue call */
		ib_put = ch->ctrl_shadow[0x8c/4];
		nv_wv32(fifo->ctrl_bo, (ch->base.cid << 12) + 0x8c, ib_put);
		
		spin_unlock_irqrestore(&ch->ctrl_shadow_lock, flags);
		
		if (pscnv_pause_debug >= 2) {
			NV_INFO(dev, "nvc0_chan_ctrl_fault: writing ib_put=0x%x"
				" into channel %d\n", ib_put, ch->base.cid);
		}
		
		dev_priv->vm->bar_flush(dev);
	}
	
	/* no page means that the kernel shall not care about vmf->page
	* and simply go on, because this handler has already set up things
	* correctly */
	return VM_FAULT_NOPAGE;
}

static int
nvc0_chan_pause_ctrl_shadow(struct nvc0_chan *ch)
{
	struct drm_device *dev = ch->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	struct vm_area_struct *vma = ch->base.vma;
	unsigned long flags;
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
	
	spin_lock_irqsave(&ch->ctrl_shadow_lock, flags);
	
	/* make process pagefault on next access to the fifo regs */
	ret = zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);	
	if (ret) {
		spin_unlock_irqrestore(&ch->ctrl_shadow_lock, flags);
		NV_INFO(dev, "nvc0_chan_pause: zap_vma_ptes failed\n");
		return ret;
	}
	
	for (i = 0; i < 0x1000; i += 4) {
		ch->ctrl_shadow[i/4] = nv_rv32(fifo->ctrl_bo, (ch->base.cid << 12) + i);
	}
	ib_get = ch->ctrl_shadow[0x88/4];
	ib_put = ch->ctrl_shadow[0x8c/4];

	ch->ctrl_is_shadowed = true;
	
	spin_unlock_irqrestore(&ch->ctrl_shadow_lock, flags);
	
	if (pscnv_pause_debug >= 2) {
		NV_INFO(dev, "nvc0_chan_pause: pausing channel %d at ib_get=0x%x"
			", ib_put=0x%x\n", ch->base.cid, ib_get, ib_put);
	}

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
	unsigned long flags;
	int ret;
	
	BUG_ON(!ch->ctrl_is_shadowed);
	
	if (!vma) {
		return 0; /* nothing to do, message already issued */
	}
	
	spin_lock_irqsave(&ch->ctrl_shadow_lock, flags);
	
	/* make process pagefault on next access to the fifo regs */
	ret = zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);	
	if (ret) {
		spin_unlock_irqrestore(&ch->ctrl_shadow_lock, flags);
		NV_INFO(dev, "nvc0_chan_continue: zap_vma_ptes failed\n");
		return ret;
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

	spin_unlock_irqrestore(&ch->ctrl_shadow_lock, flags);
	
	if (pscnv_pause_debug >= 2) {
		NV_INFO(dev, "nvc0_chan_continue: writing ib_put=0x%x into "
			"channel %d\n", ib_put, ch->base.cid);
	}
	
	return 0;
}

static int
nvc0_chan_pause(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	
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
	
	nvc0_chan_pause_ctrl_shadow(nvc0_ch(ch));
	
	pscnv_chan_set_state(ch, PSCNV_CHAN_PAUSED);
	complete(&ch->pause_completion);
	
	return 0;
}

static int
nvc0_chan_continue(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	nvc0_chan_continue_ctrl_shadow(nvc0_ch(ch));
	
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
	nvc0_ch(ch)->ctrl_shadow = (uint32_t*) __get_free_page(GFP_KERNEL);
	if (!nvc0_ch(ch)->ctrl_shadow) {
		NV_ERROR(dev, "nvc0_chan_new: failed to allocate ctrl_shadow"
				" for channel %d\n", ch->cid);
		ret = -ENOMEM;
		goto fail_shadow;
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
		spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
		ch->handle = ch->bo->start >> 12;
		spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
	}
	dev_priv->vm->bar_flush(ch->dev);
	return 0;

fail_shadow:
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

static const struct vm_operations_struct nvc0_chan_ctrl_vm_ops = {
	.open = pscnv_chan_vm_open,
	.close = pscnv_chan_vm_close,
	.fault = nvc0_chan_ctrl_fault,
};

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
	che->base.vm_ops = &nvc0_chan_ctrl_vm_ops;
	dev_priv->chan = &che->base;
	spin_lock_init(&dev_priv->chan->ch_lock);
	dev_priv->chan->ch_min = 1;
	dev_priv->chan->ch_max = 126;
	return 0;
}
