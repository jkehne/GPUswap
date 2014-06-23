#ifndef PSCNV_SWAPPING_H
#define PSCNV_SWAPPING_H

#include "nouveau_drv.h"
#include "pscnv_mem.h"

struct pscnv_swapping_option {
	struct pscnv_bo *bo;
};

struct pscnv_swapping_option_list {
	struct pscnv_swapping_option **options;
	size_t size;
};

/* called once on driver load */
int
pscnv_swapping_init(struct drm_device *dev);

static inline int
pscnv_swapping_option_list_empty(struct pscnv_swapping_option_list *list)
{
	return list->size == 0;
}

/* initialize a list of pscnv_swapping_options */
static inline void
pscnv_swapping_option_list_init(struct pscnv_swapping_option_list *list)
{
	list->options = NULL;
	list->size = 0;
}

/* free a list of pscnv_swapping_options */
static inline void
pscnv_swapping_option_list_free(struct pscnv_swapping_option_list *list)
{
	WARN_ON(!pscnv_swapping_option_list_empty(list));
	kfree(list->options);
	pscnv_swapping_option_list_init(list);
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
 * @param me the client that needs the memory
 *
 * @returns: actual bytes of vram that will be free'd. In many cases this will
 *           be more than requested */
int
pscnv_swapping_reduce_vram(struct drm_device *dev, struct pscnv_client *me, uint64_t req, uint64_t *will_free);

/*
 * decide weather pscnv_swapping_reduce_vram needs to be run to satisfy request */
int
pscnv_swapping_required(struct drm_device *dev);


#endif /* end of include guard: PSCNV_SWAPPING_H */
