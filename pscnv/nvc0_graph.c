/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "nvc0_graph.h"
#include "nvc0_grctx.h"

#include "nouveau_enum.h"
#include "pscnv_chan.h"
#include "pscnv_vm.h"

#include "nvc0_graph_error_names.inc"

/*******************************************************************************
 * PGRAPH per channel context
 ******************************************************************************/

int
nvc0_graph_chan_alloc(struct pscnv_engine *eng, struct pscnv_chan *chan)
{
	struct drm_device *dev = eng->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	struct nvc0_graph_engine *graph = NVC0_GRAPH(eng);
	struct nvc0_graph_chan *grch; /* per channel graph data */
	
	uint32_t cookie = 0xcc000000 + (chan->cid << 8);
	int ret = 0;
	int i;
	
	NV_INFO(dev, "PGRAPH: adding to channel %d in vspace %d\n",
		chan->cid, chan->vspace->vid);
	
	grch = kzalloc(sizeof *grch, GFP_KERNEL);
	if (!grch) {
		ret = -ENOMEM;
		goto fail_kzalloc;
	}
	
	/* allocate the per-channel context page (grctx) */
	grch->grctx = pscnv_mem_alloc_and_map(chan->vspace, graph->grctx_size,
		PSCNV_GEM_CONTIG | PSCNV_GEM_NOUSER | PSCNV_ZEROFILL | PSCNV_MAP_KERNEL,
		cookie, &grch->grctx_vm_base);
	
	if (!grch->grctx) {
		ret = -ENOMEM;
		goto fail_grctx;
	}

	/* allocate memory for a "mmio list" buffer that's used by the HUB
	 * fuc to modify some per-context register settings on first load
	 * of the context.
	 */
	grch->mmio = pscnv_mem_alloc_and_map(chan->vspace, 0x1000 /* size */,
		PSCNV_GEM_CONTIG | PSCNV_MAP_KERNEL,
		cookie + 1, &grch->mmio_vm_base);
	
	if (!grch->mmio) {
		ret = -ENOMEM;
		goto fail_mmio_list;
	}

	/* allocate buffers referenced by mmio list
	 * these buffers are the counterpart to obj08004, obj0800c, obj19848
	 * of the original pscnv */
	for (i = 0; graph->mmio_data[i].size && i < ARRAY_SIZE(graph->mmio_data); i++) {
		
		grch->data[i].mem = pscnv_mem_alloc_and_map(chan->vspace,
			graph->mmio_data[i].size,
			PSCNV_GEM_CONTIG | PSCNV_MAP_KERNEL,
			cookie + 0x10 + i, &grch->data[i].vm_base);
	
		if (!grch->data[i].mem) {
			ret = -ENOMEM;
			goto fail_mmio_data;
		}
	}

	/* finally, fill in the mmio list and point the context at it */
	for (i = 0; graph->mmio_list[i].addr && i < ARRAY_SIZE(graph->mmio_list); i++) {
		u32 addr = graph->mmio_list[i].addr;
		u32 data = graph->mmio_list[i].data;
		u32 shift = graph->mmio_list[i].shift;
		u32 buffer = graph->mmio_list[i].buffer;
		

		if (shift) {
			u64 info = grch->data[buffer].vm_base;
			data |= info >> shift;
		}

		nv_wv32(grch->mmio, grch->mmio_nr++ * 4, addr);
		nv_wv32(grch->mmio, grch->mmio_nr++ * 4, data);
	}

	/* fill grctx with the initial values from the template channel */
	for (i = 0; i < graph->grctx_size; i += 4)
		nv_wv32(grch->grctx, i, graph->data[i / 4]);

	/* set pointer to mmio list */
	nv_wv32(grch->grctx, 0x00, grch->mmio_nr / 2);
	nv_wv32(grch->grctx, 0x04, grch->mmio_vm_base >> 8);
	
	chan->engdata[PSCNV_ENGINE_GRAPH] = grch;
	
	/* register this engines context with the channel */
	nv_wv32(chan->bo, 0x210, lower_32_bits(grch->grctx_vm_base) | 4);
	nv_wv32(chan->bo, 0x214, upper_32_bits(grch->grctx_vm_base));
	dev_priv->vm->bar_flush(dev);

	return 0;
	
fail_mmio_data:
	for (i = 0; i < ARRAY_SIZE(graph->mmio_data); i++) {
		if (grch->data[i].mem) {
			pscnv_vspace_unmap(chan->vspace, grch->data[i].vm_base);
			pscnv_mem_free(grch->data[i].mem);
		}
	}
	pscnv_vspace_unmap(chan->vspace, grch->mmio_vm_base);
	pscnv_mem_free(grch->mmio);
	
fail_mmio_list:
	pscnv_vspace_unmap(chan->vspace, grch->grctx_vm_base);
	pscnv_mem_free(grch->grctx);
	
fail_grctx:
	kfree(grch);

fail_kzalloc:
	NV_ERROR(dev, "PGRAPH: Couldn't allocate channel %d!\n", chan->cid);
	
	return ret;
}

