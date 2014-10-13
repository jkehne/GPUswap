#ifndef PSCNV_SWAPPING_H
#define PSCNV_SWAPPING_H

#include "nouveau_drv.h"
#include "pscnv_mem.h"

#define PSCNV_INITIAL_CHUNK_LIST_SIZE 4UL

struct pscnv_chunk_list {
	struct pscnv_chunk **chunks;
	size_t size;
	size_t max;
};

/* called once on driver load */
int
pscnv_swapping_init(struct drm_device *dev);

static inline int
pscnv_chunk_list_empty(struct pscnv_chunk_list *list)
{
	return list->size == 0;
}

/* initialize a list of pscnv_chunks */
static inline void
pscnv_chunk_list_init(struct pscnv_chunk_list *list)
{
	list->chunks = NULL;
	list->size = 0;
	list->max = 0;
}

/* free a list of pscnv_chunks */
static inline void
pscnv_chunk_list_free(struct pscnv_chunk_list *list)
{
	WARN_ON(!pscnv_chunk_list_empty(list));
	kfree(list->chunks);
	pscnv_chunk_list_init(list);
}

/* tell the swapping system about a bo that meight be swapped out */
void
pscnv_swapping_add_bo(struct pscnv_bo *bo);

/* tell the system about a bo that shall not be swapped anymore */
void
pscnv_swapping_remove_bo(struct pscnv_bo *bo);

/*
 * ask the swapping system to swap out given amount of vram (in bytes)
 *
 * Be aware that this method meight block for a long time!
 *
 * @returns: actual bytes of vram that will be free'd. In many cases this will
 *           be more than requested */
int
pscnv_swapping_reduce_vram(struct drm_device *dev, uint64_t req, uint64_t *will_free);

/*
 * decide weather pscnv_swapping_reduce_vram needs to be run to satisfy request */
int
pscnv_swapping_required(struct drm_device *dev);

int
pscnv_swapping_increase_vram(struct drm_device *dev);

#endif /* end of include guard: PSCNV_SWAPPING_H */
