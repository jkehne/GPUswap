/*
 * Copyright (C) 2010 Christoph Bumiller.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "pscnv_engine.h"
#include "pscnv_chan.h"
#include "nvc0_vm.h"
#include "nvc0_graph.h"

#include "nvc0_pgraph.xml.h"

struct nvc0_graph_chan {
	struct pscnv_bo *grctx;
	struct pscnv_mm_node *grctx_vm;
};

#define nvc0_graph(x) container_of(x, struct nvc0_graph_engine, base)
#define GPC_REG(i, r) (NVC0_PGRAPH_GPC(i) + (r))
#define TP_REG(i, j, r) (NVC0_PGRAPH_GPC_TP(i, j) + (r))
#define TP_BROADCAST(n) NVC0_PGRAPH_GPC_BROADCAST_TP_BROADCAST_##n
#define ROPC_REG(i, r) (NVC0_PGRAPH_ROPC(i) + (r))
#define __TRAP_CLEAR_AND_ENABLE \
	(NVC0_PGRAPH_DISPATCH_TRAP_CLEAR | NVC0_PGRAPH_DISPATCH_TRAP_ENABLE)

void nvc0_graph_takedown(struct pscnv_engine *eng);
int nvc0_graph_chan_alloc(struct pscnv_engine *eng, struct pscnv_chan *ch);
void nvc0_graph_chan_free(struct pscnv_engine *eng, struct pscnv_chan *ch);
void nvc0_graph_chan_kill(struct pscnv_engine *eng, struct pscnv_chan *ch);
void nvc0_graph_irq_handler(struct drm_device *dev, int irq);
void nvc0_ctxctl_load_fuc(struct drm_device *dev);

static inline void
nvc0_graph_init_reset(struct drm_device *dev)
{
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) & 0xffffefff);
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) | 0x00001000);
}

static void
nvc0_graph_init_intr(struct drm_device *dev)
{
	nv_wr32(dev, NVC0_PGRAPH_TRAP, 0xffffffff);
	nv_wr32(dev, NVC0_PGRAPH_TRAP_EN, 0xffffffff);

	nv_wr32(dev, NVC0_PGRAPH_TRAP_GPCS, 0xffffffff);
	nv_wr32(dev, NVC0_PGRAPH_TRAP_GPCS_EN, 0xffffffff);
	nv_wr32(dev, NVC0_PGRAPH_TRAP_ROPCS, 0xffffffff);
	nv_wr32(dev, NVC0_PGRAPH_TRAP_ROPCS_EN, 0xffffffff);

	nv_wr32(dev, 0x400054, 0x34ce3464);
}

static void
nvc0_graph_init_units(struct drm_device *dev)
{
	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_INTR_UP_ENABLE, 0xf0000);

	nv_wr32(dev, NVC0_PGRAPH_DISPATCH_TRAP, 0xc0000000);
	nv_wr32(dev, NVC0_PGRAPH_M2MF_TRAP, 0xc0000000);
	nv_wr32(dev, NVC0_PGRAPH_CCACHE_TRAP, 0xc0000000);
	nv_wr32(dev, NVC0_PGRAPH_UNK6000_TRAP_UNK1, 0xc0000000);
	nv_wr32(dev, NVC0_PGRAPH_MACRO_TRAP, 0xc0000000);
	nv_wr32(dev, NVC0_PGRAPH_UNK6000_TRAP_UNK0, 0xc0000000);
	nv_wr32(dev, NVC0_PGRAPH_UNK5800_TRAP, 0xc0000000);

	nv_wr32(dev, NVC0_PGRAPH_UNK5800_TRAP_UNK44, 0x00ffffff);

	nv_mask(dev, TP_BROADCAST(L1) + 0xc0, 0, 8);
	nv_mask(dev, TP_BROADCAST(MP) + 0xb4, 0, 0x1000);
}

static void
nvc0_graph_gpc_init(struct drm_device *dev, struct nvc0_graph_engine *graph)
{
	int i, j;

#if 0
	nv_wr32(dev, 0x418980, 0x11110000);
	nv_wr32(dev, 0x418984, 0x00233222); /* GTX 470 */
	nv_wr32(dev, 0x418984, 0x03332222); /* GTX 480 */
	nv_wr32(dev, 0x418988, 0);
	nv_wr32(dev, 0x41898c, 0);