void
nvc0_graph_chan_free(struct pscnv_engine *eng, struct pscnv_chan *ch)
{
	struct drm_device *dev = eng->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	struct nvc0_graph_engine *graph = NVC0_GRAPH(eng);
	struct nvc0_graph_chan *grch = ch->engdata[PSCNV_ENGINE_GRAPH];
	int i;
	
	ch->engdata[PSCNV_ENGINE_GRAPH] = NULL;

	for (i = 0; i < ARRAY_SIZE(graph->mmio_data); i++) {
		if (grch->data[i].mem) {
			pscnv_vspace_unmap(ch->vspace, grch->data[i].vm_base);
			pscnv_mem_free(grch->data[i].mem);
		}
	}
	pscnv_vspace_unmap(ch->vspace, grch->mmio_vm_base);
	pscnv_mem_free(grch->mmio);
	
	pscnv_vspace_unmap(ch->vspace, grch->grctx_vm_base);
	pscnv_mem_free(grch->grctx);
	
	kfree(grch);
	
	nv_wv32(ch->bo, 0x210, 0);
	nv_wv32(ch->bo, 0x214, 0);
	dev_priv->vm->bar_flush(dev);
}

void
nvc0_graph_chan_kill(struct pscnv_engine *eng, struct pscnv_chan *ch)
{
	/* FIXME */
}

#include "nvc0_graph_reg_lists.inc"

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

void
nvc0_graph_mmio(struct nvc0_graph_engine *graph, const struct nvc0_graph_pack *p)
{
	struct drm_device *dev = graph->base.dev;
	
	const struct nvc0_graph_pack *pack;
	const struct nvc0_graph_init *init;

	pack_for_each_init(init, pack, p) {
		u32 next = init->addr + init->count * init->pitch;
		u32 addr = init->addr;
		while (addr < next) {
			nv_wr32(dev, addr, init->data);
			addr += init->pitch;
		}
	}
}

void
nvc0_graph_icmd(struct nvc0_graph_engine *graph, const struct nvc0_graph_pack *p)
{
	struct drm_device *dev = graph->base.dev;
	
	const struct nvc0_graph_pack *pack;
	const struct nvc0_graph_init *init;
	u32 data = 0;

	nv_wr32(dev, 0x400208, 0x80000000);

	pack_for_each_init(init, pack, p) {
		u32 next = init->addr + init->count * init->pitch;
		u32 addr = init->addr;

		if ((pack == p && init == p->init) || data != init->data) {
			nv_wr32(dev, 0x400204, init->data);
			data = init->data;
		}

		while (addr < next) {
			nv_wr32(dev, 0x400200, addr);
			nv_wait(dev, 0x400700, 0x00000002, 0x00000000);
			addr += init->pitch;
		}
	}

	nv_wr32(dev, 0x400208, 0x00000000);
}

void
nvc0_graph_mthd(struct nvc0_graph_engine *graph, const struct nvc0_graph_pack *p)
{
	struct drm_device *dev = graph->base.dev;
	
	const struct nvc0_graph_pack *pack;
	const struct nvc0_graph_init *init;
	u32 data = 0;

	pack_for_each_init(init, pack, p) {
		u32 ctrl = 0x80000000 | pack->type;
		u32 next = init->addr + init->count * init->pitch;
		u32 addr = init->addr;

		if ((pack == p && init == p->init) || data != init->data) {
			nv_wr32(dev, 0x40448c, init->data);
			data = init->data;
		}

		while (addr < next) {
			nv_wr32(dev, 0x404488, ctrl | (addr << 14));
			addr += init->pitch;
		}
	}
}

/*******************************************************************************
 * PGRAPH interrupt handling
 ******************************************************************************/

