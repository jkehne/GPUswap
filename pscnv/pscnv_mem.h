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

#ifndef __PSCNV_MEM_H__
#define __PSCNV_MEM_H__
#include "pscnv_drm.h"
#include "pscnv_mm.h"

#define PSCNV_MEM_PAGE_SIZE 0x1000

/* VRAM that is reserved for the driver. The lowest PSCNV_VRAM_RESERVED bytes
 * of VRAM will never be allocated to the userspace */
#define PSCNV_VRAM_RESERVED (12 << 20)

struct pscnv_vspace;
struct pscnv_client;

struct pscnv_page_and_dma {
	struct page *k; /* kernel page data */
	dma_addr_t dma;
};

/* chunk.alloc_type */
#define PSCNV_CHUNK_UNALLOCATED  0 /* no memory has been allocated for this chunk, yet */
#define PSCNV_CHUNK_VRAM         1 /* a regular chunk in VRAM */
#define PSCNV_CHUNK_SYSRAM       2 /* a chunk that is allocated in SYSRAM, as
                                    * userspace explicitly asked for */

/* chunk.flags */
#define PSCNV_CHUNK_SWAPPED      1 /* this chunk is involuntarily SYSRAM */

/** ALLCATION RULES:
 *
 * The information where a chunk is allocated of what allocation type and
 * weather it is swapped or not is scattered around 3 different values:
 * - flags in pscnv_bo
 * - alloc_type in pscnv_chunk
 * - swapping_option and already_swapped lists in pscnv_client
 *
 * following rules apply:
 * - the memtype in flags is set by userspace and indicates what it wants to
 *   have. It is never changed
 * - alloc_type says where the memory is actually allocated.
 * - SYSRAM is NOSNOOP unless (flags & MEMTYPE_MASK) == SYSRAM_SNOOP
 * - VRAM is allocated in LPT only if (flags & MEMTYPE_MASK) == VRAM_LARGE
 *
 * Swapped Memory can be detected by one of the following:
 * - flags indicates VRAM but alloc_type is SYSRAM
 * - the chunk is placed in the already_swapped list of its client */


struct pscnv_chunk {
	/* pointer to bo that this chunk belongs to.
	 * Normally the BO and its chunks are allocated together */
	struct pscnv_bo *bo;
	
	/* position in bo->chunks[] array */
	uint32_t idx; 
	
	/* currently either PSCNV_CHUNK_SWAPPED or 0 */
	uint16_t flags;
	
	/* one of PSCNV_CHUNK_UNALLOCATED, PSCNV_CHUNK_VRAM, ... */
	uint16_t alloc_type;
	
	union {
		/* PSCNV_CHUNK_VRAM only: first node of phyisical allocation
		 * of this chunk */
		struct pscnv_mm_node *vram_node;
		/* PSCNV_CHUNK_SYSRAM: pages and corresponding DMA addresses */
		struct pscnv_page_and_dma *pages;
	};
};

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

	/* CHAN only, pointer to a channel (FreeBSD doesn't allow overriding mmap) */
	struct pscnv_chan *chan;
	/* number of references to this buffer object */
	struct kref ref;
	/* vm memory node (one continuos area) that will be replaced with
	 * system RAM. Currently the BO is simply removed from other vspaces */
	struct pscnv_mm_node *primary_node;

	/* client who allocated this bo, if it was allocated by a user space process */
	struct pscnv_client *client;
	/* if this pointer is set, use this memory area to access the VRAM contents
	   of this bo (see nouveau_drv.h: nv_rv32, nv_wv32). This pointer should
	   be set, if the BO is mapped to BAR 1 */
	struct drm_local_map *drm_map;
	/* continous kernel virtual memory for a SYSRAM BO */
	void *vmap;
	
	/* page fault handler to call, if userspace has mapped this bo but no
	 * pte is set up for it */
	int (*vm_fault)(struct pscnv_bo *bo, struct vm_area_struct *vma, struct vm_fault *vmf);
	/* vma area that this BO is mapped at */
	struct vm_area_struct *vma;
	
	/* number of chunks for this bo */
	uint32_t n_chunks;
	
	/* array of chunks allocated for this bo. Sized as required */
	struct pscnv_chunk chunks[0];
};

