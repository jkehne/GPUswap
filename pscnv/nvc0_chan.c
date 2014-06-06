#include "drm.h"
#include "nouveau_drv.h"
#include "nvc0_chan.h"
#include "pscnv_chan.h"
#include "nvc0_vm.h"

static int nvc0_chan_new (struct pscnv_chan *ch)
{
	struct pscnv_vspace *vs = ch->vspace;
	struct drm_nouveau_private *dev_priv = ch->dev->dev_private;
	unsigned long flags;
	int i;
	
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
	if (!ch->bo)
		return -ENOMEM;

	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	ch->handle = ch->bo->start >> 12;
	spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);

	if (vs->vid != -3)
		dev_priv->vm->map_kernel(ch->bo);
	
	for (i = 0; i < 0x1000; i += 4) {
		nv_wv32(ch->bo, i, 0);
	}

	nv_wv32(ch->bo, 0x200, nvc0_vs(vs)->pd->start);
	nv_wv32(ch->bo, 0x204, nvc0_vs(vs)->pd->start >> 32);
	nv_wv32(ch->bo, 0x208, vs->size - 1);
	nv_wv32(ch->bo, 0x20c, (vs->size - 1) >> 32);

	if (ch->cid >= 0) {
		nv_wr32(ch->dev, 0x3000 + ch->cid * 8, (0x4 << 28) | ch->bo->start >> 12);
		spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
		ch->handle = ch->bo->start >> 12;
		spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
	}
	dev_priv->vm->bar_flush(ch->dev);
	return 0;
}

static void nvc0_chan_free(struct pscnv_chan *ch)
{
	struct drm_nouveau_private *dev_priv = ch->dev->dev_private;
	unsigned long flags;
	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	ch->handle = 0;
	spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
	 /* should usually free it, nobody else should be using this bo */
	pscnv_bo_unref(ch->bo);
}

static void
nvc0_chan_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_chan_engine *che = nvc0_ch(dev_priv->chan);
	kfree(che);
}

static void
nvc0_pd_dump_chan(struct drm_device *dev, int chid)
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
	
	dev_priv->vm->pd_dump(dev, pd_addr, chid); 
}

int
nvc0_chan_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_chan_engine *che = kzalloc(sizeof *che, GFP_KERNEL);
	if (!che) {
		NV_ERROR(dev, "CH: Couldn't alloc engine\n");
		return -ENOMEM;
	}
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) & 0xfffffeff);
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) | 0x00000100);
	che->base.takedown = nvc0_chan_takedown;
	che->base.do_chan_new = nvc0_chan_new;
	che->base.do_chan_free = nvc0_chan_free;
	che->base.pd_dump_chan = nvc0_pd_dump_chan;
	dev_priv->chan = &che->base;
	spin_lock_init(&dev_priv->chan->ch_lock);
	dev_priv->chan->ch_min = 1;
	dev_priv->chan->ch_max = 126;
	return 0;
}