static void
nvc0_graph_trap_gpc_rop(struct nvc0_graph_engine *graph, int gpc)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 trap[4];
	int i;

	trap[0] = nv_rd32(dev, GPC_UNIT(gpc, 0x0420));
	trap[1] = nv_rd32(dev, GPC_UNIT(gpc, 0x0434));
	trap[2] = nv_rd32(dev, GPC_UNIT(gpc, 0x0438));
	trap[3] = nv_rd32(dev, GPC_UNIT(gpc, 0x043c));

	NV_ERROR(dev, "GPC%d/PROP trap:", gpc);
	for (i = 0; i <= 29; ++i) {
		if (!(trap[0] & (1 << i)))
			continue;
		pr_cont(" ");
		nouveau_enum_print(nvc0_gpc_rop_error, i);
	}
	pr_cont("\n");

	NV_ERROR(dev, "x = %u, y = %u, format = %x, storage type = %x\n",
		 trap[1] & 0xffff, trap[1] >> 16, (trap[2] >> 8) & 0x3f,
		 trap[3] & 0xff);
	nv_wr32(dev, GPC_UNIT(gpc, 0x0420), 0xc0000000);
}

static void
nvc0_graph_trap_mp(struct nvc0_graph_engine *graph, int gpc, int tpc)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 werr = nv_rd32(dev, TPC_UNIT(gpc, tpc, 0x648));
	u32 gerr = nv_rd32(dev, TPC_UNIT(gpc, tpc, 0x650));

	NV_ERROR(dev, "GPC%i/TPC%i/MP trap:", gpc, tpc);
	nouveau_bitfield_print(nvc0_mp_global_error, gerr);
	if (werr) {
		pr_cont(" ");
		nouveau_enum_print(nvc0_mp_warp_error, werr & 0xffff);
	}
	pr_cont("\n");

	nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x648), 0x00000000);
	nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x650), gerr);
}

static void
nvc0_graph_trap_tpc(struct nvc0_graph_engine *graph, int gpc, int tpc)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 stat = nv_rd32(dev, TPC_UNIT(gpc, tpc, 0x0508));

	if (stat & 0x00000001) {
		u32 trap = nv_rd32(dev, TPC_UNIT(gpc, tpc, 0x0224));
		NV_ERROR(dev, "GPC%d/TPC%d/TEX: 0x%08x\n", gpc, tpc, trap);
		nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x0224), 0xc0000000);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000002) {
		nvc0_graph_trap_mp(graph, gpc, tpc);
		stat &= ~0x00000002;
	}

	if (stat & 0x00000004) {
		u32 trap = nv_rd32(dev, TPC_UNIT(gpc, tpc, 0x0084));
		NV_ERROR(dev, "GPC%d/TPC%d/POLY: 0x%08x\n", gpc, tpc, trap);
		nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x0084), 0xc0000000);
		stat &= ~0x00000004;
	}

	if (stat & 0x00000008) {
		u32 trap = nv_rd32(dev, TPC_UNIT(gpc, tpc, 0x048c));
		NV_ERROR(dev, "GPC%d/TPC%d/L1C: 0x%08x\n", gpc, tpc, trap);
		nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x048c), 0xc0000000);
		stat &= ~0x00000008;
	}

	if (stat) {
		NV_ERROR(dev, "GPC%d/TPC%d/0x%08x: unknown\n", gpc, tpc, stat);
	}
}

static void
nvc0_graph_trap_gpc(struct nvc0_graph_engine *graph, int gpc)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 stat = nv_rd32(dev, GPC_UNIT(gpc, 0x2c90));
	int tpc;

	if (stat & 0x00000001) {
		nvc0_graph_trap_gpc_rop(graph, gpc);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000002) {
		u32 trap = nv_rd32(dev, GPC_UNIT(gpc, 0x0900));
		NV_ERROR(dev, "GPC%d/ZCULL: 0x%08x\n", gpc, trap);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		stat &= ~0x00000002;
	}

	if (stat & 0x00000004) {
		u32 trap = nv_rd32(dev, GPC_UNIT(gpc, 0x1028));
		NV_ERROR(dev, "GPC%d/CCACHE: 0x%08x\n", gpc, trap);
		nv_wr32(dev, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		stat &= ~0x00000004;
	}

	if (stat & 0x00000008) {
		u32 trap = nv_rd32(dev, GPC_UNIT(gpc, 0x0824));
		NV_ERROR(dev, "GPC%d/ESETUP: 0x%08x\n", gpc, trap);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		stat &= ~0x00000009;
	}

	for (tpc = 0; tpc < graph->tpc_nr[gpc]; tpc++) {
		u32 mask = 0x00010000 << tpc;
		if (stat & mask) {
			nvc0_graph_trap_tpc(graph, gpc, tpc);
			nv_wr32(dev, GPC_UNIT(gpc, 0x2c90), mask);
			stat &= ~mask;
		}
	}

	if (stat) {
		NV_ERROR(dev, "GPC%d/0x%08x: unknown\n", gpc, stat);
	}
}