#define PSCNV_GEM_NOUSER	0x10
#define PSCNV_ZEROFILL		0x20
#define PSCNV_MAP_KERNEL	0x40
#define PSCNV_MAP_USER		0x80
#define PSCNV_GEM_READONLY  0x100
#define PSCNV_GEM_FLAG3     0x200
#define PSCNV_GEM_USER      0x400  /* < swappable */
#define PSCNV_GEM_IB        0x800
#define PSCNV_GEM_VM_KERNEL 0x1000 /* < create continous mapping in kernel vm */

extern int pscnv_mem_init(struct drm_device *);
extern void pscnv_mem_takedown(struct drm_device *);
extern struct pscnv_bo *pscnv_mem_alloc(struct drm_device *,
		uint64_t size, int flags, int tile_flags, uint32_t cookie, struct pscnv_client *client);

/*
 * convenience function. Allocates and maps to vm. GPU virtual address is
 * returned in *vm_base. tile flags are set zero */
extern struct pscnv_bo*	pscnv_mem_alloc_and_map(struct pscnv_vspace *vs, 
		uint64_t size, uint32_t flags, uint32_t cookie, uint64_t *vm_base);

/*
 * convenience function. Map the buffer in BAR1 and ioremap it via drm_addmap */
extern int pscnv_bo_map_bar1(struct pscnv_bo* bo);

/*
 * fill the whole bo to val with 32bit write operations
 * You likely want to do a bar flush after this */
extern void pscnv_bo_memset(struct pscnv_bo* bo, uint32_t val);

extern void pscnv_chunk_memset(struct pscnv_chunk* cnk, uint32_t val);

/* calls pscnv_mem_free() */
extern void pscnv_bo_ref_free(struct kref *ref);

static inline void pscnv_bo_ref(struct pscnv_bo *bo) {
	kref_get(&bo->ref);
}

static inline void pscnv_bo_unref(struct pscnv_bo *bo) {
	kref_put(&bo->ref, pscnv_bo_ref_free);
}

extern int pscnv_mem_free(struct pscnv_bo *);

void
pscnv_chunk_free(struct pscnv_chunk *cnk);

extern int nv50_vram_init(struct drm_device *);
extern int nvc0_vram_init(struct drm_device *);

/* write an amount of bytes in human-readable form (bytes, kB, MB) */ 
extern void
pscnv_mem_human_readable(char *buf, uint64_t val);

/* get the size in bytes of some chunk */
uint64_t
pscnv_chunk_size(struct pscnv_chunk *cnk);

void
pscnv_chunk_warn_wrong_alloc_type(struct pscnv_chunk *cnk, uint32_t expected, const char *fname);

/* return 1 iff chunk does not have alloc_type `expected` and print error massage */
static inline int
pscnv_chunk_expect_alloc_type(struct pscnv_chunk *cnk, uint32_t expected, const char *fname)
{
	if (cnk->alloc_type != expected) {
		pscnv_chunk_warn_wrong_alloc_type(cnk, expected, fname);
		return 1;
	}
	return 0;
}

const char *
pscnv_chunk_alloc_type_str(uint32_t at);

const char *
pscnv_bo_memtype_str(uint32_t flags);

/* object access */

/* slowpath, called "instmem" by nouveau, same for nv50, nvc0.
   PRAMIN seems to be 1MB (nouveau) though, not just 16kB as used by pscnv.
   I guess pscnv does not map a large enough io memory region
*/

/* get the index ot the chunk that holds data at `offset` */
uint32_t
pscnv_chunk_at_offset(struct drm_device *dev, uint64_t offset);

uint32_t
nv_rv32(struct pscnv_bo *bo, unsigned offset);

void
nv_wv32(struct pscnv_bo *bo, unsigned offset, uint32_t val);

uint32_t
nv_rv32_pramin(struct drm_device *dev, uint64_t addr);

void
nv_wv32_pramin(struct drm_device *dev, uint64_t addr, uint32_t val);

uint64_t
pscnv_mem_vram_usage_effective(struct drm_device *dev);

uint64_t
pscnv_mem_vram_usage_effective_unlocked(struct drm_device *dev);

#endif