#endif

	for (i = 0; i < graph->gpc_count; ++i) {
		/* the number of TPs per GPC. */
		graph->gpc_tp_count[i] =
			nv_rd32(dev, GPC_REG(i, 0x2608)) & 0xffff;
		/* the number of total TPs. */
		graph->tp_count += graph->gpc_tp_count[i];
	}

	for (i = 0; i < graph->gpc_count; ++i) {
		nv_wr32(dev, GPC_REG(i, 0x0914),
			(graph->ropc_count << 8) | graph->gpc_tp_count[i]);
		nv_wr32(dev, GPC_REG(i, 0x0910),
			(graph->gpc_count << 16) | graph->tp_count);
		nv_wr32(dev, GPC_REG(i, 0x0918), 0x92493); /* 88889 */
	}
	nv_wr32(dev, 0x419bd4, 0x92493);
	nv_wr32(dev, 0x4188ac, graph->ropc_count);

	for (i = 0; i < graph->gpc_count; ++i) {
		int mp_count = nv_rd32(dev, GPC_REG(i, 0x2608)) & 0xffff;

		NV_INFO(dev, "init TP%i (%i MPs)\n", i, mp_count);
	
		nv_wr32(dev, GPC_REG(i, 0x0420), 0xc0000000);
		nv_wr32(dev, GPC_REG(i, 0x0900), 0xc0000000);
		nv_wr32(dev, GPC_REG(i, 0x1028), 0xc0000000);
		nv_wr32(dev, GPC_REG(i, 0x0824), 0xc0000000);

		for (j = 0; j < mp_count; ++j) {
			nv_wr32(dev, TP_REG(i, j, 0x508), 0xffffffff);
			nv_wr32(dev, TP_REG(i, j, 0x50c), 0xffffffff);
			nv_wr32(dev, TP_REG(i, j, 0x224), 0xc0000000);
			nv_wr32(dev, TP_REG(i, j, 0x48c), 0xc0000000);
			nv_wr32(dev, TP_REG(i, j, 0x084), 0xc0000000);
			nv_wr32(dev, TP_REG(i, j, 0x644), 0x1ffffe);
			nv_wr32(dev, TP_REG(i, j, 0x64c), 0xf);
		}

		nv_wr32(dev, GPC_REG(i, 0x2c90), 0xffffffff); /* CTXCTL */
		nv_wr32(dev, GPC_REG(i, 0x2c94), 0xffffffff); /* CTXCTL */
        }
}

static void
nvc0_graph_ropc_init(struct drm_device *dev, struct nvc0_graph_engine *graph)
{
	int i;

	for (i = 0; i < graph->ropc_count; ++i) {
		nv_wr32(dev, ROPC_REG(i, 0x144), 0xc0000000);
		nv_wr32(dev, ROPC_REG(i, 0x070), 0xc0000000);
		nv_wr32(dev, NVC0_PGRAPH_ROPC_TRAP(i), 0xffffffff);
		nv_wr32(dev, NVC0_PGRAPH_ROPC_TRAP_EN(i), 0xffffffff);
	}
}

static void 
nvc0_graph_init_regs(struct drm_device *dev)
{
	NV_INFO(dev, "%s\n", __FUNCTION__);

	nv_wr32(dev, 0x400080, 0x003083c2);
	nv_wr32(dev, 0x400088, 0x00006fe7);
	nv_wr32(dev, 0x40008c, 0x00000000);
	nv_wr32(dev, 0x400090, 0x00000030);
        
	nv_wr32(dev, NVC0_PGRAPH_INTR_EN, 0x013901f7);
	nv_wr32(dev, NVC0_PGRAPH_INTR_DISPATCH_CTXCTL_DOWN, 0x00000100);
	nv_wr32(dev, NVC0_PGRAPH_INTR_CTXCTL_DOWN, 0x00000000);
	nv_wr32(dev, NVC0_PGRAPH_INTR_EN_CTXCTL_DOWN, 0x00000110);
	nv_wr32(dev, NVC0_PGRAPH_TRAP_EN, 0x00000000);
	nv_wr32(dev, NVC0_PGRAPH_TRAP_GPCS_EN, 0x00000000);
	nv_wr32(dev, NVC0_PGRAPH_TRAP_ROPCS_EN, 0x00000000);
	nv_wr32(dev, 0x400124, 0x00000002);

	nv_wr32(dev, 0x4188ac, 0x00000005);
}

static int 
nvc0_graph_init_ctxctl(struct drm_device *dev, struct nvc0_graph_engine *graph)
{
	uint32_t wait[3];
	int i, j, cx_num;

	NV_DEBUG(dev, "%s\n", __FUNCTION__);

	nvc0_ctxctl_load_fuc(dev);

	nv_wr32(dev, 0x409840, 0xffffffff);
	nv_wr32(dev, 0x41a10c, 0);
	nv_wr32(dev, 0x40910c, 0);
	nv_wr32(dev, 0x41a100, 2);
	nv_wr32(dev, 0x409100, 2);

	if (!nv_wait(dev, 0x409800, 0x1, 0x1))
		NV_ERROR(dev, "PGRAPH: 0x9800 stalled\n");

	nv_wr32(dev, 0x409840, 0xffffffff);
	nv_wr32(dev, 0x409500, 0x7fffffff);
	nv_wr32(dev, 0x409504, 0x21);

	wait[0] = 0x10; /* grctx size request */
	wait[1] = 0x16;
	wait[2] = 0x25;

	for (i = 0; i < 3; ++i) {
		nv_wr32(dev, 0x409840, 0xffffffff);
		nv_wr32(dev, 0x409500, 0);
		nv_wr32(dev, 0x409504, wait[i]);

		if (!nv_wait_neq(dev, 0x409800, ~0, 0x0))
			NV_WARN(dev, "PGRAPH: 0x9800 stalled (%i)\n", i);

		switch (wait[i]) {
		case 0x10:
			/* we may need one more round-up */
			graph->grctx_size =
				(nv_rd32(dev, 0x409800) + 0xffff) & ~0xffff;
			break;
		default:
			break;
		}
	}

	/* read stuff I don't know what it is */

	cx_num = nv_rd32(dev, 0x409880);

	for (i = 0; i < cx_num; ++i) {
		nv_wr32(dev, 0x409ffc, i);
		nv_rd32(dev, 0x409910);
	}

	cx_num = nv_rd32(dev, GPC_REG(0, 0x2880));

	for (i = 0; i < graph->gpc_count; ++i) {
		for (j = 0; j < cx_num; ++j) {
			nv_wr32(dev, NVC0_PGRAPH_GPC_CTXCTL_HOST_IO_INDEX(i),
				j);
			nv_rd32(dev, NVC0_PGRAPH_GPC_CTXCTL_STRAND_SIZE(i));
		}
	}

	return 0;
}