static void
nvc0_graph_trap_intr(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 trap = nv_rd32(dev, 0x400108);
	int rop, gpc, i;

	if (trap & 0x00000001) {
		u32 stat = nv_rd32(dev, 0x404000);
		NV_ERROR(dev, "DISPATCH 0x%08x\n", stat);
		nv_wr32(dev, 0x404000, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000001);
		trap &= ~0x00000001;
	}

	if (trap & 0x00000002) {
		u32 stat = nv_rd32(dev, 0x404600);
		NV_ERROR(dev, "M2MF 0x%08x\n", stat);
		nv_wr32(dev, 0x404600, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000002);
		trap &= ~0x00000002;
	}

	if (trap & 0x00000008) {
		u32 stat = nv_rd32(dev, 0x408030);
		NV_ERROR(dev, "CCACHE 0x%08x\n", stat);
		nv_wr32(dev, 0x408030, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000008);
		trap &= ~0x00000008;
	}

	if (trap & 0x00000010) {
		u32 stat = nv_rd32(dev, 0x405840);
		NV_ERROR(dev, "SHADER 0x%08x\n", stat);
		nv_wr32(dev, 0x405840, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000010);
		trap &= ~0x00000010;
	}

	if (trap & 0x00000040) {
		u32 stat = nv_rd32(dev, 0x40601c);
		NV_ERROR(dev, "UNK6 0x%08x\n", stat);
		nv_wr32(dev, 0x40601c, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000040);
		trap &= ~0x00000040;
	}

	if (trap & 0x00000080) {
		u32 stat = nv_rd32(dev, 0x404490);
		NV_ERROR(dev, "MACRO 0x%08x\n", stat);
		nv_wr32(dev, 0x404490, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000080);
		trap &= ~0x00000080;
	}

	if (trap & 0x00000100) {
		u32 stat = nv_rd32(dev, 0x407020);

		NV_ERROR(dev, "SKED:");
		for (i = 0; i <= 29; ++i) {
			if (!(stat & (1 << i)))
				continue;
			pr_cont(" ");
			nouveau_enum_print(nve0_sked_error, i);
		}
		pr_cont("\n");

		if (stat & 0x3fffffff)
			nv_wr32(dev, 0x407020, 0x40000000);
		nv_wr32(dev, 0x400108, 0x00000100);
		trap &= ~0x00000100;
	}

	if (trap & 0x01000000) {
		u32 stat = nv_rd32(dev, 0x400118);
		for (gpc = 0; stat && gpc < graph->gpc_nr; gpc++) {
			u32 mask = 0x00000001 << gpc;
			if (stat & mask) {
				nvc0_graph_trap_gpc(graph, gpc);
				nv_wr32(dev, 0x400118, mask);
				stat &= ~mask;
			}
		}
		nv_wr32(dev, 0x400108, 0x01000000);
		trap &= ~0x01000000;
	}

	if (trap & 0x02000000) {
		for (rop = 0; rop < graph->rop_nr; rop++) {
			u32 statz = nv_rd32(dev, ROP_UNIT(rop, 0x070));
			u32 statc = nv_rd32(dev, ROP_UNIT(rop, 0x144));
			NV_ERROR(dev, "ROP%d 0x%08x 0x%08x\n",
				 rop, statz, statc);
			nv_wr32(dev, ROP_UNIT(rop, 0x070), 0xc0000000);
			nv_wr32(dev, ROP_UNIT(rop, 0x144), 0xc0000000);
		}
		nv_wr32(dev, 0x400108, 0x02000000);
		trap &= ~0x02000000;
	}

	if (trap) {
		NV_ERROR(dev, "TRAP UNHANDLED 0x%08x\n", trap);
		nv_wr32(dev, 0x400108, trap);
	}
}

static void
nvc0_graph_ctxctl_debug_unit(struct nvc0_graph_engine *graph, u32 base)
{
	struct drm_device *dev = graph->base.dev;
	
	NV_ERROR(dev, "%06x - done 0x%08x\n", base,
		 nv_rd32(dev, base + 0x400));
	NV_ERROR(dev, "%06x - stat 0x%08x 0x%08x 0x%08x 0x%08x\n", base,
		 nv_rd32(dev, base + 0x800), nv_rd32(dev, base + 0x804),
		 nv_rd32(dev, base + 0x808), nv_rd32(dev, base + 0x80c));
	NV_ERROR(dev, "%06x - stat 0x%08x 0x%08x 0x%08x 0x%08x\n", base,
		 nv_rd32(dev, base + 0x810), nv_rd32(dev, base + 0x814),
		 nv_rd32(dev, base + 0x818), nv_rd32(dev, base + 0x81c));
}

