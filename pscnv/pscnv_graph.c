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
#include "nouveau_grctx.h"
#include "pscnv_graph.h"
#include "pscnv_chan.h"

int pscnv_graph_init(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t units = nv_rd32(dev, 0x1540);
	struct nouveau_grctx ctx = {};
	int ret, i;
	uint32_t *cp;

	/* reset everything */
	nv_wr32(dev, 0x200, 0xffffefff);
	nv_wr32(dev, 0x200, 0xffffffff);

	/* reset and enable traps & interrupts */
	nv_wr32(dev, 0x400804, 0xc0000000);	/* DISPATCH */
	nv_wr32(dev, 0x406800, 0xc0000000);	/* M2MF */
	nv_wr32(dev, 0x400c04, 0xc0000000);	/* VFETCH */
	nv_wr32(dev, 0x401800, 0xc0000000);	/* STRMOUT */
	nv_wr32(dev, 0x405018, 0xc0000000);	/* CCACHE */
	nv_wr32(dev, 0x402000, 0xc0000000);	/* CLIPID */
	for (i = 0; i < 16; i++)
		if (units & 1 << i) {
			if (dev_priv->chipset < 0xa0) {
				nv_wr32(dev, 0x408900 + (i << 12), 0xc0000000);	/* TEX */
				nv_wr32(dev, 0x408e08 + (i << 12), 0xc0000000);	/* TPDMA */
				nv_wr32(dev, 0x408314 + (i << 12), 0xc0000000);	/* MPC */
			} else {
				nv_wr32(dev, 0x408600 + (i << 11), 0xc0000000);	/* TEX */
				nv_wr32(dev, 0x408708 + (i << 11), 0xc0000000);	/* TPDMA */
				nv_wr32(dev, 0x40831c + (i << 11), 0xc0000000);	/* MPC */
			}
		}
	nv_wr32(dev, 0x400108, -1);	/* TRAP */
	nv_wr32(dev, 0x400138, -1);	/* TRAP_EN */
	nv_wr32(dev, 0x400100, -1);	/* INTR */
	nv_wr32(dev, 0x40013c, -1);	/* INTR_EN */

	/* set ctxprog flags */
	nv_wr32(dev, 0x400824, 0x00004000);

	/* enable FIFO access */
	/* XXX: everyone uses bit 16 here, we don't. Doesn't seem to
	 * cause any problems. wtf? */
	nv_wr32(dev, 0x400500, 0x00000001);

	/* init ZCULL... or something */
	nv_wr32(dev, 0x402ca8, 0x00000800);

	/* init DEBUG regs */
	/* XXX: look at the other two regs and values everyone uses. pick something. */
	nv_wr32(dev, 0x40008c, 0x00000004);

	/* init and upload ctxprog */
	cp = ctx.data = kmalloc (512 * 4, GFP_KERNEL);
	if (!ctx.data) {
		NV_ERROR (dev, "Couldn't allocate ctxprog\n");
		return -ENOMEM;
	}
	ctx.ctxprog_max = 512;
	ctx.dev = dev;
	ctx.mode = NOUVEAU_GRCTX_PROG;
	if ((ret = nv50_grctx_init(&ctx))) {
		kfree(ctx.data);
		return ret;
	}
	dev_priv->grctx_size = ctx.ctxvals_pos * 4;
	nv_wr32(dev, 0x400324, 0);
	for (i = 0; i < ctx.ctxprog_len; i++)
		nv_wr32(dev, 0x400328, cp[i]);
	kfree(ctx.data);
	
	/* mark no channel loaded */
	/* XXX: is that fully correct? */
	nv_wr32(dev, 0x40032c, 0);
	nv_wr32(dev, 0x400784, 0);
	nv_wr32(dev, 0x400320, 4);

	return 0;
}

int pscnv_graph_takedown(struct drm_device *dev) {
	nv_wr32(dev, 0x400138, 0);	/* TRAP_EN */
	nv_wr32(dev, 0x40013c, 0);	/* INTR_EN */
	/* XXX */
	return 0;
}

