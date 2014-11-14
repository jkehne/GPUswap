#ifndef PSCNV_SWAPPING_H
#define PSCNV_SWAPPING_H

#include "nouveau_drv.h"
#include "pscnv_mem.h"

#define PSCNV_INITIAL_CHUNK_LIST_SIZE 4UL

struct pscnv_swapping {
	struct drm_device *dev;
	atomic_t swaptask_serial;
	struct delayed_work increase_vram_work;
	
	/* gets completed every time some swapping operation is completed */
	struct completion next_swap;
};

struct pscnv_chunk_list {
	struct pscnv_chunk **chunks;
	size_t size;
	size_t max;
};

/* a swaptask collects all chunks that the source client selected for swapping
 * within the target client. This datastructure is especially useful to perform
 * many chunk copies within a single DMA transfer. */
struct pscnv_swaptask {
	/* list of swaptasks with the same src but different tgt */
	struct list_head list;
	
	/* shortcut */
	struct drm_device *dev;
	
	/* aids debugging */
	int serial;
	
	/* list of pointers to chunks that are selected for swapping */
	struct pscnv_chunk_list selected;
	
	/* client that created this swaptask */
	struct pscnv_client *src;
	
	/* client that the chunks in this task belong to. This client will be
	 * paused */
	struct pscnv_client *tgt;
	
	/* completion that will be fired when all work in this swaptask has been
	 * completed */
	struct completion completion;
};

/* called once on driver load */
int
pscnv_swapping_init(struct drm_device *dev);

/* called once on driver shutdown */
void
pscnv_swapping_exit(struct drm_device *dev);

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
	kfree(list->chunks);
	pscnv_chunk_list_init(list);
}

/* tell the swapping system about a bo that meight be swapped out */
void
pscnv_swapping_add_bo(struct pscnv_bo *bo);

/* tell the system about a bo that shall not be swapped anymore */
int
pscnv_swapping_remove_bo(struct pscnv_bo *bo);

/*
 * ask the swapping system to swap out given amount of vram (in bytes)
 *
 * Be aware that this method meight block for a long time!
 *
 * @param me calling client, may be NULL
 *
 * @returns: actual bytes of vram that will be free'd. In many cases this will
 *           be more than requested */
int
pscnv_swapping_reduce_vram(struct drm_device *dev, struct pscnv_client *me);

/*
 * decide weather pscnv_swapping_reduce_vram needs to be run to satisfy request */
int
pscnv_swapping_required(struct pscnv_bo *bo);

int
pscnv_swapping_increase_vram(struct drm_device *dev);

/*
 * allocate a chunk as SYSRAM and also put it into the already_swapped list
 * of the client that owns it, if it is swappable */
int
pscnv_swapping_sysram_fallback(struct pscnv_chunk *cnk);

#endif /* end of include guard: PSCNV_SWAPPING_H */