void
nvc0_graph_ctxctl_debug(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 gpcnr = nv_rd32(dev, 0x409604) & 0xffff;
	u32 gpc;

	nvc0_graph_ctxctl_debug_unit(graph, 0x409000);
	for (gpc = 0; gpc < gpcnr; gpc++)
		nvc0_graph_ctxctl_debug_unit(graph, 0x502000 + (gpc * 0x8000));
}

static void
nvc0_graph_ctxctl_isr(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 ustat = nv_rd32(dev, 0x409c18);

	if (ustat & 0x00000001)
		NV_ERROR(dev, "CTXCTL ucode error\n");
	if (ustat & 0x00080000)
		NV_ERROR(dev, "CTXCTL watchdog timeout\n");
	if (ustat & ~0x00080001)
		NV_ERROR(dev, "CTXCTL 0x%08x\n", ustat);

	nvc0_graph_ctxctl_debug(graph);
	nv_wr32(dev, 0x409c20, ustat);
}

static void
nvc0_graph_irq_handler(struct drm_device *dev, int irq)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_engine *graph;

	u64 inst = nv_rd32(dev, 0x409b00) & 0x0fffffff;
	u32 stat = nv_rd32(dev, 0x400100);
	u32 addr = nv_rd32(dev, 0x400704);
	u32 mthd = (addr & 0x00003ffc);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 data = nv_rd32(dev, 0x400708);
	u32 code = nv_rd32(dev, 0x400110);
	u32 class = nv_rd32(dev, 0x404200 + (subc * 4));
	int chid;
	
	graph = NVC0_GRAPH(dev_priv->engines[PSCNV_ENGINE_GRAPH]);

	chid   = pscnv_chan_handle_lookup(dev, inst);

	if (stat & 0x00000010) {
		NV_ERROR(dev,
			 "ILLEGAL_MTHD ch %d [0x%010llx] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			 chid, inst << 12, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000010);
		stat &= ~0x00000010;
	}

	if (stat & 0x00000020) {
		NV_ERROR(dev,
			 "ILLEGAL_CLASS ch %d [0x%010llx] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			 chid, inst << 12, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000020);
		stat &= ~0x00000020;
	}

	if (stat & 0x00100000) {
		NV_ERROR(dev, "DATA_ERROR [");
		nouveau_enum_print(nv50_data_error_names, code);
		pr_cont("] ch %d [0x%010llx] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			chid, inst << 12, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00100000);
		stat &= ~0x00100000;
	}

	if (stat & 0x00200000) {
		NV_ERROR(dev, "TRAP ch %d [0x%010llx]\n", chid, inst << 12);
		nvc0_graph_trap_intr(graph);
		nv_wr32(dev, 0x400100, 0x00200000);
		stat &= ~0x00200000;
	}

	if (stat & 0x00080000) {
		nvc0_graph_ctxctl_isr(graph);
		nv_wr32(dev, 0x400100, 0x00080000);
		stat &= ~0x00080000;
	}

	if (stat) {
		NV_ERROR(dev, "unknown stat 0x%08x\n", stat);
		nv_wr32(dev, 0x400100, stat);
	}

	nv_wr32(dev, 0x400500, 0x00010001);
}

/*******************************************************************************
 * PGRAPH initialization and takedown
 ******************************************************************************/

static void
nvc0_graph_init_csdata(struct nvc0_graph_engine *graph,
		       const struct nvc0_graph_pack *pack,
		       u32 falcon, u32 starstar, u32 base)
{
	struct drm_device *dev = graph->base.dev;
	
	const struct nvc0_graph_pack *iter;
	const struct nvc0_graph_init *init;
	u32 addr = ~0, prev = ~0, xfer = 0;
	u32 star, temp;

	nv_wr32(dev, falcon + 0x01c0, 0x02000000 + starstar);
	star = nv_rd32(dev, falcon + 0x01c4);
	temp = nv_rd32(dev, falcon + 0x01c4);
	if (temp > star)
		star = temp;
	nv_wr32(dev, falcon + 0x01c0, 0x01000000 + star);

	pack_for_each_init(init, iter, pack) {
		u32 head = init->addr - base;
		u32 tail = head + init->count * init->pitch;
		while (head < tail) {
			if (head != prev + 4 || xfer >= 32) {
				if (xfer) {
					u32 data = ((--xfer << 26) | addr);
					nv_wr32(dev, falcon + 0x01c4, data);
					star += 4;
				}
				addr = head;
				xfer = 0;
			}
			prev = head;
			xfer = xfer + 1;
			head = head + init->pitch;
		}
	}

	nv_wr32(dev, falcon + 0x01c4, (--xfer << 26) | addr);
	nv_wr32(dev, falcon + 0x01c0, 0x01000004 + starstar);
	nv_wr32(dev, falcon + 0x01c4, star + 4);
}

static int
nvc0_graph_init_ctxctl(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;

	u32 r000260;
	int i;

	/* load HUB microcode */
	r000260 = nv_mask(dev, 0x000260, 0x00000001, 0x00000000);
	nv_wr32(dev, 0x4091c0, 0x01000000);
	for (i = 0; i < nvc0_graph_fecs_ucode.data.size / 4; i++)
		nv_wr32(dev, 0x4091c4, nvc0_graph_fecs_ucode.data.data[i]);

	nv_wr32(dev, 0x409180, 0x01000000);
	for (i = 0; i < nvc0_graph_fecs_ucode.code.size / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(dev, 0x409188, i >> 6);
		nv_wr32(dev, 0x409184, nvc0_graph_fecs_ucode.code.data[i]);
	}

	/* load GPC microcode */
	nv_wr32(dev, 0x41a1c0, 0x01000000);
	for (i = 0; i < nvc0_graph_gpccs_ucode.data.size / 4; i++)
		nv_wr32(dev, 0x41a1c4, nvc0_graph_gpccs_ucode.data.data[i]);

	nv_wr32(dev, 0x41a180, 0x01000000);
	for (i = 0; i < nvc0_graph_gpccs_ucode.code.size / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(dev, 0x41a188, i >> 6);
		nv_wr32(dev, 0x41a184, nvc0_graph_gpccs_ucode.code.data[i]);
	}
	nv_wr32(dev, 0x000260, r000260);

	/* load register lists */
	nvc0_graph_init_csdata(graph, nvc0_grctx_pack_hub, 0x409000, 0x000, 0x000000);
	nvc0_graph_init_csdata(graph, nvc0_grctx_pack_gpc, 0x41a000, 0x000, 0x418000);
	nvc0_graph_init_csdata(graph, nvc0_grctx_pack_tpc, 0x41a000, 0x004, 0x419800);
	/* ppc seems to not exists on nvc0
	nvc0_graph_init_csdata(graph, cclass->ppc, 0x41a000, 0x008, 0x41be00); */

	/* start HUB ucode running, it'll init the GPCs */
	nv_wr32(dev, 0x40910c, 0x00000000);
	nv_wr32(dev, 0x409100, 0x00000002);
	if (!nv_wait(dev, 0x409800, 0x80000000, 0x80000000)) {
		NV_ERROR(dev, "HUB_INIT timed out\n");
		nvc0_graph_ctxctl_debug(graph);
		return -EBUSY;
	}

	graph->grctx_size = nv_rd32(dev, 0x409804);
	if (graph->data == NULL) {
		int ret = nvc0_grctx_generate(graph);
		if (ret) {
			NV_ERROR(dev, "failed to construct context\n");
			return ret;
		}
	}

	return 0;
}

static inline void
nvc0_graph_init_reset(struct drm_device *dev)
{
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) & 0xffffefff);
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) | 0x00001000);
}