int pscnv_ioctl_obj_gr_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_obj_gr_new *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	uint32_t inst;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	switch (req->oclass) {
	/* XXX: what about null object? */
	/* always available */
	case 0x5039:	/* m2mf */
	case 0x502d:	/* eng2d */
	case 0x50c0:	/* compute / turing */
		break;
	/* 5097 on NV50-NV98, 8297 on NV84-NV98 */
	case 0x8297:	/* 3d / tesla */
		if (dev_priv->chipset == 0x50)
			return -EINVAL;
		/* FALLTHRU */
	case 0x5097:	/* 3d / tesla */
		if (dev_priv->chipset >= 0xa0)
			return -EINVAL;
		break;
	/* 8397 on NVA0, NVAA, NVAC */
	case 0x8397:	/* 3d / tesla */
		if (dev_priv->chipset == 0xa0 || dev_priv->chipset >= 0xaa)
			break;
		return -EINVAL;
	/* 8597, 85c0 on NVA3, NVA5, NVA8 */
	case 0x8597:	/* 3d / tesla */
	case 0x85c0:	/* compute / turing */
		if (dev_priv->chipset >= 0xa3 && dev_priv->chipset <= 0xa8)
			break;
		return -EINVAL;
	/* compatibility crap - NV50 only */
	case 0x0012:	/* beta1 */
	case 0x0019:	/* clip */
	case 0x0043:	/* rop */
	case 0x0044:	/* patt */
	case 0x004a:	/* gdirect */
	case 0x0057:	/* chroma */
	case 0x005d:	/* triangle */
	case 0x005f:	/* blit */
	case 0x0072:	/* beta4 */
	case 0x305c:	/* line */
	case 0x3064:	/* iifc */
	case 0x3066:	/* sifc */
	case 0x307b:	/* tfc */
	case 0x308a:	/* ifc */
	case 0x5062:	/* surf2d */
	case 0x5089:	/* sifm */
		if (dev_priv->chipset != 0x50)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock (&dev_priv->vm_mutex);

	ch = pscnv_get_chan(dev, file_priv, req->cid);
	if (!ch) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	if (!(ch->engines & PSCNV_ENGINE_PGRAPH)) {
		struct nouveau_grctx ctx = {};
		uint32_t hdr;
		uint64_t limit;
		int i;
		if (dev_priv->chipset == 0x50)
			hdr = 0x200;
		else
			hdr = 0x20;
		ch->grctx = pscnv_vram_alloc(dev, dev_priv->grctx_size, PSCNV_VO_CONTIG, 0, 0x97c07e47);
		if (!ch->grctx) {
			mutex_unlock (&dev_priv->vm_mutex);
			return -ENOMEM;
		}
		for (i = 0; i < dev_priv->grctx_size; i += 4)
			nv_wv32(ch->grctx, i, 0);
		ctx.dev = dev;
		ctx.mode = NOUVEAU_GRCTX_VALS;
		ctx.data = ch->grctx;
		nv50_grctx_init(&ctx);
		limit = ch->grctx->start + dev_priv->grctx_size - 1;
		nv_wv32(ch->grctx, 0, ch->vo->start >> 12);
		nv_wv32(ch->vo, hdr + 0x00, 0x00190000);
		nv_wv32(ch->vo, hdr + 0x04, limit);
		nv_wv32(ch->vo, hdr + 0x08, ch->grctx->start);
		nv_wv32(ch->vo, hdr + 0x0c, (limit >> 32) << 24 | (ch->grctx->start >> 32));
		nv_wv32(ch->vo, hdr + 0x10, 0);
		nv_wv32(ch->vo, hdr + 0x14, 0);
		ch->engines |= PSCNV_ENGINE_PGRAPH;
		ch->vspace->engines |= PSCNV_ENGINE_PGRAPH;
	}

	inst = pscnv_chan_iobj_new(ch, 0x10);
	if (!inst) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOMEM;
	}
	nv_wv32(ch->vo, inst, req->oclass);
	nv_wv32(ch->vo, inst + 4, 0);
	nv_wv32(ch->vo, inst + 8, 0);
	nv_wv32(ch->vo, inst + 0xc, 0);

	ret = pscnv_ramht_insert (&ch->ramht, req->handle, 0x100000 | inst >> 4);

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}
