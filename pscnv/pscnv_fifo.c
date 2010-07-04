/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "drm.h"
#include "drmP.h"
#include "nouveau_drv.h"
#include "pscnv_fifo.h"
#include "pscnv_chan.h"

int pscnv_fifo_init(struct drm_device *dev) {
	int i;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	spin_lock_init(&dev_priv->pfifo_lock);

	dev_priv->playlist[0] = pscnv_vram_alloc(dev, 0x1000, PSCNV_VO_CONTIG, 0, 0x91a71157);
	dev_priv->playlist[1] = pscnv_vram_alloc(dev, 0x1000, PSCNV_VO_CONTIG, 0, 0x91a71157);
	if (!dev_priv->playlist[0] || !dev_priv->playlist[1]) {
		return -ENOMEM;
	}
	pscnv_vspace_map3(dev_priv->playlist[0]);
	pscnv_vspace_map3(dev_priv->playlist[1]);
	dev_priv->cur_playlist = 0;

	/* reset everything */
	nv_wr32(dev, 0x200, 0xfffffeff);
	nv_wr32(dev, 0x200, 0xffffffff);

	/* clear channel table */
	for (i = 0; i < 128; i++)
		nv_wr32(dev, 0x2600 + i * 4, 0);
	
	/* reset and enable interrupts */
	nv_wr32(dev, 0x2100, -1);
	nv_wr32(dev, 0x2140, -1);

	/* XXX: wtf? */
	nv_wr32(dev, 0x250c, 0x6f3cfc34);

	/* put PFIFO onto unused channel 0. */
	nv_wr32(dev, 0x3204, 0);

	/* clear GET, PUT */
	nv_wr32(dev, 0x3210, 0);
	nv_wr32(dev, 0x3270, 0);

	/* enable everything. */
	nv_wr32(dev, 0x3250, 1);
	nv_wr32(dev, 0x3220, 1);
	nv_wr32(dev, 0x3200, 1);
	nv_wr32(dev, 0x2500, 1);

	return 0;
}

int pscnv_fifo_takedown(struct drm_device *dev) {
	int i;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	for (i = 0; i < 128; i++)
		nv_wr32(dev, 0x2600 + i * 4, 0);
	nv_wr32(dev, 0x2140, 0);
	nv_wr32(dev, 0x32ec, 0);
	nv_wr32(dev, 0x2500, 0x101);
	pscnv_vram_free(dev_priv->playlist[0]);
	pscnv_vram_free(dev_priv->playlist[1]);
	/* XXX */
	return 0;
}

void pscnv_fifo_playlist_update (struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i, pos;
	struct pscnv_vo *vo;
	dev_priv->cur_playlist ^= 1;
	vo = dev_priv->playlist[dev_priv->cur_playlist];
	for (i = 0, pos = 0; i < 128; i++) {
		if (nv_rd32(dev, 0x2600 + i * 4) & 0x80000000) {
			nv_wv32(vo, pos, i);
			pos += 4;
		}
	}
	/* XXX: is this correct? is this non-racy? */
	nv_wr32(dev, 0x32f4, vo->start >> 12);
	nv_wr32(dev, 0x32ec, pos / 4);
	nv_wr32(dev, 0x2500, 0x101);
}