static int
nvc0_graph_load_ctx(struct drm_device *dev, struct pscnv_bo *vo)
{
	uint32_t inst = vo->start >> 12;

	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_RED_SWITCH, 0x070);
	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_RED_SWITCH, 0x770);
	nv_wr32(dev, 0x40802c, 1); /* ??? */
	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_CC_SCRATCH_CLEAR(0), 0x30);

	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_WRCMD_DATA, (0x8 << 28) | inst);
	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_WRCMD_CMD, 0x3);

	return 0;
}

int
nvc0_graph_store_ctx(struct drm_device *dev)
{
	uint32_t inst = nv_rd32(dev, 0x409b00) & 0xfffffff;

	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_CC_SCRATCH_CLEAR(0), 0x3);
	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_WRCMD_DATA, (0x8 << 28) | inst);
	nv_wr32(dev, NVC0_PGRAPH_CTXCTL_WRCMD_CMD, 0x9);

	if (!nv_wait(dev, NVC0_PGRAPH_CTXCTL_CC_SCRATCH(0), ~0, 0x1)) {
		NV_ERROR(dev, "PGRAPH: failed to store context\n");
		return -EBUSY;
	}
	NV_INFO(dev, "PGRAPH: context stored: 0x%08x\n",
			nv_rd32(dev, NVC0_PGRAPH_CTXCTL_CC_SCRATCH(0)));

	return 0;
}

static int 
nvc0_grctx_generate(struct drm_device *dev, struct nvc0_graph_engine *graph, 
					struct pscnv_chan *chan)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_chan *grch = chan->engdata[PSCNV_ENGINE_GRAPH];
	int i, ret;
	uint32_t *grctx;

	if (graph->grctx_initvals)
		return 0;
	NV_INFO(dev, "PGRAPH: generating default grctx\n");

	grctx = kmalloc(graph->grctx_size, GFP_KERNEL);
	if (!grctx)
		return -ENOMEM;

	nvc0_graph_load_ctx(dev, chan->bo);

	nv_wv32(grch->grctx, 0x1c, 1);
	nv_wv32(grch->grctx, 0x20, 0);
	dev_priv->vm->bar_flush(dev);
	nv_wv32(grch->grctx, 0x28, 0);
	nv_wv32(grch->grctx, 0x2c, 0);
	dev_priv->vm->bar_flush(dev);

	nvc0_grctx_construct(dev, graph, chan);

	ret = nvc0_graph_store_ctx(dev);
	if (ret)
		return ret;

	for (i = 0; i < graph->grctx_size / 4; ++i)
		grctx[i] = nv_rv32(grch->grctx, i * 4);

	for (i = 0; i < 0x100 / 4; ++i)
		NV_DEBUG(dev, "grctx[%i] = 0x%08x\n", i, grctx[i]);

	graph->grctx_initvals = grctx;

	nv_wr32(dev, 0x104048, nv_rd32(dev, 0x104048) | 3);
	nv_wr32(dev, 0x105048, nv_rd32(dev, 0x105048) | 3);

	nv_wv32(grch->grctx, 0xf4, 0);
	nv_wv32(grch->grctx, 0xf8, 0);
	nv_wv32(grch->grctx, 0x10, 0); /* mmio list size */
	nv_wv32(grch->grctx, 0x14, 0); /* mmio list */
	nv_wv32(grch->grctx, 0x18, 0);
	nv_wv32(grch->grctx, 0x1c, 1);
	nv_wv32(grch->grctx, 0x20, 0);
	nv_wv32(grch->grctx, 0x28, 0);
	nv_wv32(grch->grctx, 0x2c, 0);
	dev_priv->vm->bar_flush(dev);

	return 0;
}

void
nvc0_graph_takedown(struct pscnv_engine *eng)
{
	nv_wr32(eng->dev, NVC0_PGRAPH_TRAP_EN, 0);
	nv_wr32(eng->dev, NVC0_PGRAPH_INTR_EN, 0);
}

