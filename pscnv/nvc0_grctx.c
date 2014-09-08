/*
 * Copyright 2010 Red Hat Inc.
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

#include "nvc0_grctx.h"
#include "nvc0_graph.h"
#include "pscnv_vm.h"

#include "nvc0_grctx_reg_lists.inc"

/*******************************************************************************
 * PGRAPH context implementation
 ******************************************************************************/

static void
nvc0_grctx_generate_mods(struct nvc0_graph_engine *graph, struct nvc0_grctx *info)
{
	struct drm_device *dev = graph->base.dev;
	
	int gpc, tpc;
	u32 offset;
	
	/* the mmio_data and mmio_list macros fill the mmio_data respective
	 * mmio_list arrays of graph via moving pointers in grctx.
	 *
	 * On allocation of a regular channel, nvc0_graph_chan_alloc will
	 * copy these list conents into the newly allocated channel */

	mmio_data(0x002000, 0x0100, NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS);
	mmio_data(0x008000, 0x0100, NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS);
	mmio_data(0x060000, 0x1000, NV_MEM_ACCESS_RW);

	mmio_list(0x408004, 0x00000000,  8, 0);
	mmio_list(0x408008, 0x80000018,  0, 0);
	mmio_list(0x40800c, 0x00000000,  8, 1);
	mmio_list(0x408010, 0x80000000,  0, 0);
	mmio_list(0x418810, 0x80000000, 12, 2);
	mmio_list(0x419848, 0x10000000, 12, 2);
	mmio_list(0x419004, 0x00000000,  8, 1);
	mmio_list(0x419008, 0x00000000,  0, 0);
	mmio_list(0x418808, 0x00000000,  8, 0);
	mmio_list(0x41880c, 0x80000018,  0, 0);

	mmio_list(0x405830, 0x02180000, 0, 0);

	for (gpc = 0, offset = 0; gpc < graph->gpc_nr; gpc++) {
		for (tpc = 0; tpc < graph->tpc_nr[gpc]; tpc++) {
			u32 addr = TPC_UNIT(gpc, tpc, 0x0520);
			mmio_list(addr, 0x02180000 | offset, 0, 0);
			offset += 0x0324;
		}
	}
}

static void
nvc0_grctx_generate_tpcid(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	int gpc, tpc, id;

	for (tpc = 0, id = 0; tpc < 4; tpc++) {
		for (gpc = 0; gpc < graph->gpc_nr; gpc++) {
			if (tpc < graph->tpc_nr[gpc]) {
				nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x698), id);
				nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x4e8), id);
				nv_wr32(dev, GPC_UNIT(gpc, 0x0c10 + tpc * 4), id);
				nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x088), id);
				id++;
			}

			nv_wr32(dev, GPC_UNIT(gpc, 0x0c08), graph->tpc_nr[gpc]);
			nv_wr32(dev, GPC_UNIT(gpc, 0x0c8c), graph->tpc_nr[gpc]);
		}
	}
}

static void
nvc0_grctx_generate_r406028(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 tmp[GPC_MAX / 8] = {}, i = 0;
	for (i = 0; i < graph->gpc_nr; i++)
		tmp[i / 8] |= graph->tpc_nr[i] << ((i % 8) * 4);
	for (i = 0; i < 4; i++) {
		nv_wr32(dev, 0x406028 + (i * 4), tmp[i]);
		nv_wr32(dev, 0x405870 + (i * 4), tmp[i]);
	}
}

static void
nvc0_grctx_generate_r4060a8(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	u8  tpcnr[GPC_MAX], data[TPC_MAX];
	int gpc, tpc, i;

	memcpy(tpcnr, graph->tpc_nr, sizeof(graph->tpc_nr));
	memset(data, 0x1f, sizeof(data));

	gpc = -1;
	for (tpc = 0; tpc < graph->tpc_total; tpc++) {
		do {
			gpc = (gpc + 1) % graph->gpc_nr;
		} while (!tpcnr[gpc]);
		tpcnr[gpc]--;
		data[tpc] = gpc;
	}

	for (i = 0; i < 4; i++)
		nv_wr32(dev, 0x4060a8 + (i * 4), ((u32 *)data)[i]);
}