void pscnv_fifo_chan_free(struct pscnv_chan *ch) {
	struct drm_device *dev = ch->vspace->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;
	spin_lock_irqsave(&dev_priv->pfifo_lock, flags);
	nv_wr32(dev, 0x2600 + ch->cid * 4, nv_rd32(dev, 0x2600 + ch->cid * 4) & 0x3fffffff);
	pscnv_fifo_playlist_update(dev);
	nv_wr32(dev, 0x2504, 1);
	if (!nouveau_wait_until(dev, 2000000000ULL, 0x2504, 0x10, 0x10)) {
		NV_ERROR(dev, "PFIFO freeze fail!\n");
	}
	if ((nv_rd32(dev, 0x3204) & 0x7f) == ch->cid) {
		NV_INFO(dev, "Kicking channel %d off PFIFO.\n", ch->cid);
		nv_wr32(dev, 0x3204, 0);

		/* put PFIFO onto unused channel 0. */
		nv_wr32(dev, 0x3204, 0);

		/* clear GET, PUT */
		nv_wr32(dev, 0x3210, 0);
		nv_wr32(dev, 0x3270, 0);

		/* enable everything. */
		nv_wr32(dev, 0x3250, 1);
		nv_wr32(dev, 0x3220, 1);
		nv_wr32(dev, 0x3200, 1);
		nv_wr32(dev, 0x2500, 1);

		/* XXX: what if there were some errors on the channel?
		 * is the above enough to clean up any potential mess? */
	}
	nv_wr32(dev, 0x2600 + ch->cid * 4, 0);
	nv_wr32(dev, 0x2504, 0);
	spin_unlock_irqrestore(&dev_priv->pfifo_lock, flags);
}