int
nvc0_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_bo *vo;
	int i, ret;
	struct nvc0_graph_engine *res = kzalloc(sizeof *res, GFP_KERNEL);

	if (!res) {
		NV_ERROR(dev, "PGRAPH: Couldn't allocate engine!\n");
		return -ENOMEM;
	}
	NV_INFO(dev, "PGRAPH: Initializing...\n");

	dev_priv->engines[PSCNV_ENGINE_GRAPH] = &res->base;
	res->base.dev = dev;
	res->base.takedown = nvc0_graph_takedown;
	res->base.chan_alloc = nvc0_graph_chan_alloc;
	res->base.chan_kill = nvc0_graph_chan_kill;
	res->base.chan_free = nvc0_graph_chan_free;
	spin_lock_init(&res->lock);

	vo = pscnv_mem_alloc(dev, 0x1000, PSCNV_GEM_CONTIG, 0, 
						 NVC0_PGRAPH_GPC_BROADCAST_FFB_UNK34_ADDR);
	if (!vo)
		return -ENOMEM;
	ret = dev_priv->vm->map_kernel(vo);
	if (ret)
		return ret;
	res->obj188b4 = vo; /* PGRAPH_GPC_BROADCAST_FFB_UNK32_ADDR */

	vo = pscnv_mem_alloc(dev, 0x1000, PSCNV_GEM_CONTIG, 0,
						 NVC0_PGRAPH_GPC_BROADCAST_FFB_UNK38_ADDR);
	if (!vo)
		return -ENOMEM;
	ret = dev_priv->vm->map_kernel(vo);
	if (ret)
		return ret;
	res->obj188b8 = vo; /* PGRAPH_GPC_BROADCAST_FFB_UNK38_ADDR */

	for (i = 0; i < 0x1000; i += 4) {
		nv_wv32(res->obj188b4, i, 0x10);
		nv_wv32(res->obj188b8, i, 0x10);
	}
	dev_priv->vm->bar_flush(dev);

	vo = pscnv_mem_alloc(dev, 0x2000, PSCNV_GEM_CONTIG | PSCNV_GEM_NOUSER, 0,
						 NVC0_PGRAPH_CCACHE_HUB2GPC_ADDR);
	if (!vo)
		return -ENOMEM;
	ret = dev_priv->vm->map_kernel(vo);
	if (ret)
		return ret;
	res->obj08004 = vo; /* PGRAPH_CCACHE_HUB2GPC_ADDR */

	vo = pscnv_mem_alloc(dev, 0x8000, PSCNV_GEM_CONTIG | PSCNV_GEM_NOUSER, 0,
						 NVC0_PGRAPH_CCACHE_HUB2ESETUP_ADDR);
	if (!vo)
		return -ENOMEM;
	ret = dev_priv->vm->map_kernel(vo);
	if (ret)
		return ret;
	res->obj0800c = vo; /* PGRAPH_CCACHE_HUB2ESETUP_ADDR */

	vo = pscnv_mem_alloc(dev, 3 << 17, PSCNV_GEM_CONTIG, 0, 
						 TP_BROADCAST(POLY_POLY2ESETUP));
	if (!vo)
		return -ENOMEM;
	ret = dev_priv->vm->map_kernel(vo);
	if (ret)
		return ret;
	res->obj19848 = vo;

	nv_wr32(dev, NVC0_PGRAPH_FIFO_CONTROL, 
			nv_rd32(dev, NVC0_PGRAPH_FIFO_CONTROL) & ~0x00010001);

	nvc0_graph_init_reset(dev);

	res->gpc_count = nv_rd32(dev, NVC0_PGRAPH_CTXCTL_UNITS) & 0x1f;
	res->ropc_count = nv_rd32(dev, NVC0_PGRAPH_CTXCTL_UNITS) >> 16;

	nv_wr32(dev, NVC0_PGRAPH_GPC_BROADCAST_FFB, 0x00000000);
	nv_wr32(dev, 0x4188a4, 0x00000000); /* ??? */
	for (i = 0; i < 4; ++i)
		nv_wr32(dev, 0x418888 + i * 4, 0x00000000); /* ??? */

	nv_wr32(dev, NVC0_PGRAPH_GPC_BROADCAST_FFB_UNK34_ADDR, 
			res->obj188b4->start >> 8);
	nv_wr32(dev, NVC0_PGRAPH_GPC_BROADCAST_FFB_UNK38_ADDR,
			res->obj188b4->start >> 8);

	nvc0_graph_init_regs(dev);

	nv_wr32(dev, NVC0_PGRAPH_FIFO_CONTROL, 
			NVC0_PGRAPH_FIFO_CONTROL_UNK16 | NVC0_PGRAPH_FIFO_CONTROL_PULL);

	nv_wr32(dev, NVC0_PGRAPH_INTR, 0xffffffff);
	nv_wr32(dev, NVC0_PGRAPH_INTR_EN, 0xffffffff);

	nvc0_graph_init_units(dev);
	nvc0_graph_gpc_init(dev, res);
	nvc0_graph_ropc_init(dev, res);

	nvc0_graph_init_intr(dev);

	ret = nvc0_graph_init_ctxctl(dev, res);
	if (ret)
		return ret;

	nouveau_irq_register(dev, 12, nvc0_graph_irq_handler);

	return 0;
}