static void
nvc0_graph_takedown(struct pscnv_engine *eng)
{
	struct nvc0_graph_engine *graph = NVC0_GRAPH(eng);
	struct drm_device *dev = eng->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	BUG_ON(!graph);
	
	nvc0_graph_init_reset(dev);
	
	if (dev_priv->irq_handler[12])
		nouveau_irq_unregister(dev, 12);

	WARN_ON(!graph->unk4188b4);
	WARN_ON(!graph->unk4188b8);
	
	if (graph->unk4188b4) {
		pscnv_mem_free(graph->unk4188b4);
		graph->unk4188b4 = NULL;
	}
	
	if (graph->unk4188b8) {
		pscnv_mem_free(graph->unk4188b8);
		graph->unk4188b8 = NULL;
	}

	dev_priv->engines[PSCNV_ENGINE_GRAPH] = NULL;
	kfree(graph);
}

static int
nvc0_graph_ctor(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_engine *graph;
	int ret, i;
	
	graph = kzalloc(sizeof *graph, GFP_KERNEL);
	
	if (!graph) {
		ret = -ENOMEM;
		goto fail_kzalloc;
	}
	
	graph->base.dev = dev;
	graph->base.takedown = nvc0_graph_takedown;
	graph->base.chan_alloc = nvc0_graph_chan_alloc;
	graph->base.chan_kill = nvc0_graph_chan_kill;
	graph->base.chan_free = nvc0_graph_chan_free;
	
	/* unk4188b4 and unk4188b8 match the obj188b4 and obj188b8 of the
	 * original pscnv.
	 *
	 * obj08004, obj0800c, obj19848 are not allocated here. Instead these
	 * are allocated per channel in nvc0_chan_alloc() */
	
	graph->unk4188b4 = pscnv_mem_alloc(dev, 0x1000,
		PSCNV_GEM_CONTIG | PSCNV_MAP_KERNEL, 0, 0x4188b4, NULL);
		
	if (!graph->unk4188b4) {
		goto fail_unk4188b4;
	}
	
	pscnv_bo_memset(graph->unk4188b4, 0x00000010);
	
	graph->unk4188b8 = pscnv_mem_alloc(dev, 0x1000,
		PSCNV_GEM_CONTIG | PSCNV_MAP_KERNEL, 0, 0x4188b8, NULL);
		
	if (!graph->unk4188b8) {
		goto fail_unk4188b8;
	}
	
	pscnv_bo_memset(graph->unk4188b8, 0x00000010);
	
	dev_priv->vm->bar_flush(dev);

	graph->rop_nr = (nv_rd32(dev, 0x409604) & 0x001f0000) >> 16;
	graph->gpc_nr =  nv_rd32(dev, 0x409604) & 0x0000001f;
	for (i = 0; i < graph->gpc_nr; i++) {
		graph->tpc_nr[i]  = nv_rd32(dev, GPC_UNIT(i, 0x2608));
		graph->tpc_total += graph->tpc_nr[i];
	}

	/*XXX: these need figuring out... though it might not even matter */
	switch (dev_priv->chipset) {
	case 0xc0:
		if (graph->tpc_total == 11) { /* 465, 3/4/4/0, 4 */
			graph->magic_not_rop_nr = 0x07;
		} else
		if (graph->tpc_total == 14) { /* 470, 3/3/4/4, 5 */
			graph->magic_not_rop_nr = 0x05;
		} else
		if (graph->tpc_total == 15) { /* 480, 3/4/4/4, 6 */
			graph->magic_not_rop_nr = 0x06;
		}
		break;
	case 0xc3: /* 450, 4/0/0/0, 2 */
		graph->magic_not_rop_nr = 0x03;
		break;
	case 0xc4: /* 460, 3/4/0/0, 4 */
		graph->magic_not_rop_nr = 0x01;
		break;
	case 0xc1: /* 2/0/0/0, 1 */
		graph->magic_not_rop_nr = 0x01;
		break;
	case 0xc8: /* 4/4/3/4, 5 */
		graph->magic_not_rop_nr = 0x06;
		break;
	case 0xce: /* 4/4/0/0, 4 */
		graph->magic_not_rop_nr = 0x03;
		break;
	case 0xcf: /* 4/0/0/0, 3 */
		graph->magic_not_rop_nr = 0x03;
		break;
	case 0xd7:
	case 0xd9: /* 1/0/0/0, 1 */
		graph->magic_not_rop_nr = 0x01;
		break;
	default:
		NV_ERROR(dev, "PGRAPH: unknown chipset 0x%x\n", dev_priv->chipset);
		ret = -EINVAL;
		goto fail_chipset;
	}
	
	dev_priv->engines[PSCNV_ENGINE_GRAPH] = &graph->base;

	return 0;

fail_chipset:
	pscnv_mem_free(graph->unk4188b8);

fail_unk4188b8:
	pscnv_mem_free(graph->unk4188b4);

fail_unk4188b4:
	kfree(graph);
		
fail_kzalloc:
	NV_ERROR(dev, "PGRAPH: Couldn't allocate nvc0_graph_engine!\n");
	return ret;
}