int pscnv_ioctl_fifo_init(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_fifo_init *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	uint32_t pb_inst;
	unsigned long flags;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	ch = pscnv_get_chan(dev, file_priv, req->cid);
	if (!ch) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	/* XXX: verify that we get a DMA object. */
	pb_inst = pscnv_ramht_find(&ch->ramht, req->pb_handle);
	if (!pb_inst || pb_inst & 0xffff0000) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	spin_lock_irqsave(&dev_priv->pfifo_lock, flags);

	/* init RAMFC. */
	nv_wv32(ch->vo, ch->ramfc + 0x00, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x04, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x08, req->pb_start);
	nv_wv32(ch->vo, ch->ramfc + 0x0c, req->pb_start >> 32);
	nv_wv32(ch->vo, ch->ramfc + 0x10, req->pb_start);
	nv_wv32(ch->vo, ch->ramfc + 0x14, req->pb_start >> 32);
	nv_wv32(ch->vo, ch->ramfc + 0x18, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x1c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x20, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x24, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x28, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x2c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x30, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x34, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x38, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x3c, 0x003f6078);
	nv_wv32(ch->vo, ch->ramfc + 0x40, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x44, 0x2101ffff);
	nv_wv32(ch->vo, ch->ramfc + 0x48, pb_inst);
	nv_wv32(ch->vo, ch->ramfc + 0x4c, 0xffffffff);
	nv_wv32(ch->vo, ch->ramfc + 0x50, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x54, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x58, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x5c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x60, 0x7fffffff);
	nv_wv32(ch->vo, ch->ramfc + 0x64, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x68, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x6c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x70, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x74, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x78, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x7c, 0x30000000 | (req->slimask&0xfff));
	nv_wv32(ch->vo, ch->ramfc + 0x80, 0xc000000 | ch->ramht.offset >> 4);
	nv_wv32(ch->vo, ch->ramfc + 0x84, 0);

	if (dev_priv->chipset != 0x50) {
		nv_wv32(ch->vo, ch->ramfc + 0x88, ch->cache->start >> 10);
		nv_wv32(ch->vo, ch->ramfc + 0x8c, 0);
		nv_wv32(ch->vo, ch->ramfc + 0x90, 0);
		nv_wv32(ch->vo, ch->ramfc + 0x94, 0);
		nv_wv32(ch->vo, ch->ramfc + 0x98, ch->vo->start >> 12);

		nv_wr32(dev, 0x2600 + req->cid * 4, 0x80000000 | (ch->vo->start + ch->ramfc) >> 8);
	} else {
		nv_wr32(dev, 0x2600 + req->cid * 4, 0x80000000 | ch->vo->start >> 12);
	}

	pscnv_fifo_playlist_update(dev);
	spin_unlock_irqrestore(&dev_priv->pfifo_lock, flags);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_fifo_init_ib(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_fifo_init_ib *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	uint32_t pb_inst;
	unsigned long flags;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	ch = pscnv_get_chan(dev, file_priv, req->cid);
	if (!ch) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	/* XXX: verify that we get a DMA object. */
	pb_inst = pscnv_ramht_find(&ch->ramht, req->pb_handle);
	if (!pb_inst || pb_inst & 0xffff0000) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	spin_lock_irqsave(&dev_priv->pfifo_lock, flags);

	/* init RAMFC. */
	nv_wv32(ch->vo, ch->ramfc + 0x00, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x04, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x08, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x0c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x10, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x14, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x18, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x1c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x20, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x24, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x28, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x2c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x30, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x34, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x38, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x3c, 0x403f6078);
	nv_wv32(ch->vo, ch->ramfc + 0x40, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x44, 0x2101ffff);
	nv_wv32(ch->vo, ch->ramfc + 0x48, pb_inst);
	nv_wv32(ch->vo, ch->ramfc + 0x4c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x50, req->ib_start);
	nv_wv32(ch->vo, ch->ramfc + 0x54, req->ib_start >> 32 | req->ib_order << 16);
	nv_wv32(ch->vo, ch->ramfc + 0x58, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x5c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x60, 0x7fffffff);
	nv_wv32(ch->vo, ch->ramfc + 0x64, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x68, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x6c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x70, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x74, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x78, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x7c, 0x30000000 | (req->slimask&0xfff));
	nv_wv32(ch->vo, ch->ramfc + 0x80, 0xc000000 | ch->ramht.offset >> 4);
	nv_wv32(ch->vo, ch->ramfc + 0x84, 0);

	if (dev_priv->chipset != 0x50) {
		nv_wv32(ch->vo, ch->ramfc + 0x88, ch->cache->start >> 10);
		nv_wv32(ch->vo, ch->ramfc + 0x8c, 0);
		nv_wv32(ch->vo, ch->ramfc + 0x90, 0);
		nv_wv32(ch->vo, ch->ramfc + 0x94, 0);
		nv_wv32(ch->vo, ch->ramfc + 0x98, ch->vo->start >> 12);

		nv_wr32(dev, 0x2600 + req->cid * 4, 0x80000000 | (ch->vo->start + ch->ramfc) >> 8);
	} else {
		nv_wr32(dev, 0x2600 + req->cid * 4, 0x80000000 | ch->vo->start >> 12);
	}

	pscnv_fifo_playlist_update(dev);
	spin_unlock_irqrestore(&dev_priv->pfifo_lock, flags);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

void pscnv_fifo_irq_handler(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t status;
	int ch;
	unsigned long flags;
	spin_lock_irqsave(&dev_priv->pfifo_lock, flags);
	status = nv_rd32(dev, 0x2100);
	ch = nv_rd32(dev, 0x3204) & 0x7f;
	if (status & 0x00000001) {
		uint32_t get = nv_rd32(dev, 0x3270);
		uint32_t addr = nv_rd32(dev, 0x90000 + (get & 0x7fc) * 2);
		uint32_t data = nv_rd32(dev, 0x90000 + (get & 0x7fc) * 2 + 4);
		uint32_t pull = nv_rd32(dev, 0x3250);
		const char *reason = 0;
		if (pull & 0x10)
			reason = "NO_HASH";
		if (pull & 0x100)
			reason = "EMPTY_SUBCHAN";
		if (reason)
			NV_ERROR(dev, "PFIFO_CACHE_ERROR [%s]: ch %d subch %d addr %04x data %08x\n", reason, ch, (addr >> 13) & 7, addr & 0x1ffc, data);
		else
			NV_ERROR(dev, "PFIFO_CACHE_ERROR [%08x]: ch %d subch %d addr %04x data %08x\n", pull, ch, (addr >> 13) & 7, addr & 0x1ffc, data);
		get += 4;
		nv_wr32(dev, 0x3270, get);
		nv_wr32(dev, 0x2100, 0x00000001);
		nv_wr32(dev, 0x3250, 1);
		status &= ~0x00000001;
	}
	if (status & 0x00000010) {
		NV_ERROR(dev, "PFIFO BAR fault!\n");
		nv_wr32(dev, 0x2100, 0x00000010);
		status &= ~0x00000010;
	}
	if (status & 0x00000040) {
		NV_ERROR(dev, "PFIFO PEEPHOLE fault!\n");
		nv_wr32(dev, 0x2100, 0x00000040);
		status &= ~0x00000040;
	}
	if (status & 0x00001000) {
		uint32_t get = nv_rd32(dev, 0x3244);
		uint32_t gethi = nv_rd32(dev, 0x3328);
		/* XXX: yup. a race. */
		uint32_t put = nv_rd32(dev, 0x3240);
		uint32_t puthi = nv_rd32(dev, 0x3320);
		uint32_t ib_get = nv_rd32(dev, 0x3334);
		uint32_t ib_put = nv_rd32(dev, 0x3330);
		uint32_t dma_state = nv_rd32(dev, 0x3228);
		uint32_t dma_push = nv_rd32(dev, 0x3220);
		uint32_t st1 = nv_rd32(dev, 0x32a0);
		uint32_t st2 = nv_rd32(dev, 0x32a4);
		uint32_t st3 = nv_rd32(dev, 0x32a8);
		uint32_t st4 = nv_rd32(dev, 0x32ac);
		uint32_t len = nv_rd32(dev, 0x3364);
		NV_ERROR(dev, "PFIFO_DMA_PUSHER: ch %d addr %02x%08x [PUT %02x%08x], IB %08x [PUT %08x] status %08x len %08x push %08x shadow %08x %08x %08x %08x\n",
				ch, gethi, get, puthi, put, ib_get, ib_put, dma_state, len, dma_push, st1, st2, st3, st4);
		if (get != put || gethi != puthi) {
			nv_wr32(dev, 0x3244, put);
			nv_wr32(dev, 0x3328, puthi);
		} else if (ib_get != ib_put) {
			nv_wr32(dev, 0x3334, ib_put);
		} else {
			nv_wr32(dev, 0x3330, 0);
			nv_wr32(dev, 0x3334, 0);
		}
		nv_wr32(dev, 0x3228, 0);
		nv_wr32(dev, 0x3364, 0);
		nv_wr32(dev, 0x3220, 1);
		nv_wr32(dev, 0x2100, 0x00001000);
		status &= ~0x00001000;
	}
	if (status & 0x00100000) {
		uint32_t get = nv_rd32(dev, 0x3270);
		uint32_t addr = nv_rd32(dev, 0x90000 + (get & 0x7fc) * 2);
		uint32_t data = nv_rd32(dev, 0x90000 + (get & 0x7fc) * 2 + 4);
		uint32_t pull = nv_rd32(dev, 0x3250);
		if (dev_priv->chipset > 0x50) {
			/* the SEMAPHORE fuckup special #2 */
			uint32_t sem_lo = nv_rd32(dev, 0x3404);
			if (sem_lo & 3) {
				nv_wr32(dev, 0x3404, 0);
				get -= 4;
				get &= 0xffc;
				addr = 0x14;
				data = sem_lo;
			}
		}
		NV_ERROR(dev, "PFIFO_SEMAPHORE: ch %d subch %d addr %04x data %08x status %08x\n", ch, (addr >> 13) & 7, addr & 0x1ffc, data, pull);
		get += 4;
		nv_wr32(dev, 0x3270, get);
		nv_wr32(dev, 0x3250, 1);
		nv_wr32(dev, 0x2100, 0x00100000);
		status &= ~0x00100000;
	}
	if (status) {
		NV_ERROR(dev, "Unknown PFIFO interrupt %08x\n", status);
		nv_wr32(dev, 0x2100, status);
	}
	pscnv_vm_trap(dev);
	spin_unlock_irqrestore(&dev_priv->pfifo_lock, flags);
}
