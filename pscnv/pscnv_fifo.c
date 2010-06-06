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

int pscnv_fifo_init(struct drm_device *dev) {
	int i;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	spin_lock_init(&dev_priv->pfifo_lock);

	dev_priv->playlist[0] = pscnv_vram_alloc(dev, 0x1000, PSCNV_VO_CONTIG, 0, 0x91a71157);
	dev_priv->playlist[1] = pscnv_vram_alloc(dev, 0x1000, PSCNV_VO_CONTIG, 0, 0x91a71157);
	if (!dev_priv->playlist[0] || !dev_priv->playlist[1]) {
		return -ENOMEM;
	}
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
	nv_wr32(dev, 0x2504, 0x6f3cfc34);

	/* put PFIFO at a nonexistent channel. */
	nv_wr32(dev, 0x3204, 0x7f);

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
	for (i = 0; i < 128; i++)
		nv_wr32(dev, 0x2600 + i * 4, 0);
	nv_wr32(dev, 0x2140, 0);
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
		if (nv_rd32(dev, 0x2600 + i * 4)) {
			nv_wv32(vo, pos, i);
			pos += 4;
		}
	}
	/* XXX: is this correct? is this non-racy? */
	nv_wr32(dev, 0x32f4, vo->start >> 12);
	nv_wr32(dev, 0x32ec, pos / 4);
	nv_wr32(dev, 0x2500, 0x101);
}

int pscnv_ioctl_fifo_init(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_fifo_init *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	uint32_t pb_inst;

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

	spin_lock(&dev_priv->pfifo_lock);

	/* init RAMFC. */
	nv_wv32(ch->vo, ch->ramfc + 0x00, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x04, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x08, req->pb_start);
	nv_wv32(ch->vo, ch->ramfc + 0x0c, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x10, req->pb_start);
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
	nv_wv32(ch->vo, ch->ramfc + 0x3c, 0x003f6078);
	nv_wv32(ch->vo, ch->ramfc + 0x40, 0);
	nv_wv32(ch->vo, ch->ramfc + 0x44, 0x2101ffff);
	nv_wv32(ch->vo, ch->ramfc + 0x48, pb_inst);
	nv_wv32(ch->vo, ch->ramfc + 0x4c, 0);
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
		/* XXX: what are these two for? */
		nv_wv32(ch->vo, 0, req->cid);
		nv_wv32(ch->vo, 4, (ch->vo->start + ch->ramfc) >> 8);

		nv_wr32(dev, 0x2600 + req->cid * 4, 0x80000000 | (ch->vo->start + ch->ramfc) >> 8);
	} else {
		nv_wr32(dev, 0x2600 + req->cid * 4, 0x80000000 | ch->vo->start >> 12);
	}

	pscnv_fifo_playlist_update(dev);
	spin_unlock(&dev_priv->pfifo_lock);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}
