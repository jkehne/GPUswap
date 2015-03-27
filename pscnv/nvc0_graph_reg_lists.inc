/*******************************************************************************
 * PGRAPH register lists
 * (to be included in nvc0_graph.c)
 ******************************************************************************/

const struct nvc0_graph_init
nvc0_graph_init_main_0[] = {
	{ 0x400080,   1, 0x04, 0x003083c2 },
	{ 0x400088,   1, 0x04, 0x00006fe7 },
	{ 0x40008c,   1, 0x04, 0x00000000 },
	{ 0x400090,   1, 0x04, 0x00000030 },
	{ 0x40013c,   1, 0x04, 0x013901f7 },
	{ 0x400140,   1, 0x04, 0x00000100 },
	{ 0x400144,   1, 0x04, 0x00000000 },
	{ 0x400148,   1, 0x04, 0x00000110 },
	{ 0x400138,   1, 0x04, 0x00000000 },
	{ 0x400130,   2, 0x04, 0x00000000 },
	{ 0x400124,   1, 0x04, 0x00000002 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_fe_0[] = {
	{ 0x40415c,   1, 0x04, 0x00000000 },
	{ 0x404170,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_pri_0[] = {
	{ 0x404488,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_rstr2d_0[] = {
	{ 0x407808,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_pd_0[] = {
	{ 0x406024,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_ds_0[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_scc_0[] = {
	{ 0x40803c,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_prop_0[] = {
	{ 0x4184a0,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_gpc_unk_0[] = {
	{ 0x418604,   1, 0x04, 0x00000000 },
	{ 0x418680,   1, 0x04, 0x00000000 },
	{ 0x418714,   1, 0x04, 0x80000000 },
	{ 0x418384,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_setup_0[] = {
	{ 0x418814,   3, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_crstr_0[] = {
	{ 0x418b04,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_setup_1[] = {
	{ 0x4188c8,   1, 0x04, 0x80000000 },
	{ 0x4188cc,   1, 0x04, 0x00000000 },
	{ 0x4188d0,   1, 0x04, 0x00010000 },
	{ 0x4188d4,   1, 0x04, 0x00000001 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_zcull_0[] = {
	{ 0x418910,   1, 0x04, 0x00010001 },
	{ 0x418914,   1, 0x04, 0x00000301 },
	{ 0x418918,   1, 0x04, 0x00800000 },
	{ 0x418980,   1, 0x04, 0x77777770 },
	{ 0x418984,   3, 0x04, 0x77777777 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_gpm_0[] = {
	{ 0x418c04,   1, 0x04, 0x00000000 },
	{ 0x418c88,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_gpc_unk_1[] = {
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418e00,   1, 0x04, 0x00000050 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_gcc_0[] = {
	{ 0x41900c,   1, 0x04, 0x00000000 },
	{ 0x419018,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_tpccs_0[] = {
	{ 0x419d08,   2, 0x04, 0x00000000 },
	{ 0x419d10,   1, 0x04, 0x00000014 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_tex_0[] = {
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_pe_0[] = {
	{ 0x41980c,   3, 0x04, 0x00000000 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x41984c,   1, 0x04, 0x00005bc5 },
	{ 0x419850,   4, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_l1c_0[] = {
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419ca8,   1, 0x04, 0x80000000 },
	{ 0x419cb4,   1, 0x04, 0x00000000 },
	{ 0x419cb8,   1, 0x04, 0x00008bf4 },
	{ 0x419cbc,   1, 0x04, 0x28137606 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_wwdx_0[] = {
	{ 0x419bd4,   1, 0x04, 0x00800000 },
	{ 0x419bdc,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_tpccs_1[] = {
	{ 0x419d2c,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_mpc_0[] = {
	{ 0x419c0c,   1, 0x04, 0x00000000 },
	{}
};

static const struct nvc0_graph_init
nvc0_graph_init_sm_0[] = {
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00001100 },
	{ 0x419eac,   1, 0x04, 0x11100702 },
	{ 0x419eb0,   1, 0x04, 0x00000003 },
	{ 0x419eb4,   4, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x06060618 },
	{ 0x419ed0,   1, 0x04, 0x0eff0e38 },
	{ 0x419ed4,   1, 0x04, 0x011104f1 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419f2c,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_be_0[] = {
	{ 0x40880c,   1, 0x04, 0x00000000 },
	{ 0x408910,   9, 0x04, 0x00000000 },
	{ 0x408950,   1, 0x04, 0x00000000 },
	{ 0x408954,   1, 0x04, 0x0000ffff },
	{ 0x408984,   1, 0x04, 0x00000000 },
	{ 0x408988,   1, 0x04, 0x08040201 },
	{ 0x40898c,   1, 0x04, 0x80402010 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_fe_1[] = {
	{ 0x4040f0,   1, 0x04, 0x00000000 },
	{}
};

const struct nvc0_graph_init
nvc0_graph_init_pe_1[] = {
	{ 0x419880,   1, 0x04, 0x00000002 },
	{}
};

static const struct nvc0_graph_pack
nvc0_graph_pack_mmio[] = {
	{ nvc0_graph_init_main_0 },
	{ nvc0_graph_init_fe_0 },
	{ nvc0_graph_init_pri_0 },
	{ nvc0_graph_init_rstr2d_0 },
	{ nvc0_graph_init_pd_0 },
	{ nvc0_graph_init_ds_0 },
	{ nvc0_graph_init_scc_0 },
	{ nvc0_graph_init_prop_0 },
	{ nvc0_graph_init_gpc_unk_0 },
	{ nvc0_graph_init_setup_0 },
	{ nvc0_graph_init_crstr_0 },
	{ nvc0_graph_init_setup_1 },
	{ nvc0_graph_init_zcull_0 },
	{ nvc0_graph_init_gpm_0 },
	{ nvc0_graph_init_gpc_unk_1 },
	{ nvc0_graph_init_gcc_0 },
	{ nvc0_graph_init_tpccs_0 },
	{ nvc0_graph_init_tex_0 },
	{ nvc0_graph_init_pe_0 },
	{ nvc0_graph_init_l1c_0 },
	{ nvc0_graph_init_wwdx_0 },
	{ nvc0_graph_init_tpccs_1 },
	{ nvc0_graph_init_mpc_0 },
	{ nvc0_graph_init_sm_0 },
	{ nvc0_graph_init_be_0 },
	{ nvc0_graph_init_fe_1 },
	{ nvc0_graph_init_pe_1 },
	{}
};