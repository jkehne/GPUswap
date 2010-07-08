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

	spin_lock_init(&dev_priv->pgraph_lock);

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

void pscnv_graph_chan_free(struct pscnv_chan *ch) {
	struct drm_device *dev = ch->vspace->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	uint64_t start;
	unsigned long flags;
	spin_lock_irqsave(&dev_priv->pgraph_lock, flags);
	start = ptimer->read(dev);
	/* disable PFIFO access */
	nv_wr32(dev, 0x400500, 0);
	/* tell ctxprog to hang in sync point, if it's executing */
	nv_wr32(dev, 0x400830, 1);
	/* make sure that ctxprog either isn't executing, or is waiting at the
	 * sync point. */
	while ((nv_rd32(dev, 0x400300) & 1) && !(nv_rd32(dev, 0x400824) & 0x80000000)) {
		if (ptimer->read(dev) - start >= 2000000000) {
			NV_ERROR(dev, "ctxprog wait fail!\n");
			break;
		}
	}
	/* check if the channel we're freeing is active on PGRAPH. */
	if (nv_rd32(dev, 0x40032c) == (0x80000000 | ch->vo->start >> 12)) {
		NV_INFO(dev, "Kicking channel %d off PGRAPH.\n", ch->cid);
		/* DIE */
		nv_wr32(dev, 0x400040, -1);
		nv_wr32(dev, 0x400040, 0);
		/* no active channel now. */
		nv_wr32(dev, 0x40032c, 0);
		/* if ctxprog was running, rewind it to the beginning. if it
		 * wasn't, this has no effect. */
		nv_wr32(dev, 0x400310, 0);
	}
	/* or maybe it was just going to be loaded in? */
	if (nv_rd32(dev, 0x400330) == (0x80000000 | ch->vo->start >> 12)) {
		nv_wr32(dev, 0x400330, 0);
		nv_wr32(dev, 0x400310, 0);
	}
	/* back to normal state. */
	nv_wr32(dev, 0x400830, 0);
	nv_wr32(dev, 0x400500, 1);
	spin_unlock_irqrestore(&dev_priv->pgraph_lock, flags);
	pscnv_vram_free(ch->grctx);
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
	case 0x0030:
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

struct pscnv_enumval {
	int value;
	char *name;
	void *data;
};

static struct pscnv_enumval dispatch_errors[] = {
	{ 3, "INVALID_QUERY_OR_TEXTURE", 0 },
	{ 4, "INVALID_VALUE", 0 },
	{ 5, "INVALID_ENUM", 0 },

	{ 8, "INVALID_OBJECT", 0 },

	{ 0xb, "INVALID_ADDRESS_ALIGNMENT", 0 },
	{ 0xc, "INVALID_BITFIELD", 0 },

	{ 0x10, "RT_DOUBLE_BIND", 0 },
	{ 0x11, "RT_TYPES_MISMATCH", 0 },
	{ 0x12, "RT_LINEAR_WITH_ZETA", 0 },

	{ 0x1b, "SAMPLER_OVER_LIMIT", 0 },
	{ 0x1c, "TEXTURE_OVER_LIMIT", 0 },

	{ 0x21, "Z_OUT_OF_BOUNDS", 0 },

	{ 0x23, "M2MF_OUT_OF_BOUNDS", 0 },

	{ 0x27, "CP_MORE_PARAMS_THAN_SHARED", 0 },
	{ 0x28, "CP_NO_REG_SPACE_STRIPED", 0 },
	{ 0x29, "CP_NO_REG_SPACE_PACKED", 0 },
	{ 0x2a, "CP_NOT_ENOUGH_WARPS", 0 },
	{ 0x2b, "CP_BLOCK_SIZE_MISMATCH", 0 },
	{ 0x2c, "CP_NOT_ENOUGH_LOCAL_WARPS", 0 },
	{ 0x2d, "CP_NOT_ENOUGH_STACK_WARPS", 0 },
	{ 0x2e, "CP_NO_BLOCKDIM_LATCH", 0 },

	{ 0x31, "ENG2D_FORMAT_MISMATCH", 0 },

	{ 0x47, "VP_CLIP_OVER_LIMIT", 0 },

	{ 0, 0, 0 },
};

static struct pscnv_enumval *pscnv_enum_find (struct pscnv_enumval *list, int val) {
	while (list->value != val && list->name)
		list++;
	if (list->name)
		return list;
	else
		return 0;
}

void nv50_graph_trap_handler(struct drm_device *dev) {
	uint32_t status = nv_rd32(dev, 0x400108);
	uint32_t ustatus;
	uint32_t chan = nv_rd32(dev, 0x400784);

	if (status & 0x001) {
		ustatus = nv_rd32(dev, 0x400804) & 0x7fffffff;
		if (ustatus & 0x00000001) {
			nv_wr32(dev, 0x400500, 0);
			if (nv_rd32(dev, 0x400808) & 0x80000000) {
				uint32_t class = nv_rd32(dev, 0x400814);
				uint32_t mthd = nv_rd32(dev, 0x400808) & 0x1ffc;
				uint32_t subc = (nv_rd32(dev, 0x400808) >> 16) & 0x7;
				uint32_t data = nv_rd32(dev, 0x40080c);
				NV_ERROR(dev, "PGRAPH_TRAP_DISPATCH: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
				NV_INFO(dev, "PGRAPH_TRAP_DISPATCH: 400808: %08x\n", nv_rd32(dev, 0x400808));
				NV_INFO(dev, "PGRAPH_TRAP_DISPATCH: 400848: %08x\n", nv_rd32(dev, 0x400848));
				nv_wr32(dev, 0x400808, 0);
			} else {
				NV_ERROR(dev, "PGRAPH_TRAP_DISPATCH: No stuck command?\n");
			}
			nv_wr32(dev, 0x4008e8, nv_rd32(dev, 0x4008e8) & 3);
			nv_wr32(dev, 0x400848, 0);
		}
		if (ustatus & 0x00000002) {
			/* XXX: this one involves much more pain. */
			NV_ERROR(dev, "PGRAPH_TRAP_QUERY: ch %x.\n", chan);
		}
		if (ustatus & 0x00000004) {
			NV_ERROR(dev, "PGRAPH_TRAP_GRCTX_MMIO: ch %x. This is a kernel bug.\n", chan);
		}
		if (ustatus & 0x00000008) {
			NV_ERROR(dev, "PGRAPH_TRAP_GRCTX_XFER1: ch %x. This is a kernel bug.\n", chan);
		}
		if (ustatus & 0x00000010) {
			NV_ERROR(dev, "PGRAPH_TRAP_GRCTX_XFER2: ch %x. This is a kernel bug.\n", chan);
		}
		ustatus &= ~0x0000001f;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_DISPATCH: Unknown ustatus 0x%08x on ch %x\n", ustatus, chan);
		nv_wr32(dev, 0x400804, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x001);
		status &= ~0x001;
	}

	if (status & 0x002) {
		ustatus = nv_rd32(dev, 0x406800) & 0x7fffffff;
		if (ustatus & 1)
			NV_ERROR (dev, "PGRAPH_TRAP_M2MF_NOTIFY: ch %x %08x %08x %08x %08x\n",
				chan,
				nv_rd32(dev, 0x406804),
				nv_rd32(dev, 0x406808),
				nv_rd32(dev, 0x40680c),
				nv_rd32(dev, 0x406810));
		if (ustatus & 2)
			NV_ERROR (dev, "PGRAPH_TRAP_M2MF_IN: ch %x %08x %08x %08x %08x\n",
				chan,
				nv_rd32(dev, 0x406804),
				nv_rd32(dev, 0x406808),
				nv_rd32(dev, 0x40680c),
				nv_rd32(dev, 0x406810));
		if (ustatus & 4)
			NV_ERROR (dev, "PGRAPH_TRAP_M2MF_OUT: ch %x %08x %08x %08x %08x\n",
				chan,
				nv_rd32(dev, 0x406804),
				nv_rd32(dev, 0x406808),
				nv_rd32(dev, 0x40680c),
				nv_rd32(dev, 0x406810));
		ustatus &= ~0x00000007;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_M2MF: Unknown ustatus 0x%08x on ch %x\n", ustatus, chan);
		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(dev, 0x400040, 2);
		nv_wr32(dev, 0x400040, 0);
		nv_wr32(dev, 0x406800, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x002);
		status &= ~0x002;
	}

	if (status & 0x004) {
		ustatus = nv_rd32(dev, 0x400c04) & 0x7fffffff;
		if (ustatus & 0x00000001) {
			NV_ERROR (dev, "PGRAPH_TRAP_VFETCH: ch %x\n", chan);
		}
		ustatus &= ~0x00000001;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_VFETCH: Unknown ustatus 0x%08x on ch %x\n", ustatus, chan);
		nv_wr32(dev, 0x400c04, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x004);
		status &= ~0x004;
	}

	if (status & 0x008) {
		ustatus = nv_rd32(dev, 0x401800) & 0x7fffffff;
		if (ustatus & 0x00000001) {
			NV_ERROR (dev, "PGRAPH_TRAP_STRMOUT: ch %x %08x %08x %08x %08x\n", chan,
				nv_rd32(dev, 0x401804),
				nv_rd32(dev, 0x401808),
				nv_rd32(dev, 0x40180c),
				nv_rd32(dev, 0x401810));
		}
		ustatus &= ~0x00000001;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_STRMOUT: Unknown ustatus 0x%08x on ch %x\n", ustatus, chan);
		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(dev, 0x400040, 0x80);
		nv_wr32(dev, 0x400040, 0);
		nv_wr32(dev, 0x401800, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x008);
		status &= ~0x008;
	}

	if (status & 0x010) {
		ustatus = nv_rd32(dev, 0x405018) & 0x7fffffff;
		if (ustatus & 0x00000001) {
			NV_ERROR (dev, "PGRAPH_TRAP_CCACHE: ch %x\n", chan);
		}
		ustatus &= ~0x00000001;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_CCACHE: Unknown ustatus 0x%08x on ch %x\n", ustatus, chan);
		nv_wr32(dev, 0x405018, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x010);
		status &= ~0x010;
	}

	if (status & 0x020) {
		ustatus = nv_rd32(dev, 0x402000) & 0x7fffffff;
		if (ustatus & 0x00000001) {
			NV_ERROR (dev, "PGRAPH_TRAP_CLIPID: ch %x\n", chan);
		}
		ustatus &= ~0x00000001;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_CLIPID: Unknown ustatus 0x%08x on ch %x\n", ustatus, chan);
		nv_wr32(dev, 0x402000, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x020);
		status &= ~0x020;
	}

	/* XXX: per-TP traps. */

	if (status) {
		NV_ERROR(dev, "Unknown PGRAPH trap %08x on ch %x\n", status, chan);
		nv_wr32(dev, 0x400108, status);
	}
}

void pscnv_graph_irq_handler(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t status;
	unsigned long flags;
	uint32_t st, chan, addr, data, datah, ecode, class, subc, mthd;
	spin_lock_irqsave(&dev_priv->pgraph_lock, flags);
	status = nv_rd32(dev, 0x400100);
	ecode = nv_rd32(dev, 0x400110);
	st = nv_rd32(dev, 0x400700);
	addr = nv_rd32(dev, 0x400704);
	mthd = addr & 0x1ffc;
	subc = (addr >> 16) & 7;
	data = nv_rd32(dev, 0x400708);
	datah = nv_rd32(dev, 0x40070c);
	chan = nv_rd32(dev, 0x400784);
	class = nv_rd32(dev, 0x400814) & 0xffff;

	if (status & 0x00000001) {
		NV_ERROR(dev, "PGRAPH_NOTIFY: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000001);
		status &= ~0x00000001;
	}
	if (status & 0x00000002) {
		NV_ERROR(dev, "PGRAPH_QUERY: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000002);
		status &= ~0x00000002;
	}
	if (status & 0x00000010) {
		NV_ERROR(dev, "PGRAPH_ILLEGAL_MTHD: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000010);
		status &= ~0x00000010;
	}
	if (status & 0x00000020) {
		NV_ERROR(dev, "PGRAPH_ILLEGAL_CLASS: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000020);
		status &= ~0x00000020;
	}
	if (status & 0x00000040) {
		NV_ERROR(dev, "PGRAPH_DOUBLE_NOTIFY: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000040);
		status &= ~0x00000040;
	}
	if (status & 0x00010000) {
		NV_ERROR(dev, "PGRAPH_BUFFER_NOTIFY: ch %x\n", chan);
		nv_wr32(dev, 0x400100, 0x00010000);
		status &= ~0x00010000;
	}
	if (status & 0x00100000) {
		struct pscnv_enumval *ev;
		ev = pscnv_enum_find(dispatch_errors, ecode);
		if (ev)
			NV_ERROR(dev, "PGRAPH_DISPATCH_ERROR [%s]: ch %x sub %d [%04x] mthd %04x data %08x\n", ev->name, chan, subc, class, mthd, data);
		else
			NV_ERROR(dev, "PGRAPH_DISPATCH_ERROR [%x]: ch %x sub %d [%04x] mthd %04x data %08x\n", ecode, chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00100000);
		status &= ~0x00100000;
	}

	if (status & 0x00200000) {
		nv50_graph_trap_handler(dev);
		nv_wr32(dev, 0x400100, 0x00200000);
		status &= ~0x00200000;
	}

	if (status & 0x01000000) {
		NV_ERROR(dev, "PGRAPH_SINGLE_STEP: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x01000000);
		status &= ~0x01000000;
	}

	if (status) {
		NV_ERROR(dev, "Unknown PGRAPH interrupt %08x\n", status);
		NV_ERROR(dev, "PGRAPH: ch %x sub %d [%04x] mthd %04x data %08x\n", chan, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, status);
	}
	nv_wr32(dev, 0x400500, 1);
	pscnv_vm_trap(dev);
	spin_unlock_irqrestore(&dev_priv->pgraph_lock, flags);
}
