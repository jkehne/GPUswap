#ifndef __NVKM_GRCTX_NVC0_H__
#define __NVKM_GRCTX_NVC0_H__

/* memory type/access flags, do not match hardware values */
#define NV_MEM_ACCESS_RO  1
#define NV_MEM_ACCESS_WO  2
#define NV_MEM_ACCESS_RW (NV_MEM_ACCESS_RO | NV_MEM_ACCESS_WO)
#define NV_MEM_ACCESS_SYS 4

#include "nvc0_graph.h"

struct nvc0_grctx {
	struct nvc0_graph_engine *graph;
	struct nvc0_graph_data *data;
	struct nvc0_graph_mmio *mmio;
	int buffer_nr;
	u64 buffer[4];
	u64 addr;
};

#define mmio_data(s,a,p) do {                                              \
	info->buffer[info->buffer_nr] = round_up(info->addr, (a));             \
	info->addr = info->buffer[info->buffer_nr++] + (s);                    \
	info->data->size = (s);                                                \
	info->data->align = (a);                                               \
	info->data->access = (p);                                              \
	info->data++;                                                          \
} while(0)

#define mmio_list(r,d,s,b) do {                                            \
	info->mmio->addr = (r);                                                \
	info->mmio->data = (d);                                                \
	info->mmio->shift = (s);                                               \
	info->mmio->buffer = (b);                                              \
	info->mmio++;                                                          \
	nv_wr32(dev, (r), (d) | ((s) ? (info->buffer[(b)] >> (s)) : 0));       \
} while(0)

int  nvc0_grctx_generate(struct nvc0_graph_engine *);

/* context init value lists */

extern const struct nvc0_graph_pack nvc0_grctx_pack_icmd[];

extern const struct nvc0_graph_pack nvc0_grctx_pack_mthd[];
extern const struct nvc0_graph_init nvc0_grctx_init_902d_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_9039_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_90c0_0[];

extern const struct nvc0_graph_pack nvc0_grctx_pack_hub[];
extern const struct nvc0_graph_init nvc0_grctx_init_main_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_fe_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_pri_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_memfmt_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_rstr2d_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_scc_0[];

extern const struct nvc0_graph_pack nvc0_grctx_pack_gpc[];
extern const struct nvc0_graph_init nvc0_grctx_init_gpc_unk_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_prop_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvc0_grctx_init_zcull_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_crstr_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_gpm_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_gcc_0[];

extern const struct nvc0_graph_pack nvc0_grctx_pack_zcull[];

extern const struct nvc0_graph_pack nvc0_grctx_pack_tpc[];
extern const struct nvc0_graph_init nvc0_grctx_init_pe_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_wwdx_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_mpc_0[];
extern const struct nvc0_graph_init nvc0_grctx_init_tpccs_0[];

extern const struct nvc0_graph_init nvc4_grctx_init_tex_0[];
extern const struct nvc0_graph_init nvc4_grctx_init_l1c_0[];
extern const struct nvc0_graph_init nvc4_grctx_init_sm_0[];

extern const struct nvc0_graph_init nvc1_grctx_init_9097_0[];

extern const struct nvc0_graph_init nvc1_grctx_init_gpm_0[];

extern const struct nvc0_graph_init nvc1_grctx_init_pe_0[];
extern const struct nvc0_graph_init nvc1_grctx_init_wwdx_0[];
extern const struct nvc0_graph_init nvc1_grctx_init_tpccs_0[];

extern const struct nvc0_graph_init nvc8_grctx_init_9197_0[];
extern const struct nvc0_graph_init nvc8_grctx_init_9297_0[];

extern const struct nvc0_graph_pack nvd9_grctx_pack_icmd[];

extern const struct nvc0_graph_pack nvd9_grctx_pack_mthd[];

extern const struct nvc0_graph_init nvd9_grctx_init_fe_0[];
extern const struct nvc0_graph_init nvd9_grctx_init_be_0[];

extern const struct nvc0_graph_init nvd9_grctx_init_prop_0[];
extern const struct nvc0_graph_init nvd9_grctx_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvd9_grctx_init_crstr_0[];

extern const struct nvc0_graph_init nvd9_grctx_init_sm_0[];

extern const struct nvc0_graph_init nvd7_grctx_init_pe_0[];

extern const struct nvc0_graph_init nvd7_grctx_init_wwdx_0[];

extern const struct nvc0_graph_init nve4_grctx_init_memfmt_0[];
extern const struct nvc0_graph_init nve4_grctx_init_ds_0[];
extern const struct nvc0_graph_init nve4_grctx_init_scc_0[];

extern const struct nvc0_graph_init nve4_grctx_init_gpm_0[];

extern const struct nvc0_graph_init nve4_grctx_init_pes_0[];

extern const struct nvc0_graph_pack nvf0_grctx_pack_mthd[];

extern const struct nvc0_graph_init nvf0_grctx_init_pri_0[];
extern const struct nvc0_graph_init nvf0_grctx_init_cwd_0[];

extern const struct nvc0_graph_init nvf0_grctx_init_gpc_unk_2[];

extern const struct nvc0_graph_init nvf0_grctx_init_mpc_0[];
extern const struct nvc0_graph_init nvf0_grctx_init_l1c_0[];

extern const struct nvc0_graph_init nv108_grctx_init_rstr2d_0[];

extern const struct nvc0_graph_init nv108_grctx_init_prop_0[];
extern const struct nvc0_graph_init nv108_grctx_init_crstr_0[];


#endif
