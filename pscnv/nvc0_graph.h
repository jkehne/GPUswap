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

#ifndef __NVC0_GRAPH_H__
#define __NVC0_GRAPH_H__

#define NVC0_GPC_COUNT_MAX 32

struct nvc0_graph_engine {
	struct pscnv_engine base;
	spinlock_t lock;
	uint32_t grctx_size;
	uint32_t *grctx_initvals;
	int ropc_count;
	int gpc_count;
	int tp_count;
	int gpc_tp_count[NVC0_GPC_COUNT_MAX];
	int gpc_cx_count[NVC0_GPC_COUNT_MAX];
	struct pscnv_bo *obj188b4;
	struct pscnv_bo *obj188b8;
	struct pscnv_bo *obj08004;
	struct pscnv_bo *obj0800c;
	struct pscnv_bo *obj19848;
};

extern void nvc0_grctx_construct(struct drm_device *dev,
				 struct nvc0_graph_engine *graph,
				 struct pscnv_chan *chan);

#define nvc0_graph(x) container_of(x, struct nvc0_graph_engine, base)

/* for unknown pgraph CTXCTL regs. */
#define NVC0_PGRAPH_GPC_REG(gpc, r) ((0x500000 + (gpc) * 0x8000) + (r))
#define NVC0_PGRAPH_TP_REG(gpc, tp, r) \
	((0x504000 + (gpc) * 0x8000 + (tp) * 0x800) + (r))

/* for unknown ROPC regs*/
#define NVC0_PGRAPH_ROPC_REG(i, r) ((0x410000 + (i) * 0x400) + (r))

#endif
