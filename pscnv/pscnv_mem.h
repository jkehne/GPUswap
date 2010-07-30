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

#ifndef __PSCNV_VRAM_H__
#define __PSCNV_VRAM_H__
#include "pscnv_drm.h"

#define PSCNV_MEM_PAGE_SIZE 0x1000

/* A VRAM object of any kind. */
struct pscnv_bo {
	struct drm_device *dev;
	/* size. Always a multiple of page size. */
	uint64_t size;
	/* misc flags, see below. */
	int flags;
	int tile_flags;
	/* cookie: free-form 32-bit number displayed in debug info. */
	uint32_t cookie;
	/* only used for debug */
	int serial;
	/* only for contig blocks. same info as start of first [and only]
	 * region, but more convenient to access */
	uint64_t start;
	/* the following used for GEM objects only */
	uint32_t user[8];
	struct drm_gem_object *gem;
	struct pscnv_vm_mapnode *map1;
	struct pscnv_vm_mapnode *map3;
	/* VRAM only: a linked list of VRAM regions making up this BO. */
	struct list_head regions;
	/* SYSRaM only: list of pages */
	struct page **pages;
	dma_addr_t *dmapages;
};

/* a contiguous VRAM region. They're linked into two lists: global list of
 * all regions and local list of regions within a single VO or free list.
 */
struct pscnv_vram_region {
	struct list_head global_list;
	struct list_head local_list;
	/* VRAM is split into so-called rblocks. Pages can be sane or LSR.
	 * you cannot have both sane and LSR pages in a single rblock.
	 * So an rblock can be in one of three states - UNTYPED when no
	 * pages are allocated in it, SANE when it contains sane pages,
	 * and LSR when it contains LSR pages. Region type is a combination
	 * of containing rblock state and free/used status.
	 */
	enum {
		PSCNV_VRAM_FREE_UNTYPED,
		PSCNV_VRAM_FREE_SANE,
		PSCNV_VRAM_FREE_LSR,
		PSCNV_VRAM_LAST_FREE = PSCNV_VRAM_FREE_LSR,
		PSCNV_VRAM_USED_SANE,
		PSCNV_VRAM_USED_LSR,
	} type;
	uint64_t start;
	uint64_t size;
	struct pscnv_bo *bo;
};

extern int pscnv_mem_init(struct drm_device *);
extern int pscnv_mem_takedown(struct drm_device *);
extern struct pscnv_bo *pscnv_mem_alloc(struct drm_device *,
		uint64_t size, int flags, int tile_flags, uint32_t cookie);
extern int pscnv_mem_free(struct pscnv_bo *);

extern int pscnv_vram_init(struct drm_device *);
extern int pscnv_vram_takedown(struct drm_device *);
extern int pscnv_vram_alloc(struct pscnv_bo *);
extern int pscnv_vram_free(struct pscnv_bo *);

extern int pscnv_sysram_alloc(struct pscnv_bo *);
extern int pscnv_sysram_free(struct pscnv_bo *);
extern int pscnv_sysram_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

#endif