/* list of PGRAPH writes put in grctx+0x14, count of writes grctx+0x10 */
static int
nvc0_graph_create_context_mmio_list(struct pscnv_vspace *vs)
{
	struct drm_device *dev = vs->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_bo *vo;
	int i, ret;

	vo = pscnv_mem_alloc(vs->dev, 0x1000, PSCNV_GEM_CONTIG, 0, 0x33101157);
	if (!vo)
		return -ENOMEM;
	nvc0_vs(vs)->mmio_bo = vo;

	ret = dev_priv->vm->map_kernel(nvc0_vs(vs)->mmio_bo);
	if (ret)
		return ret;

	ret = pscnv_vspace_map(vs, vo, 0x1000, (1ULL << 40) - 1, 0,
			       &nvc0_vs(vs)->mmio_vm);
	if (ret)
		return ret;

	i = -4;
	nv_wv32(vo, i+=4, NVC0_PGRAPH_CCACHE_HUB2GPC_ADDR);
	nv_wv32(vo, i+=4, nvc0_vs(vs)->obj08004->start >> 8);

	nv_wv32(vo, i+=4, NVC0_PGRAPH_CCACHE_HUB2GPC_CONF);
	nv_wv32(vo, i+=4, 0x80000018);

	nv_wv32(vo, i+=4, NVC0_PGRAPH_CCACHE_HUB2ESETUP_ADDR);
	nv_wv32(vo, i+=4, nvc0_vs(vs)->obj0800c->start >> 8);

	nv_wv32(vo, i+=4, NVC0_PGRAPH_CCACHE_HUB2ESETUP_CONF);
	nv_wv32(vo, i+=4, 0x80000000);

	nv_wv32(vo, i+=4, NVC0_PGRAPH_GPC_BROADCAST_ESETUP_POLY2ESETUP);
	nv_wv32(vo, i+=4, (8 << 28) | (nvc0_vs(vs)->obj19848->start >> 12));

	nv_wv32(vo, i+=4, TP_BROADCAST(POLY_POLY2ESETUP));
	nv_wv32(vo, i+=4, (1 << 28) | (nvc0_vs(vs)->obj19848->start >> 12));

	nv_wv32(vo, i+=4, NVC0_PGRAPH_GPC_BROADCAST_CCACHE_HUB2GPC_ADDR);
	nv_wv32(vo, i+=4, nvc0_vs(vs)->obj0800c->start >> 8);

	nv_wv32(vo, i+=4, NVC0_PGRAPH_GPC_BROADCAST_CCACHE_HUB2GPC_CONF);
	nv_wv32(vo, i+=4, 0);

	nv_wv32(vo, i+=4, NVC0_PGRAPH_GPC_BROADCAST_ESETUP_HUB2ESETUP_ADDR);
	nv_wv32(vo, i += 4, nvc0_vs(vs)->obj08004->start >> 8);

	nv_wv32(vo, i += 4, NVC0_PGRAPH_GPC_BROADCAST_ESETUP_HUB2ESETUP_CONF);
	nv_wv32(vo, i += 4, 0x80000018);

	return 0;
}

int
nvc0_graph_chan_alloc(struct pscnv_engine *eng, struct pscnv_chan *chan)
{
	struct drm_device *dev = eng->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_engine *graph = nvc0_graph(eng);
	int i, ret;
	struct nvc0_graph_chan *grch = kzalloc(sizeof *grch, GFP_KERNEL);

	if (!grch) {
		NV_ERROR(dev, "PGRAPH: Couldn't allocate channel !\n");
		return -ENOMEM;
	}

	graph->grctx_size = 0x60000; /* XXX */

	grch->grctx = pscnv_mem_alloc(dev, graph->grctx_size,
				      PSCNV_GEM_CONTIG | PSCNV_GEM_NOUSER,
				      0, 0x93ac0747);
	if (!grch->grctx)
		return -ENOMEM;

	ret = dev_priv->vm->map_kernel(grch->grctx);
	if (ret) {
		pscnv_mem_free(grch->grctx);
		return ret;
	}

	ret = pscnv_vspace_map(chan->vspace,
			       grch->grctx, 0x1000, (1ULL << 40) - 1,
			       0, &grch->grctx_vm);
	if (ret) {
		pscnv_mem_free(grch->grctx);
		return ret;
	}

	nv_wv32(chan->bo, 0x210, grch->grctx_vm->start | 4);
	nv_wv32(chan->bo, 0x214, grch->grctx_vm->start >> 32);
	dev_priv->vm->bar_flush(dev);

	if (!nvc0_vs(chan->vspace)->obj08004) {
		ret = pscnv_vspace_map(chan->vspace, graph->obj08004,
				       0x1000, (1ULL << 40) - 1, 0,
				       &nvc0_vs(chan->vspace)->obj08004);
		if (ret)
			return ret;

		ret = pscnv_vspace_map(chan->vspace, graph->obj0800c,
				       0x1000, (1ULL << 40) - 1, 0,
				       &nvc0_vs(chan->vspace)->obj0800c);
		if (ret)
			return ret;

		ret = pscnv_vspace_map(chan->vspace, graph->obj19848,
				       0x1000, (1ULL << 40) - 1, 0,
				       &nvc0_vs(chan->vspace)->obj19848);
		if (ret)
			return ret;
	}

	chan->engdata[PSCNV_ENGINE_GRAPH] = grch;

	if (!graph->grctx_initvals)
		return nvc0_grctx_generate(dev, graph, chan);

	/* fill in context values generated for 1st context */
	for (i = 0; i < graph->grctx_size / 4; ++i)
		nv_wv32(grch->grctx, i * 4, graph->grctx_initvals[i]);

	if (!nvc0_vs(chan->vspace)->mmio_bo) {
		ret = nvc0_graph_create_context_mmio_list(chan->vspace);
		if (ret)
			return ret;
	}

	nv_wv32(grch->grctx, 0xf4, 0);
	nv_wv32(grch->grctx, 0xf8, 0);
	nv_wv32(grch->grctx, 0x10, 10); /* mmio list size */
	nv_wv32(grch->grctx, 0x14, nvc0_vs(chan->vspace)->mmio_vm->start);
	nv_wv32(grch->grctx, 0x18, nvc0_vs(chan->vspace)->mmio_vm->start >> 32);
	nv_wv32(grch->grctx, 0x1c, 1);
	nv_wv32(grch->grctx, 0x20, 0);
	nv_wv32(grch->grctx, 0x28, 0);
	nv_wv32(grch->grctx, 0x2c, 0);
	dev_priv->vm->bar_flush(dev);

	return 0;
}

