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
#include "pscnv_mm.h"

#define PSCNV_MEM_PAGE_SIZE 0x1000

struct pscnv_vspace;

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
	struct pscnv_mm_node *map1;
	struct pscnv_mm_node *map3;
	/* VRAM only: the first mm node */
	struct pscnv_mm_node *mmnode;
	/* SYSRaM only: list of pages */
	struct page **pages;
	dma_addr_t *dmapages;
	/* CHAN only, pointer to a channel (FreeBSD doesn't allow overriding mmap) */
	struct pscnv_chan *chan;
	/* number of references to this buffer object */
	struct kref ref;
	/* vm memory node (one continuos area) that will be replaced with
	 * system RAM. Currently the BO is simply removed from other vspaces */
	struct pscnv_mm_node *primary_node;
	/* bo that holds the content of this bo, if it gets swapped out */
	struct pscnv_bo *backing_store;
#ifndef __linux__
	/* freebsd: Allocate an array of fake pages we can populate if user-space mappable */
	vm_page_t fake_pages;
#endif
};
#define PSCNV_GEM_NOUSER	0x10

struct pscnv_vram_engine {
	void (*takedown) (struct drm_device *);
	int (*alloc) (struct pscnv_bo *);
	int (*free) (struct pscnv_bo *);
	int (*sysram_tiling_ok) (struct pscnv_bo *);
};

extern int pscnv_mem_init(struct drm_device *);
extern void pscnv_mem_takedown(struct drm_device *);
extern struct pscnv_bo *pscnv_mem_alloc(struct drm_device *,
		uint64_t size, int flags, int tile_flags, uint32_t cookie);

/*
 * convenience function. Allocates and maps to vm. GPU virtual address is
 * returned in *vm_base. tile flags are set zero */
extern struct pscnv_bo*	pscnv_mem_alloc_and_map(struct pscnv_vspace *vs, 
		uint64_t size, uint32_t flags, uint32_t cookie, uint64_t *vm_base);
		
/* calls pscnv_mem_free() */
extern void pscnv_bo_ref_free(struct kref *ref);

static inline void pscnv_bo_ref(struct pscnv_bo *bo) {
	kref_get(&bo->ref);
}

static inline void pscnv_bo_unref(struct pscnv_bo *bo) {
	kref_put(&bo->ref, pscnv_bo_ref_free);
}

extern int pscnv_mem_free(struct pscnv_bo *);

extern int pscnv_vram_free(struct pscnv_bo *bo);
extern void pscnv_vram_takedown(struct drm_device *dev);

extern int nv50_vram_init(struct drm_device *);
extern int nvc0_vram_init(struct drm_device *);

extern int pscnv_sysram_alloc(struct pscnv_bo *);
extern int pscnv_sysram_free(struct pscnv_bo *);
extern int pscnv_sysram_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

#endif