static void
nvc0_grctx_generate_r418bb8(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	u32 data[6] = {}, data2[2] = {};
	u8  tpcnr[GPC_MAX];
	u8  shift, ntpcv;
	int gpc, tpc, i;

	/* calculate first set of magics */
	memcpy(tpcnr, graph->tpc_nr, sizeof(graph->tpc_nr));

	gpc = -1;
	for (tpc = 0; tpc < graph->tpc_total; tpc++) {
		do {
			gpc = (gpc + 1) % graph->gpc_nr;
		} while (!tpcnr[gpc]);
		tpcnr[gpc]--;

		data[tpc / 6] |= gpc << ((tpc % 6) * 5);
	}

	for (; tpc < 32; tpc++)
		data[tpc / 6] |= 7 << ((tpc % 6) * 5);

	/* and the second... */
	shift = 0;
	ntpcv = graph->tpc_total;
	while (!(ntpcv & (1 << 4))) {
		ntpcv <<= 1;
		shift++;
	}

	data2[0]  = (ntpcv << 16);
	data2[0] |= (shift << 21);
	data2[0] |= (((1 << (0 + 5)) % ntpcv) << 24);
	for (i = 1; i < 7; i++)
		data2[1] |= ((1 << (i + 5)) % ntpcv) << ((i - 1) * 5);

	/* GPC_BROADCAST */
	nv_wr32(dev, 0x418bb8, (graph->tpc_total << 8) |
				 graph->magic_not_rop_nr);
	for (i = 0; i < 6; i++)
		nv_wr32(dev, 0x418b08 + (i * 4), data[i]);

	/* GPC_BROADCAST.TP_BROADCAST */
	nv_wr32(dev, 0x419bd0, (graph->tpc_total << 8) |
				 graph->magic_not_rop_nr | data2[0]);
	nv_wr32(dev, 0x419be4, data2[1]);
	for (i = 0; i < 6; i++)
		nv_wr32(dev, 0x419b00 + (i * 4), data[i]);

	/* UNK78xx */
	nv_wr32(dev, 0x4078bc, (graph->tpc_total << 8) |
				 graph->magic_not_rop_nr);
	for (i = 0; i < 6; i++)
		nv_wr32(dev, 0x40780c + (i * 4), data[i]);
}

static void
nvc0_grctx_generate_r406800(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	
	u64 tpc_mask = 0, tpc_set = 0;
	u8  tpcnr[GPC_MAX];
	int gpc, tpc;
	int i, a, b;

	memcpy(tpcnr, graph->tpc_nr, sizeof(graph->tpc_nr));
	for (gpc = 0; gpc < graph->gpc_nr; gpc++)
		tpc_mask |= ((1ULL << graph->tpc_nr[gpc]) - 1) << (gpc * 8);

	for (i = 0, gpc = -1, b = -1; i < 32; i++) {
		a = (i * (graph->tpc_total - 1)) / 32;
		if (a != b) {
			b = a;
			do {
				gpc = (gpc + 1) % graph->gpc_nr;
			} while (!tpcnr[gpc]);
			tpc = graph->tpc_nr[gpc] - tpcnr[gpc]--;

			tpc_set |= 1ULL << ((gpc * 8) + tpc);
		}

		nv_wr32(dev, 0x406800 + (i * 0x20), lower_32_bits(tpc_set));
		nv_wr32(dev, 0x406c00 + (i * 0x20), lower_32_bits(tpc_set ^ tpc_mask));
		if (graph->gpc_nr > 4) {
			nv_wr32(dev, 0x406804 + (i * 0x20), upper_32_bits(tpc_set));
			nv_wr32(dev, 0x406c04 + (i * 0x20), upper_32_bits(tpc_set ^ tpc_mask));
		}
	}
}

static void
nvc0_grctx_generate_main(struct nvc0_graph_engine *graph, struct nvc0_grctx *info)
{
	struct drm_device *dev = graph->base.dev;
	
	nv_mask(dev, 0x000260, 0x00000001, 0x00000000);

	nvc0_graph_mmio(graph, nvc0_grctx_pack_hub);
	nvc0_graph_mmio(graph, nvc0_grctx_pack_gpc);
	nvc0_graph_mmio(graph, nvc0_grctx_pack_zcull);
	nvc0_graph_mmio(graph, nvc0_grctx_pack_tpc);
	/* ppc seems not to exists
	 * nvc0_graph_mmio(graph, oclass->ppc); */

	nv_wr32(dev, 0x404154, 0x00000000);

	nvc0_grctx_generate_mods(graph, info);

	nvc0_grctx_generate_tpcid(graph);
	nvc0_grctx_generate_r406028(graph);
	nvc0_grctx_generate_r4060a8(graph);
	nvc0_grctx_generate_r418bb8(graph);
	nvc0_grctx_generate_r406800(graph);

	nvc0_graph_icmd(graph, nvc0_grctx_pack_icmd);
	nv_wr32(dev, 0x404154, 0x00000400);
	nvc0_graph_mthd(graph, nvc0_grctx_pack_mthd);
	nv_mask(dev, 0x000260, 0x00000001, 0x00000001);
}