void
nvc0_graph_chan_kill(struct pscnv_engine *eng, struct pscnv_chan *ch)
{
	/* FIXME */
}

void
nvc0_graph_chan_free(struct pscnv_engine *eng, struct pscnv_chan *ch)
{
	struct nvc0_graph_chan *grch = ch->engdata[PSCNV_ENGINE_GRAPH];

	pscnv_vspace_unmap_node(grch->grctx_vm);
	pscnv_mem_free(grch->grctx);
	kfree(grch);
	ch->engdata[PSCNV_ENGINE_GRAPH] = NULL;
}

/* IRQ Handler */

struct pscnv_enum {
	int value;
	const char *name;
	void *data;
};

static const struct pscnv_enum dispatch_errors[] = {
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

	{ 0, NULL, 0 },
};

static const struct pscnv_enum *
pscnv_enum_find(const struct pscnv_enum *list, int val)
{
	for (; list->value != val && list->name; ++list);
	return list->name ? list : NULL;
}

static void
nvc0_graph_trap_handler(struct drm_device *dev, int cid)
{
	uint32_t status = nv_rd32(dev, NVC0_PGRAPH_TRAP);
	uint32_t ustatus;

	if (status & NVC0_PGRAPH_TRAP_DISPATCH) {
		ustatus = nv_rd32(dev, NVC0_PGRAPH_DISPATCH_TRAP) & 0x7fffffff;
		if (ustatus & 0x00000001) {
			NV_ERROR(dev, "PGRAPH_TRAP_DISPATCH: ch %d\n", cid);
		}
		if (ustatus & 0x00000002) {
			NV_ERROR(dev, "PGRAPH_TRAP_QUERY: ch %d\n", cid);
		}
		ustatus &= ~0x00000003;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_DISPATCH: unknown ustatus "
				 "%08x on ch %d\n", ustatus, cid);

		nv_wr32(dev, NVC0_PGRAPH_DISPATCH_TRAP, __TRAP_CLEAR_AND_ENABLE);
		nv_wr32(dev, NVC0_PGRAPH_TRAP, NVC0_PGRAPH_TRAP_DISPATCH);
		status &= ~NVC0_PGRAPH_TRAP_DISPATCH;
	}

	if (status & NVC0_PGRAPH_TRAP_M2MF) {
		ustatus = nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP) & 0x7fffffff;
		if (ustatus & 1)
			NV_ERROR(dev, "PGRAPH_TRAP_M2MF_NOTIFY: ch %d "
				 "%08x %08x %08x %08x\n", cid,
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x04),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x08),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x0c),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x10));
		if (ustatus & 2)
			NV_ERROR(dev, "PGRAPH_TRAP_M2MF_IN: ch %d "
				 "%08x %08x %08x %08x\n", cid,
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x04),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x08),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x0c),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x10));
		if (ustatus & 4)
			NV_ERROR(dev, "PGRAPH_TRAP_M2MF_OUT: ch %d "
				 "%08x %08x %08x %08x\n", cid,
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x04),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x08),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x0c),
				 nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP + 0x10));
		ustatus &= ~0x00000007;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_M2MF: unknown ustatus %08x "
				 "on ch %d\n", cid, ustatus);
		nv_wr32(dev, NVC0_PGRAPH_M2MF_TRAP, __TRAP_CLEAR_AND_ENABLE);
		nv_wr32(dev, NVC0_PGRAPH_TRAP, NVC0_PGRAPH_TRAP_M2MF);
		status &= ~NVC0_PGRAPH_TRAP_M2MF;
	}

	if (status & NVC0_PGRAPH_TRAP_UNK4) {
		ustatus = nv_rd32(dev, NVC0_PGRAPH_UNK5800_TRAP);
		if (ustatus & (1 << 24))
			NV_ERROR(dev, "PGRAPH_TRAP_SHADERS: VPA fail\n");
		if (ustatus & (1 << 25))
			NV_ERROR(dev, "PGRAPH_TRAP_SHADERS: VPB fail\n");
		if (ustatus & (1 << 26))
			NV_ERROR(dev, "PGRAPH_TRAP_SHADERS: TCP fail\n");
		if (ustatus & (1 << 27))
			NV_ERROR(dev, "PGRAPH_TRAP_SHADERS: TEP fail\n");
		if (ustatus & (1 << 28))
			NV_ERROR(dev, "PGRAPH_TRAP_SHADERS: GP fail\n");
		if (ustatus & (1 << 29))
			NV_ERROR(dev, "PGRAPH_TRAP_SHADERS: FP fail\n");
		NV_ERROR(dev, "PGRAPH_TRAP_SHDERS: ustatus = %08x\n", ustatus);
		nv_wr32(dev, NVC0_PGRAPH_UNK5800_TRAP, __TRAP_CLEAR_AND_ENABLE);
		nv_wr32(dev, NVC0_PGRAPH_TRAP, NVC0_PGRAPH_TRAP_UNK4);
		status &= ~NVC0_PGRAPH_TRAP_UNK4;
	}

	if (status & NVC0_PGRAPH_TRAP_MACRO) {
		ustatus = nv_rd32(dev, NVC0_PGRAPH_MACRO_TRAP) & 0x7fffffff;
		if (ustatus & NVC0_PGRAPH_MACRO_TRAP_TOO_FEW_PARAMS)
			NV_ERROR(dev, "PGRAPH_TRAP_MACRO: TOO_FEW_PARAMS %08x\n",
				 nv_rd32(dev, 0x404424));
		if (ustatus & NVC0_PGRAPH_MACRO_TRAP_TOO_MANY_PARAMS)
			NV_ERROR(dev, "PGRAPH_TRAP_MACRO: TOO_MANY_PARAMS %08x\n",
				 nv_rd32(dev, 0x404424));
		if (ustatus & NVC0_PGRAPH_MACRO_TRAP_ILLEGAL_OPCODE)
			NV_ERROR(dev, "PGRAPH_TRAP_MACRO: ILLEGAL_OPCODE %08x\n",
				 nv_rd32(dev, 0x404424));
		if (ustatus & NVC0_PGRAPH_MACRO_TRAP_DOUBLE_BRANCH)
			NV_ERROR(dev, "PGRAPH_TRAP_MACRO: DOUBLE_BRANCH %08x\n",
				 nv_rd32(dev, 0x404424));
		ustatus &= ~0xf;
		if (ustatus)
			NV_ERROR(dev, "PGRAPH_TRAP_MACRO: unknown ustatus %08x\n", ustatus);
		nv_wr32(dev, NVC0_PGRAPH_MACRO_TRAP, __TRAP_CLEAR_AND_ENABLE);
		nv_wr32(dev, NVC0_PGRAPH_TRAP, NVC0_PGRAPH_TRAP_MACRO);
		status &= ~NVC0_PGRAPH_TRAP_MACRO;
	}

	if (status) {
		NV_ERROR(dev, "PGRAPH: unknown trap %08x on ch %d\n", status, cid);
		NV_INFO(dev,
				"DISPATCH_TRAP = %08x\n"
				"M2MF_TRAP = %08x\n"
				"CCACHE_TRAP = %08x\n"
				"UNK6000_TRAP_UNK0 = %08x\n"
				"UNK6000_TRAP_UNK1 = %08x\n"
				"MACRO_TRAP = %08x\n"
				"UNK5800_TRAP = %08x\n",
				nv_rd32(dev, NVC0_PGRAPH_DISPATCH_TRAP),
				nv_rd32(dev, NVC0_PGRAPH_M2MF_TRAP),
				nv_rd32(dev, NVC0_PGRAPH_CCACHE_TRAP),
				nv_rd32(dev, NVC0_PGRAPH_UNK6000_TRAP_UNK0),
				nv_rd32(dev, NVC0_PGRAPH_UNK6000_TRAP_UNK1),
				nv_rd32(dev, NVC0_PGRAPH_MACRO_TRAP),
				nv_rd32(dev, NVC0_PGRAPH_UNK5800_TRAP));

		nv_wr32(dev, NVC0_PGRAPH_TRAP, status);
	}
}

