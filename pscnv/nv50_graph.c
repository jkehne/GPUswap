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
#include "pscnv_engine.h"
#include "pscnv_chan.h"
#include "nv50_vm.h"

struct nv50_graph_engine {
	struct pscnv_engine base;
	spinlock_t lock;
	uint32_t grctx_size;
};

struct nv50_graph_chan {
	struct pscnv_vo *grctx;
};

#define nv50_graph(x) container_of(x, struct nv50_graph_engine, base)

static int nv50_graph_oclasses[] = {
	/* NULL */
	0x0030, 
	/* m2mf */
	0x5039,
	/* NV01-style 2d */
	0x0012,
	0x0019,
	0x0043,
	0x0044,
	0x004a,
	0x0057,
	0x005d,
	0x005f,
	0x0072,
	0x305c,
	0x3064,
	0x3066,
	0x307b,
	0x308a,
	0x5062,
	0x5089,
	/* NV50-style 2d */
	0x502d,
	/* compute */
	0x50c0,
	/* 3d */
	0x5097,
	/* list terminator */
	0
};

static int nv84_graph_oclasses[] = {
	/* NULL */
	0x0030, 
	/* m2mf */
	0x5039,
	/* NV50-style 2d */
	0x502d,
	/* compute */
	0x50c0,
	/* 3d */
	0x5097,
	0x8297,
	/* list terminator */
	0
};

static int nva0_graph_oclasses[] = {
	/* NULL */
	0x0030, 
	/* m2mf */
	0x5039,
	/* NV50-style 2d */
	0x502d,
	/* compute */
	0x50c0,
	/* 3d */
	0x8397,
	/* list terminator */
	0
};

static int nva3_graph_oclasses[] = {
	/* NULL */
	0x0030, 
	/* m2mf */
	0x5039,
	/* NV50-style 2d */
	0x502d,
	/* compute */
	0x50c0,
	0x85c0,
	/* 3d */
	0x8597,
	/* list terminator */
	0
};

static int nvaf_graph_oclasses[] = {
	/* NULL */
	0x0030, 
	/* m2mf */
	0x5039,
	/* NV50-style 2d */
	0x502d,
	/* compute */
	0x50c0,
	0x85c0,
	/* 3d */
	0x8697,
	/* list terminator */
	0
};

void nv50_graph_takedown(struct pscnv_engine *eng);
void nv50_graph_irq_handler(struct pscnv_engine *eng);
int nv50_graph_tlb_flush(struct pscnv_engine *eng, struct pscnv_vspace *vs);
int nv50_graph_chan_alloc(struct pscnv_engine *eng, struct pscnv_chan *ch);
void nv50_graph_chan_free(struct pscnv_engine *eng, struct pscnv_chan *ch);
void nv50_graph_chan_kill(struct pscnv_engine *eng, struct pscnv_chan *ch);
int nv50_graph_chan_obj_new(struct pscnv_engine *eng, struct pscnv_chan *ch, uint32_t handle, uint32_t oclass, uint32_t flags);

struct pscnv_engine *nv50_graph_init(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t units = nv_rd32(dev, 0x1540);
	struct nouveau_grctx ctx = {};
	int ret, i;
	uint32_t *cp;
	struct nv50_graph_engine *res = kzalloc(sizeof *res, GFP_KERNEL);

	if (!res) {
		NV_ERROR(dev, "PGRAPH: Couldn't allocate engine!\n");
		return 0;
	}

	res->base.dev = dev;
	res->base.irq = 12;
	if (dev_priv->chipset == 0x50)
		res->base.oclasses = nv50_graph_oclasses;
	else if (dev_priv->chipset < 0xa0)
		res->base.oclasses = nv84_graph_oclasses;
	else if (dev_priv->chipset == 0xa0 ||
			(dev_priv->chipset >= 0xaa && dev_priv->chipset <= 0xac))
		res->base.oclasses = nva0_graph_oclasses;
	else if (dev_priv->chipset < 0xaa)
		res->base.oclasses = nva3_graph_oclasses;
	else
		res->base.oclasses = nvaf_graph_oclasses;
	res->base.takedown = nv50_graph_takedown;
	res->base.irq_handler = nv50_graph_irq_handler;
	res->base.tlb_flush = nv50_graph_tlb_flush;
	res->base.chan_alloc = nv50_graph_chan_alloc;
	res->base.chan_kill = nv50_graph_chan_kill;
	res->base.chan_free = nv50_graph_chan_free;
	res->base.chan_obj_new = nv50_graph_chan_obj_new;
	spin_lock_init(&res->lock);

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
	/* XXX: figure out what exactly is bit 16. All I know is that it's
	 * needed for QUERYs to work. */
	nv_wr32(dev, 0x400500, 0x00010001);

	/* init ZCULL... or something */
	nv_wr32(dev, 0x402ca8, 0x00000800);

	/* init DEBUG regs */
	/* XXX: look at the other two regs and values everyone uses. pick something. */
	nv_wr32(dev, 0x40008c, 0x00000004);

	/* init and upload ctxprog */
	cp = ctx.data = kmalloc (512 * 4, GFP_KERNEL);
	if (!ctx.data) {
		NV_ERROR (dev, "PGRAPH: Couldn't allocate ctxprog!\n");
		kfree(res);
		return 0;
	}
	ctx.ctxprog_max = 512;
	ctx.dev = dev;
	ctx.mode = NOUVEAU_GRCTX_PROG;
	if ((ret = nv50_grctx_init(&ctx))) {
		kfree(ctx.data);
		kfree(res);
		return 0;
	}
	res->grctx_size = ctx.ctxvals_pos * 4;
	nv_wr32(dev, 0x400324, 0);
	for (i = 0; i < ctx.ctxprog_len; i++)
		nv_wr32(dev, 0x400328, cp[i]);
	kfree(ctx.data);
	
	/* mark no channel loaded */
	/* XXX: is that fully correct? */
	nv_wr32(dev, 0x40032c, 0);
	nv_wr32(dev, 0x400784, 0);
	nv_wr32(dev, 0x400320, 4);

	return &res->base;
}