/* before we can use a channel, we have to prefill its chan_bo with a lot
 * of inital values that are written to it by the GPU after setting up lots
 * of registers.
 *
 * We do this work only once in a 'template channel'. This channel is setup the
 * hard way  below, bypassing all the pscnv_vspace and pscnv_chan stuff.
 *
 * It's only use is getting the initial values, they'll be saved to graph-data
 * and copyied to all subsequently created channels in nvc0_graph_chan_alloc().
 *
 * Note that this is a major difference to how the original pscnv works: it
 * 'abused' the first ordinary channel for the creation of the initvals and
 * I have the suspicion that this left the channel in an inconsistent state */
int
nvc0_grctx_generate(struct nvc0_graph_engine *graph)
{
	struct drm_device *dev = graph->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct pscnv_bo *chan;
	
	/* not a pointer, struct allocated on stack for passing down parameters
	 * to the genarate functions */
	struct nvc0_grctx info;
	
	int ret, i;
	
	NV_INFO(dev, "PGRAPH: generating default grctx\n");

	/* allocate memory to for a "channel", which we'll use to generate
	 * the default context values
	 *
	 * multiple objects are stored in this BO:
	 * [0x00000] chan_bo (effictivly just the PD and grctx pointer)
	 * [0x01000] PD where PD[0] -> PT
	 * [0x02000-0x42000] PT
	 * [0x80000-0x80000+grctx_size] grctx (context values will apear here)
	 */
	chan = pscnv_mem_alloc(dev, 0x80000 + graph->grctx_size,
		PSCNV_GEM_CONTIG | PSCNV_ZEROFILL | PSCNV_MAP_KERNEL, 0,
		0xcc0000aa, NULL);
	
	if (!chan) {
		NV_ERROR(dev, "nvc0_grctx_generate: failed to allocate template"
			      " channel memory");
		return -ENOMEM;
	}

	/* PGD pointer */
	nv_wv32(chan, 0x0200, lower_32_bits(chan->start + 0x1000));
	nv_wv32(chan, 0x0204, upper_32_bits(chan->start + 0x1000));
	nv_wv32(chan, 0x0208, 0xffffffff);
	nv_wv32(chan, 0x020c, 0x000000ff);

	/* PGT[0] pointer */
	nv_wv32(chan, 0x1000, 0x00000000);
	nv_wv32(chan, 0x1004, 0x00000001 | (chan->start + 0x2000) >> 8);

	/* identity-map the whole "channel" into its own vm */
	for (i = 0; i < chan->size / 4096; i++) {
		u64 pte = ((chan->start + (i * 4096)) >> 8) | 1;
		nv_wv32(chan, 0x2000 + (i * 8), lower_32_bits(pte));
		nv_wv32(chan, 0x2004 + (i * 8), upper_32_bits(pte));
	}

	/* context pointer (virt) */
	nv_wv32(chan, 0x0210, 0x00080004);
	nv_wv32(chan, 0x0214, 0x00000000);

	dev_priv->vm->bar_flush(dev);

	/* poor man's tlb flush */
	nv_wr32(dev, 0x100cb8, (chan->start + 0x1000) >> 8);
	nv_wr32(dev, 0x100cbc, 0x80000001);
	nv_wait(dev, 0x100c80, 0x00008000, 0x00008000);

	/* setup default state for mmio list construction */
	info.graph = graph;
	info.data = graph->mmio_data;
	info.mmio = graph->mmio_list;
	info.addr = 0x2000 + (i * 8); /* end of PT */
	info.buffer_nr = 0;

	/* make channel current */
	nv_wr32(dev, 0x409840, 0x80000000);
	nv_wr32(dev, 0x409500, 0x80000000 | chan->start >> 12);
	nv_wr32(dev, 0x409504, 0x00000001);
	if (!nv_wait(dev, 0x409800, 0x80000000, 0x80000000))
		NV_ERROR(dev, "HUB_SET_CHAN timeout\n");

	/* setup lots of registers and prepare graph->mmio_data, graph->mmio_list */
	nvc0_grctx_generate_main(graph, &info);
	
	/* trigger a context unload by unsetting the "next channel valid" bit
	 * and faking a context switch interrupt
	 */
	nv_mask(dev, 0x409b04, 0x80000000, 0x00000000);
	nv_wr32(dev, 0x409000, 0x00000100);
	if (!nv_wait(dev, 0x409b00, 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "grctx template channel unload timeout\n");
		//ret = -EBUSY;
		//goto done;
	}
	
	/* now all the context values should be available. Save a copy to
	 * graph->data */

	graph->data = kmalloc(graph->grctx_size, GFP_KERNEL);
	if (graph->data) {
		for (i = 0; i < graph->grctx_size; i += 4)
			graph->data[i / 4] = nv_rv32(chan, 0x80000 + i);
		ret = 0;
	} else {
		ret = -ENOMEM;
	}

done:
	pscnv_mem_free(chan);
	return ret;
}