void nvc0_graph_irq_handler(struct drm_device *dev, int irq)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_engine *graph;
	uint32_t status;
	unsigned long flags;
	uint32_t pgraph, addr, datal, datah, ecode, grcl, subc, mthd;
	int cid;
#define PGRAPH_ERROR(name)												\
	NV_ERROR(dev, "%s: st %08x ch %d sub %d [%04x] mthd %04x data %08x%08x\n", \
			 name, pgraph, cid, subc, grcl, mthd, datah, datal);

	graph = nvc0_graph(dev_priv->engines[PSCNV_ENGINE_GRAPH]);

	spin_lock_irqsave(&graph->lock, flags);

	status = nv_rd32(dev, NVC0_PGRAPH_INTR);
	ecode = nv_rd32(dev, NVC0_PGRAPH_DATA_ERROR);
	pgraph = nv_rd32(dev, NVC0_PGRAPH_STATUS);
	addr = nv_rd32(dev, NVC0_PGRAPH_TRAPPED_ADDR);
	mthd = addr & NVC0_PGRAPH_TRAPPED_ADDR_MTHD__MASK;
	subc = (addr & NVC0_PGRAPH_TRAPPED_ADDR_SUBCH__MASK) >> 
		NVC0_PGRAPH_TRAPPED_ADDR_SUBCH__SHIFT;
	datal = nv_rd32(dev, NVC0_PGRAPH_TRAPPED_DATA_LOW);
	datah = nv_rd32(dev, NVC0_PGRAPH_TRAPPED_DATA_HIGH);
	grcl = nv_rd32(dev, NVC0_PGRAPH_DISPATCH_CTX_SWITCH) & 0xffff;
	cid = -1;

	if (status & NVC0_PGRAPH_INTR_NOTIFY) {
		PGRAPH_ERROR("PGRAPH_NOTIFY");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_NOTIFY);
		status &= ~NVC0_PGRAPH_INTR_NOTIFY;
	}
	if (status & NVC0_PGRAPH_INTR_QUERY) {
		PGRAPH_ERROR("PGRAPH_QUERY");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_QUERY);
		status &= ~NVC0_PGRAPH_INTR_QUERY;
	}
	if (status & NVC0_PGRAPH_INTR_SYNC) {
		PGRAPH_ERROR("PGRAPH_SYNC");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_SYNC);
		status &= ~NVC0_PGRAPH_INTR_SYNC;
	}
	if (status & NVC0_PGRAPH_INTR_ILLEGAL_MTHD) {
		PGRAPH_ERROR("PGRAPH_ILLEGAL_MTHD");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_ILLEGAL_MTHD);
		status &= ~NVC0_PGRAPH_INTR_ILLEGAL_MTHD;
	}
	if (status & NVC0_PGRAPH_INTR_ILLEGAL_CLASS) {
		PGRAPH_ERROR("PGRAPH_ILLEGAL_CLASS");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_ILLEGAL_CLASS);
		status &= ~NVC0_PGRAPH_INTR_ILLEGAL_CLASS;
	}
	if (status & NVC0_PGRAPH_INTR_DOUBLE_NOTIFY) {
		PGRAPH_ERROR("PGRAPH_DOUBLE_NOITFY");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_DOUBLE_NOTIFY);
		status &= ~NVC0_PGRAPH_INTR_DOUBLE_NOTIFY;
	}
	if (status & NVC0_PGRAPH_INTR_UNK7) {
		PGRAPH_ERROR("PGRAPH_UNK7");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_UNK7);
		status &= ~NVC0_PGRAPH_INTR_UNK7;
	}
	if (status & NVC0_PGRAPH_INTR_FIRMWARE_MTHD) {
		PGRAPH_ERROR("PGRAPH_FIRMWARE_MTHD");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_FIRMWARE_MTHD);
		status &= ~NVC0_PGRAPH_INTR_FIRMWARE_MTHD;
	}
	if (status & NVC0_PGRAPH_INTR_BUFFER_NOTIFY) {
		PGRAPH_ERROR("PGRAPH_BUFFER_NOTIFY");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_BUFFER_NOTIFY);
		status &= ~NVC0_PGRAPH_INTR_BUFFER_NOTIFY;
	}
	if (status & NVC0_PGRAPH_INTR_CTXCTL_UP) {
		PGRAPH_ERROR("PGRAPH_CTXCTL_UP");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_CTXCTL_UP);
		status &= ~NVC0_PGRAPH_INTR_CTXCTL_UP;
	}
	if (status & NVC0_PGRAPH_INTR_DATA_ERROR) {
		const struct pscnv_enum *ev;
		ev = pscnv_enum_find(dispatch_errors, ecode);
		if (ev) {
			NV_ERROR(dev, "PGRAPH_DATA_ERROR [%s]", ev->name);
			PGRAPH_ERROR("");
		} else {
			NV_ERROR(dev, "PGRAPH_DATA_ERROR [%x]", ecode);
		}
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_DATA_ERROR);
		status &= ~NVC0_PGRAPH_INTR_DATA_ERROR;
	}
	if (status & NVC0_PGRAPH_INTR_TRAP) {
		nvc0_graph_trap_handler(dev, cid);
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_TRAP);
		status &= ~NVC0_PGRAPH_INTR_TRAP;
	}
	if (status & NVC0_PGRAPH_INTR_SINGLE_STEP) {
		PGRAPH_ERROR("PGRAPH_SINGLE_STEP");
		nv_wr32(dev, NVC0_PGRAPH_INTR, NVC0_PGRAPH_INTR_SINGLE_STEP);
		status &= ~NVC0_PGRAPH_INTR_SINGLE_STEP;
	}
	if (status) {
		NV_ERROR(dev, "Unknown PGRAPH interrupt(s) %08x\n", status);
		PGRAPH_ERROR("PGRAPH");
		nv_wr32(dev, NVC0_PGRAPH_INTR, status);
	}

	nv_wr32(dev, NVC0_PGRAPH_FIFO_CONTROL, (1 << 16) | 1);

	spin_unlock_irqrestore(&graph->lock, flags);
}
