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

#ifndef __NVC0_GRAPH_H__
#define __NVC0_GRAPH_H__

#include "nouveau_drv.h"
#include "pscnv_engine.h"

#define GPC_MAX 32
#define TPC_MAX (GPC_MAX * 8)

#define ROP_BCAST(r)      (0x408800 + (r))
#define ROP_UNIT(u, r)    (0x410000 + (u) * 0x400 + (r))
#define GPC_BCAST(r)      (0x418000 + (r))
#define GPC_UNIT(t, r)    (0x500000 + (t) * 0x8000 + (r))
#define PPC_UNIT(t, m, r) (0x503000 + (t) * 0x8000 + (m) * 0x200 + (r))
#define TPC_UNIT(t, m, r) (0x504000 + (t) * 0x8000 + (m) * 0x800 + (r))

#define NVC0_GRAPH(x) container_of(x, struct nvc0_graph_engine, base)

struct nvc0_graph_data {
	u32 size;
	u32 align;
	u32 access;
};

struct nvc0_graph_mmio {
	u32 addr;
	u32 data;
	u32 shift;
	u32 buffer;
};

struct nvc0_graph_fuc {
	u32 *data;
	u32  size;
};

struct nvc0_graph_engine {
	struct pscnv_engine base;

	struct nvc0_graph_fuc fuc409c;
	struct nvc0_graph_fuc fuc409d;
	struct nvc0_graph_fuc fuc41ac;
	struct nvc0_graph_fuc fuc41ad;

	u8 rop_nr;
	u8 gpc_nr;
	u8 tpc_nr[GPC_MAX];
	u8 tpc_total;

	struct pscnv_bo *unk4188b4;
	struct pscnv_bo *unk4188b8;

	struct nvc0_graph_data mmio_data[4];
	struct nvc0_graph_mmio mmio_list[4096/8];
	u32  grctx_size;
	u32 *data;

	u8 magic_not_rop_nr;
};

struct nvc0_graph_chan {
	struct pscnv_bo *grctx;
	uint64_t grctx_vm_base;
	
	struct pscnv_bo *mmio;
	uint64_t mmio_vm_base;
	int mmio_nr;
	
	struct {
		struct pscnv_bo *mem;
		uint64_t vm_base;
	} data[4];
};

/* in pscnv_engine.h:
	int  nvc0_graph_init(struct drm_device *dev); */

struct nvc0_graph_init {
	u32 addr;
	u8  count;
	u8  pitch;
	u32 data;
};

struct nvc0_graph_pack {
	const struct nvc0_graph_init *init;
	u32 type;
};

void
nvc0_graph_mmio(struct nvc0_graph_engine *graph, const struct nvc0_graph_pack *p);

void
nvc0_graph_icmd(struct nvc0_graph_engine *graph, const struct nvc0_graph_pack *p);

void
nvc0_graph_mthd(struct nvc0_graph_engine *graph, const struct nvc0_graph_pack *p);

#define pack_for_each_init(init, pack, head)                               \
	for (pack = head; pack && pack->init; pack++)                          \
		  for (init = pack->init; init && init->count; init++)

struct nvc0_graph_ucode {
	struct nvc0_graph_fuc code;
	struct nvc0_graph_fuc data;
};

extern struct nvc0_graph_ucode nvc0_graph_fecs_ucode;
extern struct nvc0_graph_ucode nvc0_graph_gpccs_ucode;

/* register init value lists */

extern const struct nvc0_graph_init nvc0_graph_init_main_0[];
extern const struct nvc0_graph_init nvc0_graph_init_fe_0[];
extern const struct nvc0_graph_init nvc0_graph_init_pri_0[];
extern const struct nvc0_graph_init nvc0_graph_init_rstr2d_0[];
extern const struct nvc0_graph_init nvc0_graph_init_pd_0[];
extern const struct nvc0_graph_init nvc0_graph_init_ds_0[];
extern const struct nvc0_graph_init nvc0_graph_init_scc_0[];
extern const struct nvc0_graph_init nvc0_graph_init_prop_0[];
extern const struct nvc0_graph_init nvc0_graph_init_gpc_unk_0[];
extern const struct nvc0_graph_init nvc0_graph_init_setup_0[];
extern const struct nvc0_graph_init nvc0_graph_init_crstr_0[];
extern const struct nvc0_graph_init nvc0_graph_init_setup_1[];
extern const struct nvc0_graph_init nvc0_graph_init_zcull_0[];
extern const struct nvc0_graph_init nvc0_graph_init_gpm_0[];
extern const struct nvc0_graph_init nvc0_graph_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvc0_graph_init_gcc_0[];
extern const struct nvc0_graph_init nvc0_graph_init_tpccs_0[];
extern const struct nvc0_graph_init nvc0_graph_init_tex_0[];
extern const struct nvc0_graph_init nvc0_graph_init_pe_0[];
extern const struct nvc0_graph_init nvc0_graph_init_l1c_0[];
extern const struct nvc0_graph_init nvc0_graph_init_wwdx_0[];
extern const struct nvc0_graph_init nvc0_graph_init_tpccs_1[];
extern const struct nvc0_graph_init nvc0_graph_init_mpc_0[];
extern const struct nvc0_graph_init nvc0_graph_init_be_0[];
extern const struct nvc0_graph_init nvc0_graph_init_fe_1[];
extern const struct nvc0_graph_init nvc0_graph_init_pe_1[];

extern const struct nvc0_graph_init nvc4_graph_init_ds_0[];
extern const struct nvc0_graph_init nvc4_graph_init_tex_0[];
extern const struct nvc0_graph_init nvc4_graph_init_sm_0[];

extern const struct nvc0_graph_init nvc1_graph_init_gpc_unk_0[];
extern const struct nvc0_graph_init nvc1_graph_init_setup_1[];

extern const struct nvc0_graph_init nvd9_graph_init_pd_0[];
extern const struct nvc0_graph_init nvd9_graph_init_ds_0[];
extern const struct nvc0_graph_init nvd9_graph_init_prop_0[];
extern const struct nvc0_graph_init nvd9_graph_init_gpm_0[];
extern const struct nvc0_graph_init nvd9_graph_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvd9_graph_init_tex_0[];
extern const struct nvc0_graph_init nvd9_graph_init_sm_0[];
extern const struct nvc0_graph_init nvd9_graph_init_fe_1[];

extern const struct nvc0_graph_init nvd7_graph_init_pes_0[];
extern const struct nvc0_graph_init nvd7_graph_init_wwdx_0[];
extern const struct nvc0_graph_init nvd7_graph_init_cbm_0[];

extern const struct nvc0_graph_init nve4_graph_init_main_0[];
extern const struct nvc0_graph_init nve4_graph_init_tpccs_0[];
extern const struct nvc0_graph_init nve4_graph_init_pe_0[];
extern const struct nvc0_graph_init nve4_graph_init_be_0[];

extern const struct nvc0_graph_init nvf0_graph_init_fe_0[];
extern const struct nvc0_graph_init nvf0_graph_init_sked_0[];
extern const struct nvc0_graph_init nvf0_graph_init_cwd_0[];
extern const struct nvc0_graph_init nvf0_graph_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvf0_graph_init_sm_0[];

extern const struct nvc0_graph_init nv108_graph_init_gpc_unk_0[];


#endif