void nv50_graph_takedown(struct pscnv_engine *eng) {
	nv_wr32(eng->dev, 0x400138, 0);	/* TRAP_EN */
	nv_wr32(eng->dev, 0x40013c, 0);	/* INTR_EN */
	/* XXX */
}

int nv50_graph_chan_alloc(struct pscnv_engine *eng, struct pscnv_chan *ch) {
	struct drm_device *dev = eng->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_graph_engine *graph = nv50_graph(eng);
	struct nouveau_grctx ctx = {};
	uint32_t hdr;
	uint64_t limit;
	int i;
	struct nv50_graph_chan *grch = kzalloc(sizeof *grch, GFP_KERNEL);

	if (!grch) {
		NV_ERROR(dev, "PGRAPH: Couldn't allocate channel!\n");
		return -ENOMEM;
	}

	if (dev_priv->chipset == 0x50)
		hdr = 0x200;
	else
		hdr = 0x20;
	grch->grctx = pscnv_vram_alloc(dev, graph->grctx_size, PSCNV_VO_CONTIG, 0, 0x97c07e47);
	if (!grch->grctx) {
		NV_ERROR(dev, "PGRAPH: No VRAM for context!\n");
		kfree(grch);
		return -ENOMEM;
	}
	for (i = 0; i < graph->grctx_size; i += 4)
		nv_wv32(grch->grctx, i, 0);
	ctx.dev = dev;
	ctx.mode = NOUVEAU_GRCTX_VALS;
	ctx.data = grch->grctx;
	nv50_grctx_init(&ctx);
	limit = grch->grctx->start + graph->grctx_size - 1;
	nv_wv32(ch->vo, hdr + 0x00, 0x00190000);
	nv_wv32(ch->vo, hdr + 0x04, limit);
	nv_wv32(ch->vo, hdr + 0x08, grch->grctx->start);
	nv_wv32(ch->vo, hdr + 0x0c, (limit >> 32) << 24 | (grch->grctx->start >> 32));
	nv_wv32(ch->vo, hdr + 0x10, 0);
	nv_wv32(ch->vo, hdr + 0x14, 0);
	ch->vspace->engref[PSCNV_ENGINE_GRAPH]++;
	ch->engdata[PSCNV_ENGINE_GRAPH] = grch;
	return 0;
}

int nv50_graph_tlb_flush(struct pscnv_engine *eng, struct pscnv_vspace *vs) {
	return nv50_vm_flush(eng->dev, 0);
}

void nv50_graph_chan_kill(struct pscnv_engine *eng, struct pscnv_chan *ch) {
	struct drm_device *dev = eng->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_graph_engine *graph = nv50_graph(eng);
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	uint64_t start;
	unsigned long flags;
	spin_lock_irqsave(&graph->lock, flags);
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
	nv_wr32(dev, 0x400500, 0x10001);
	spin_unlock_irqrestore(&graph->lock, flags);
}

void nv50_graph_chan_free(struct pscnv_engine *eng, struct pscnv_chan *ch) {
	struct nv50_graph_chan *grch = ch->engdata[PSCNV_ENGINE_GRAPH];
	pscnv_vram_free(grch->grctx);
	kfree(grch);
	ch->vspace->engref[PSCNV_ENGINE_GRAPH]--;
	ch->engdata[PSCNV_ENGINE_GRAPH] = 0;
}

int nv50_graph_chan_obj_new(struct pscnv_engine *eng, struct pscnv_chan *ch, uint32_t handle, uint32_t oclass, uint32_t flags) {
	uint32_t inst = pscnv_chan_iobj_new(ch, 0x10);
	if (!inst) {
		return -ENOMEM;
	}
	nv_wv32(ch->vo, inst, oclass);
	nv_wv32(ch->vo, inst + 4, 0);
	nv_wv32(ch->vo, inst + 8, 0);
	nv_wv32(ch->vo, inst + 0xc, 0);
	return pscnv_ramht_insert (&ch->ramht, handle, 0x100000 | inst >> 4);
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

void nv50_graph_irq_handler(struct pscnv_engine *eng) {
	struct drm_device *dev = eng->dev;
	struct nv50_graph_engine *graph = nv50_graph(eng);
	uint32_t status;
	unsigned long flags;
	uint32_t st, chan, addr, data, datah, ecode, class, subc, mthd;
	spin_lock_irqsave(&graph->lock, flags);
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
	nv_wr32(dev, 0x400500, 0x10001);
	pscnv_vm_trap(dev);
	spin_unlock_irqrestore(&graph->lock, flags);
}