int
nvc0_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_engine *graph;
	
	u32 magicgpc918;
	u32 data[TPC_MAX / 8] = {};
	u8  tpcnr[GPC_MAX];
	int gpc, tpc, rop;
	int ret, i;
	
	NV_INFO(dev, "PGRAPH: Initializing...\n");
	
	ret = nvc0_graph_ctor(dev);
		
	if (ret) {
		goto fail_ctor;
	}
	
	graph = NVC0_GRAPH(dev_priv->engines[PSCNV_ENGINE_GRAPH]);
	BUG_ON(!graph);
	
	nvc0_graph_init_reset(dev);
	
	magicgpc918 = DIV_ROUND_UP(0x00800000, graph->tpc_total);

	nv_wr32(dev, GPC_BCAST(0x0880), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x08a4), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x0888), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x088c), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x0890), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x0894), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x08b4), graph->unk4188b4->start >> 8);
	nv_wr32(dev, GPC_BCAST(0x08b8), graph->unk4188b8->start >> 8);

	nvc0_graph_mmio(graph, nvc0_graph_pack_mmio);

	memcpy(tpcnr, graph->tpc_nr, sizeof(graph->tpc_nr));
	for (i = 0, gpc = -1; i < graph->tpc_total; i++) {
		do {
			gpc = (gpc + 1) % graph->gpc_nr;
		} while (!tpcnr[gpc]);
		tpc = graph->tpc_nr[gpc] - tpcnr[gpc]--;

		data[i / 8] |= tpc << ((i % 8) * 4);
	}

	nv_wr32(dev, GPC_BCAST(0x0980), data[0]);
	nv_wr32(dev, GPC_BCAST(0x0984), data[1]);
	nv_wr32(dev, GPC_BCAST(0x0988), data[2]);
	nv_wr32(dev, GPC_BCAST(0x098c), data[3]);

	for (gpc = 0; gpc < graph->gpc_nr; gpc++) {
		nv_wr32(dev, GPC_UNIT(gpc, 0x0914),
			graph->magic_not_rop_nr << 8 | graph->tpc_nr[gpc]);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0910), 0x00040000 |
			graph->tpc_total);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	if (dev_priv->chipset != 0xd7)
		nv_wr32(dev, GPC_BCAST(0x1bd4), magicgpc918);
	else
		nv_wr32(dev, GPC_BCAST(0x3fd4), magicgpc918);

	nv_wr32(dev, GPC_BCAST(0x08ac), nv_rd32(dev, 0x100800));

	nv_wr32(dev, 0x400500, 0x00010001);

	nv_wr32(dev, 0x400100, 0xffffffff);
	nv_wr32(dev, 0x40013c, 0xffffffff);

	nv_wr32(dev, 0x409c24, 0x000f0000);
	nv_wr32(dev, 0x404000, 0xc0000000);
	nv_wr32(dev, 0x404600, 0xc0000000);
	nv_wr32(dev, 0x408030, 0xc0000000);
	nv_wr32(dev, 0x40601c, 0xc0000000);
	nv_wr32(dev, 0x404490, 0xc0000000);
	nv_wr32(dev, 0x406018, 0xc0000000);
	nv_wr32(dev, 0x405840, 0xc0000000);
	nv_wr32(dev, 0x405844, 0x00ffffff);
	nv_mask(dev, 0x419cc0, 0x00000008, 0x00000008);
	nv_mask(dev, 0x419eb4, 0x00001000, 0x00001000);

	for (gpc = 0; gpc < graph->gpc_nr; gpc++) {
		nv_wr32(dev, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		for (tpc = 0; tpc < graph->tpc_nr[gpc]; tpc++) {
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x508), 0xffffffff);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x50c), 0xffffffff);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x224), 0xc0000000);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x48c), 0xc0000000);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x084), 0xc0000000);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x644), 0x001ffffe);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x64c), 0x0000000f);
		}
		nv_wr32(dev, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nv_wr32(dev, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}

	for (rop = 0; rop < graph->rop_nr; rop++) {
		nv_wr32(dev, ROP_UNIT(rop, 0x144), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x070), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(dev, ROP_UNIT(rop, 0x208), 0xffffffff);
	}

	nv_wr32(dev, 0x400108, 0xffffffff); /* NVC0_PGRAPH_TRAP */
	nv_wr32(dev, 0x400138, 0xffffffff); /* NVC0_PGRAPH_TRAP_EN */
	nv_wr32(dev, 0x400118, 0xffffffff);
	nv_wr32(dev, 0x400130, 0xffffffff);
	nv_wr32(dev, 0x40011c, 0xffffffff);
	nv_wr32(dev, 0x400134, 0xffffffff);

	nv_wr32(dev, 0x400054, 0x34ce3464);
	
	ret = nvc0_graph_init_ctxctl(graph);
	if (ret) {
		NV_ERROR(dev, "PGRAPH: init_ctxctl failed!\n");
		goto fail_init_ctxctl;
	}
	
	nouveau_irq_register(dev, 12, nvc0_graph_irq_handler);
	
	return 0;
	

fail_init_ctxctl:
fail_ctor:
	NV_ERROR(dev, "PGRAPH: engine initialization failed!\n");
	
	return ret;
}

#include "fuc/hubnvc0.fuc.h"

struct nvc0_graph_ucode
nvc0_graph_fecs_ucode = {
	.code.data = nvc0_grhub_code,
	.code.size = sizeof(nvc0_grhub_code),
	.data.data = nvc0_grhub_data,
	.data.size = sizeof(nvc0_grhub_data),
};

#include "fuc/gpcnvc0.fuc.h"

struct nvc0_graph_ucode
nvc0_graph_gpccs_ucode = {
	.code.data = nvc0_grgpc_code,
	.code.size = sizeof(nvc0_grgpc_code),
	.data.data = nvc0_grgpc_data,
	.data.size = sizeof(nvc0_grgpc_data),
};
