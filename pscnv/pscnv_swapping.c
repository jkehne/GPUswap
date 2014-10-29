#include "pscnv_swapping.h"
#include "pscnv_dma.h"
#include "pscnv_vm.h"
#include "pscnv_client.h"
#include "pscnv_sysram.h"
#include "pscnv_vram.h"

#include <linux/random.h>
#include <linux/completion.h>

/* BOs smaller than this size are ignored */
#define PSCNV_SWAPPING_MIN_SIZE (4UL << 20) /* 4 MB */

/* reduce_vram will be called up to 3 times. Each time, it performs MAXOPS
 * operations and each operation itself is made up of OPS_PER_VICTIM
 * suboperations. Assuming 4MB Chunks, we can swap out 64 * 4 * 4 MB = 1 GB
 * of memory in a single reduce_vram call.
 * Maximum "unfairness" is 4 * 4MB = 16 MB */
#define PSCNV_SWAPPING_MAXOPS 64
#define PSCNV_SWAPPING_OPS_PER_VICTIM 4

#define PSCNV_SWAPPING_TIMEOUT 5*HZ

#if 0
static void
pscnv_swapping_memdump(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	uint32_t pagenum;
	uint32_t i;
	uint32_t *mem;
	
	if (!bo->pages || !bo->pages[0]) {
		NV_INFO(dev, "pscnv_swapping_memdump: can not memdump bo with "
			     "cookie=%x, it has no pages attached\n", bo->cookie);
		return;
	}
	
	// bo->size is a multiple of page size
	for (pagenum = 0; pagenum < bo->size >> PAGE_SHIFT; pagenum++) {
		NV_INFO(dev, "=== DUMP BO %08x/%d page %u\n", bo->cookie, bo->serial, pagenum);
		mem = kmap(bo->pages[pagenum]);
		for (i = 0; i < 256; i += 4) {
			NV_INFO(dev, "%08x %08x %08x %08x\n",
				mem[i], mem[i+1], mem[i+2], mem[i+3]);
		}
		kunmap(bo->pages[pagenum]);
        }
}
#endif

/* called once on driver load */
int
pscnv_swapping_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	if (pscnv_swapping_debug >= 1) {
		NV_INFO(dev, "pscnv_swapping: initalizing....\n");
	}
	
	atomic_set(&dev_priv->swaptask_serial, 0);

	return 0;
}


/*******************************************************************************
 * CHUNK_LIST
 ******************************************************************************/

static void
pscnv_chunk_list_add_unlocked(struct pscnv_chunk_list *list,
					struct pscnv_chunk *cnk)
{
	struct pscnv_bo *bo = cnk->bo;
	struct drm_device *dev = bo->dev;
	
	if (list->max <= list->size) {
		list->max = max(2*list->max, PSCNV_INITIAL_CHUNK_LIST_SIZE);
		
		list->chunks = krealloc(list->chunks,
			sizeof(struct pscnv_chunk*) * list->max, GFP_KERNEL);
		
		/* krealloc may return ZERO_SIZE_PTR=16 */
		if (ZERO_OR_NULL_PTR(list->chunks)) {
			NV_ERROR(dev, "pscnv_swapping_option_list_add: out of "
				"memory. chunks=%p\n", list->chunks);
			return;
		}
	}
	
	list->chunks[list->size++] = cnk;
}

/* remove the n-th option from the list and return it */
static struct pscnv_chunk*
pscnv_chunk_list_take_unlocked(struct pscnv_chunk_list *list, size_t n)
{
	struct pscnv_chunk* ret;
	
	WARN_ON(n >= list->size);
	if (n >= list->size) {
		return NULL;
	}
	
	ret = list->chunks[n];
	
	/* we always keep the options tightly packed, for better random pick */
	list->chunks[n] = list->chunks[--list->size];
	
	return ret;
}

/* get a random integer in [0,n-1] */
static inline uint32_t
pscnv_swapping_roll_dice(uint32_t n)
{
	uint32_t rnd;
	
	get_random_bytes(&rnd, 4);
	
	return rnd % n;
}

static struct pscnv_chunk*
pscnv_chunk_list_take_random_unlocked(struct pscnv_chunk_list *list)
{
	if (pscnv_chunk_list_empty(list)) {
		return NULL;
	}
	
	return pscnv_chunk_list_take_unlocked(list, pscnv_swapping_roll_dice(list->size));
}

static size_t
pscnv_chunk_list_remove_bo_unlocked(struct pscnv_chunk_list *list, struct pscnv_bo *bo)
{
	struct pscnv_chunk *cnk;
	size_t count = 0;
	size_t i = 0;
	size_t size_before = list->size;
	
	while (i < list->size) {
		if (list->chunks[i]->bo == bo) {
			cnk = pscnv_chunk_list_take_unlocked(list, i);
			count++;
			/* no i++ here, last element is moved here and gets
			 * checked at the next iteration */
		} else {
			i++;
		}
	}
	
	WARN_ON(size_before - count != list->size);
	
	return count;
}

#if 0
static int
pscnv_swapping_list_search_bo_unlocked(struct pscnv_chunk_list *list, struct pscnv_bo *bo)
{
	size_t i;
	
	for (i = 0; i < list->size; i++) {
		if (list->options[i]->bo == bo) {
			return true;
		}
	}
	
	return false;
}
#endif

/*******************************************************************************
 * SWAPTASK
 ******************************************************************************/

struct pscnv_swaptask *
pscnv_swaptask_new(struct pscnv_client *tgt)
{
	struct drm_device *dev = tgt->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_swaptask *st;
	int serial;
	
	st = kzalloc(sizeof(struct pscnv_swaptask), GFP_KERNEL);
	
	if (!st) {
		NV_ERROR(dev, "pscnv_swaptask_new: out of memory\n");
		return NULL;
	}
	
	serial = atomic_inc_return(&dev_priv->swaptask_serial);
	
	if (pscnv_swapping_debug >= 3) {
		NV_INFO(dev, "pscnv_swaptask_new: new swaptask %d for client %d\n",
			serial, tgt->pid);
	}
	
	INIT_LIST_HEAD(&st->list);
	pscnv_chunk_list_init(&st->selected);
	st->tgt = tgt;
	st->dev = dev;
	st->serial = serial;
	init_completion(&st->completion);
	
	return st;
}

void
pscnv_swaptask_free(struct pscnv_swaptask *st)
{
	struct drm_device *dev = st->dev;
	
	if (pscnv_swapping_debug >= 3) {
		NV_INFO(dev, "pscnv_swaptask_free: free swaptask %d for client %d\n",
			st->serial, st->tgt->pid);
	}
	
	pscnv_chunk_list_free(&st->selected);
	kfree(st);
}

static struct pscnv_swaptask *
pscnv_swaptask_get(struct list_head *swaptasks, struct pscnv_client *tgt)
{
	struct pscnv_swaptask *cur;
	struct pscnv_swaptask *new_st;
	
	list_for_each_entry(cur, swaptasks, list) {
		if (cur->tgt == tgt) {
			return cur;
		}
	}
	
	new_st = pscnv_swaptask_new(tgt);
	
	if (new_st) {
		list_add(&new_st->list, swaptasks);
	}
	
	return new_st;
}
	
/* return pointer to swaptask that received the chunk */
static struct pscnv_swaptask *
pscnv_swaptask_add_chunk_unlocked(struct list_head *swaptasks, struct pscnv_chunk *cnk)
{
	struct drm_device *dev = cnk->bo->dev;
	struct pscnv_client *tgt = cnk->bo->client;
	struct pscnv_swaptask *st;
	
	BUG_ON(!tgt);
	
	st = pscnv_swaptask_get(swaptasks, tgt);
	if (!st) {
		return NULL;
	}
	BUG_ON(st->tgt != tgt);
	
	if (pscnv_swapping_debug >= 3) {
		NV_INFO(dev, "pscnv_swaptask_add_chunk %08x/%d-%u for tgt %d to "
				"swaptask %d\n",
			cnk->bo->cookie, cnk->bo->serial, cnk->idx, tgt->pid,
			st->serial);
	}
	
	pscnv_chunk_list_add_unlocked(&st->selected, cnk);
	
	return st;
}

static int
pscnv_vram_to_host(struct pscnv_chunk* vram)
{
	struct pscnv_bo *bo = vram->bo;
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl;
	struct pscnv_chunk sysram; /* temporarily on stack */
	struct pscnv_mm_node *primary_node = bo->primary_node;
	struct pscnv_vspace *vs = NULL;
	int res;
	
	if (!dev_priv->dma) {
		pscnv_dma_init(dev);
	}
	
	if (!dev_priv->dma) {
		NV_ERROR(dev, "pscnv_vram_to_host: no DMA available\n");
		return -EINVAL;
	}
	
	if (pscnv_chunk_expect_alloc_type(vram, PSCNV_CHUNK_VRAM, "pscnv_vram_to_host")) {
		return -EINVAL;
	}
	
	if (!primary_node && pscnv_swapping_debug >= 1) {
		NV_INFO(dev, "pscnv_swapping_replace: BO %08x/%d-%u has no "
			"primary node attached, Strange.\n",
			bo->cookie, bo->serial, vram->idx);
	}
	
	if (primary_node)
		vs = primary_node->vspace;
	
	memset(&sysram, 0, sizeof(struct pscnv_chunk));
	sysram.bo = bo;
	sysram.idx = vram->idx;
	
	res = pscnv_sysram_alloc_chunk(&sysram);
	if (res) {
		NV_ERROR(dev, "pscnv_vram_to_host: pscnv_sysram_alloc_chunk "
			"failed on %08x/%d-%u\n", bo->cookie, bo->serial,
			sysram.idx);
		return res;
	}
	
	res = pscnv_dma_chunk_to_chunk(vram, &sysram, 0 /* flags */);
	
	if (res) {
		NV_INFO(dev, "copy_to_host: failed to DMA- Transfer!\n");
		goto fail_dma;
	}
	
	//pscnv_swapping_memdump(sysram);
	
	/* this overwrites existing PTE */
	if (vs) {
		dev_priv->vm->do_unmap(vs,
			primary_node->start + vram->idx * dev_priv->chunk_size,
			pscnv_chunk_size(vram));
		res = dev_priv->vm->do_map_chunk(vs, &sysram,
			primary_node->start + sysram.idx * dev_priv->chunk_size);
	
		if (res) {
			NV_INFO(dev, "copy_to_host: failed to replace mapping\n");
			goto fail_map_chunk;
		}
	}
	
	pscnv_vram_free_chunk(vram);
	
	/* vram chunk is unallocated now, replace its values with the sysram
	 * chunk */
	vram->alloc_type = sysram.alloc_type;
	vram->pages = sysram.pages;
	
	atomic64_add(pscnv_chunk_size(vram), &dev_priv->vram_swapped);
	cl = bo->client;
	if (cl) {
		atomic64_add(pscnv_chunk_size(vram), &cl->vram_swapped);
	} else {
		NV_ERROR(dev, "pscnv_vram_to_host: can not account client vram usage\n");
	}
	
	/* refcnt of sysram now belongs to the vram bo, it will unref it,
	   when it gets free'd itself */
	
	return 0;

fail_map_chunk:
	/* reset PTEs to old value, just to be safe */
	if (vs) {
		dev_priv->vm->do_unmap(vs,
			primary_node->start + sysram.idx * dev_priv->chunk_size,
			pscnv_chunk_size(&sysram));
		dev_priv->vm->do_map_chunk(vs, vram,
			primary_node->start + vram->idx * dev_priv->chunk_size);
	}

fail_dma:
	pscnv_sysram_free_chunk(&sysram);

	return res;
}

static void
pscnv_swapping_swap_out(void *data, struct pscnv_client *cl)
{
	struct drm_device *dev = cl->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_swaptask *st = data;
	struct pscnv_chunk *cnk;
	int ret;
	size_t i;
	
	BUG_ON(st->tgt != cl);
	
	if (pscnv_swapping_debug >= 2) {
		NV_INFO(dev, "pscnv_swapping_swap_out: [client %d] begin swaptask "
				"%d with %lu chunks\n", cl->pid, st->serial,
				st->selected.size);
	}
	
	for (i = 0; i < st->selected.size; i++) {
		cnk = st->selected.chunks[i];
		
		if (pscnv_chunk_expect_alloc_type(cnk, PSCNV_CHUNK_VRAM,
						"pscnv_swapping_swap_out")) {
			continue;
		}
		
		/* until now: one chunk after the other */
		ret = pscnv_vram_to_host(cnk);
		if (ret) {
			NV_ERROR(dev, "pscnv_swapping_swap_out: [client %d] vram_to_host"
				" failed for chunk %08x/%d-%u\n", cl->pid,
				cnk->bo->cookie, cnk->bo->serial, cnk->idx);
			continue;
		}
		
		/* vram_to_host increases swapped out counter */
		mutex_lock(&dev_priv->clients->lock);
		pscnv_chunk_list_add_unlocked(&cl->already_swapped, cnk);
		mutex_unlock(&dev_priv->clients->lock);
	}
	
	if (pscnv_swapping_debug >= 2) {
		NV_INFO(dev, "pscnv_swapping_swap_out: [client %d] end swaptask %d\n",
				cl->pid, st->serial);
	}
	
	complete(&st->completion);
}

static void
pscnv_swaptask_fire(struct list_head *swaptasks)
{	
	struct pscnv_swaptask *cur;
	
	list_for_each_entry(cur, swaptasks, list) {
		pscnv_client_do_on_empty_fifo_unlocked(cur->tgt,
			pscnv_swapping_swap_out, cur);
	}
}

static void
pscnv_swapping_add_bo_internal(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl = bo->client;
	unsigned int i;
	
	mutex_lock(&dev_priv->clients->lock);
	for (i = 0; i < bo->n_chunks; i++) {
		pscnv_chunk_list_add_unlocked(&cl->swapping_options, &bo->chunks[i]);
	}
	mutex_unlock(&dev_priv->clients->lock);
}

/* tell the swapping system about a bo that meight be swapped out */
void
pscnv_swapping_add_bo(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	
	if (!bo->client) {
		NV_INFO(dev, "pscnv_swapping_add_bo: %08x/%d has no client attached, doing nothing\n", bo->cookie, bo->serial);
		return;
	}
	
	if (!(bo->flags & PSCNV_GEM_USER)) {
		/* do not try to swap any system resources like pagetable */
		return;
	}
	
	if (bo->size < PSCNV_SWAPPING_MIN_SIZE) {
		/* we ignore small bo's at the moment */
		return;
	}
	
	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			if (pscnv_swapping_debug >= 1) {
				NV_INFO(dev, "pscnv_swapping_add_bo: adding %08x/%d to the swapping options of client %d\n", bo->cookie, bo->serial, bo->client->pid);
			}
			pscnv_swapping_add_bo_internal(bo);
			return;
		case PSCNV_GEM_SYSRAM_SNOOP:
		case PSCNV_GEM_SYSRAM_NOSNOOP:
			NV_INFO(dev, "pscnv_swapping_add_bo: %08x/%d is already sysram!, doing nothing\n", bo->cookie, bo->serial);
			return;
		default:
			NV_INFO(dev, "pscnv_swapping_add_bo: %08x/%d has unknown storage type, doing nothing\n", bo->cookie, bo->serial);
			return;
	}
}

static void
pscnv_swapping_remove_bo_internal(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl = bo->client;
	
	mutex_lock(&dev_priv->clients->lock);
	
	pscnv_chunk_list_remove_bo_unlocked(&cl->swapping_options, bo);
	pscnv_chunk_list_remove_bo_unlocked(&cl->already_swapped, bo);
	
	/*if (count == 0) {
		NV_INFO(dev, "pscnv_swapping_remove_bo: not a single swapping "
			     "option has been removed for BO %08x/%d on client %d\n",
			      bo->cookie, bo->serial, cl->pid);
	}
	
	if (pscnv_swapping_list_search_bo_unlocked(&cl->already_swapped, bo)) {
		NV_INFO(dev, "pscnv_swapping_remove_bo: BO %08x/%d on client %d "
			     " has already been swapped, oops\n",
			      bo->cookie, bo->serial, cl->pid);
	}*/
	
	mutex_unlock(&dev_priv->clients->lock);
}

void
pscnv_swapping_remove_bo(struct pscnv_bo *bo)
{
	if (!bo->client) {
		/* this bo has not ever been added to some options list */
		return;
	}
	
	/* NV_INFO(dev, "pscnv_swapping_remove_bo: removing %08x/%d to the swapping options of client %d\n", bo->cookie, bo->serial, bo->client->pid);*/
	pscnv_swapping_remove_bo_internal(bo);
}

#if 0
static int
pscnv_swapping_replace(struct pscnv_chunk* old, struct pscnv_chunk* new)
{
	struct pscnv_bo *bo_old = pscnv_chunk_bo(old);
	struct pscnv_bo *bo_new = pscnv_chunk_bo(new);
	struct drm_device *dev = bo_old->dev;
	struct pscnv_mm_node *primary_node = bo_old->primary_node;
	struct pscnv_mm_node *swapped_node;
	struct pscnv_vspace *vs;
	uint64_t start, end;
	int res;
	
	if (pscnv_swapping_debug >= 2) {
		NV_INFO(dev, "pscnv_swapping_replace:");
	}
	
	if (!primary_node) {
		NV_INFO(dev, "pscnv_swapping_replace: BO %08x/%d-%u has no "
			"primary node attached, nothing to do\n",
			old_bo->cookie, old_bo->serial, old->idx);
		return -EINVAL;
	}
	
	BUG_ON(primary_node->bo != old_bo);
	vs = primary_node->vspace;
	
	start = primary_node->start;
	end = primary_node->start + primary_node->size;
	
	mutex_lock(&vs->lock);
	res = dev_priv->vm->do_unmap(vs, node->start, node->size);
	
	
	mutex_unlock(&vs->lock);
	
	res = pscnv_vspace_unmap_node(primary_node);
	if (res) {
		NV_INFO(dev, "pscnv_swapping_replace: vid=%d BO %08x/%d: failed "
			"to unmap node in range %llx-%llx\n",
			vs->vid, vram->cookie, vram->serial, start, end);
		return res;
	}
	vram->primary_node = NULL;
	
	res = pscnv_vspace_map(vs, sysram, start, end, 0 /* back */, &swapped_node);
	if (res) {
		NV_INFO(dev, "pscnv_swapping_replace: vid=%d BO %08x/%d: failed "
			"to map in range %llx-%llx\n",
			vs->vid, sysram->cookie, sysram->serial, start, end);
		return res;
	}
	
	return 0;
}
#endif

#if 0
static int
pscnv_vram_from_host(struct pscnv_bo* vram)
{
	struct drm_device *dev = vram->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl;
	int res;
	
	if (!dev_priv->dma) {
		pscnv_dma_init(dev);
	}
	
	if (!dev_priv->dma) {
		NV_ERROR(dev, "pscnv_vram_from_host: no DMA available\n");
		return -EINVAL;
	}
	
	vram->flags &= ~PSCNV_GEM_USER; /* do not enter swapping logic */
	res = dev_priv->vram->alloc(vram);
	vram->flags |= PSCNV_GEM_USER;
	
	if (res) {
		NV_INFO(dev, "pscnv_vram_from_host: failed to allocate VRAM!\n");
		return -ENOMEM;
	}
	
	res = pscnv_dma_bo_to_bo(vram, vram->backing_store, 0 /* flags */);
	
	if (res) {
		NV_INFO(dev, "pscnv_vram_from_host: failed to DMA- Transfer!\n");
		return res;
	}
	
	//pscnv_swapping_memdump(sysram);
	
	res = pscnv_swapping_replace(vram->backing_store, vram);
	if (res) {
		NV_INFO(dev, "pscnv_vram_from_host: failed to replace mapping\n");
		return res;
	}
	
	pscnv_mem_free(vram->backing_store);
	
	vram->backing_store = NULL;
	atomic64_sub(vram->size, &dev_priv->vram_swapped);
	cl = vram->client;
	if (cl) {
		atomic64_sub(vram->size, &cl->vram_swapped);
	} else {
		NV_ERROR(dev, "pscnv_vram_to_host: can not account client vram usage\n");
	}
	
	/* refcnt of sysram now belongs to the vram bo, it will unref it,
	   when it gets free'd itself */
	
	return 0;
}
static int
pscnv_vram_from_sysram(struct pscnv_bo *sysram)
{
	struct drm_device *dev = sysram->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_bo *new_vram;
	int res;
	
	if (!dev_priv->dma) {
		pscnv_dma_init(dev);
	}
	
	if (!dev_priv->dma) {
		NV_ERROR(dev, "pscnv_vram_from_sysram: no DMA available\n");
		return -EINVAL;
	}
	
	new_vram = pscnv_mem_alloc(dev, sysram->size,
		    PSCNV_GEM_VRAM_LARGE,
		    0 /* tile flags */,
		    0x12345678,
		    sysram->client /*client */);
	new_vram->flags |= PSCNV_GEM_USER;
	
	if (!new_vram) {
		NV_INFO(dev, "pscnv_vram_from_sysram: failed to allocate VRAM!\n");
		return -ENOMEM;
	}
	
	res = pscnv_dma_bo_to_bo(new_vram, sysram, 0 /* flags */);
	
	if (res) {
		NV_INFO(dev, "pscnv_vram_from_sysram: failed to DMA- Transfer!\n");
		return res;
	}
	
	res = pscnv_swapping_replace(sysram, new_vram);
	if (res) {
		NV_INFO(dev, "pscnv_vram_from_sysram: failed to replace mapping\n");
		return res;
	}
	
	// TODO
	//pscnv_mem_free(sysram);
	
	atomic64_sub(new_vram->size, &dev_priv->vram_swapped);
	if (new_vram->client) {
		atomic64_sub(new_vram->size, &new_vram->client->vram_swapped);
	} else {
		NV_ERROR(dev, "pscnv_vram_to_host: can not account client vram usage\n");
	}
	
	return 0;
}
#endif

static int
pscnv_swaptask_wait_for_completions(struct drm_device *dev, const char *fname, struct list_head *swaptasks)
{
	struct pscnv_swaptask *cur, *tmp;
	int res;
	
	list_for_each_entry(cur, swaptasks, list) {
		res = wait_for_completion_interruptible_timeout(&cur->completion,
			PSCNV_SWAPPING_TIMEOUT);
		
		if (res == -ERESTARTSYS) {
			NV_INFO(dev, "%s: interrupted while waiting for "
				     "completion of swaptask %d on client %d\n",
				     fname, cur->serial, cur->tgt->pid);
			return -EINTR;
		}
		if (res == 0) {
			NV_INFO(dev, "%s: timed out while waiting for completion "
					"of swaptask %d on client %d\n",
					fname, cur->serial, cur->tgt->pid);
			return -EBUSY;
		}
	}
	
	/* memory leak in case something goes wrong - still better than risking
	   that someone crashes the system when he finally calls complete() */
	list_for_each_entry_safe(cur, tmp, swaptasks, list) {
		list_del(&cur->list);
		pscnv_swaptask_free(cur);
	}
	
	return 0;
}

#if 0
static void
pscnv_swapping_swap_in(void *data, struct pscnv_client *cl) { return; }
#endif

#if 0
static void
pscnv_swapping_swap_in(void *data, struct pscnv_client *cl)
{
	//struct drm_device *dev = cl->dev;
	//struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chunk *opt = data;
	
	switch (opt->bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
	case PSCNV_GEM_VRAM_SMALL:
	case PSCNV_GEM_VRAM_LARGE:
		pscnv_vram_from_host(opt->bo);
		break;
	case PSCNV_GEM_SYSRAM_SNOOP:
	case PSCNV_GEM_SYSRAM_NOSNOOP:
		pscnv_vram_from_sysram(opt->bo);
		break;
	}
	
	/* TODO
	mutex_lock(&dev_priv->clients->lock);
	pscnv_chunk_list_add_unlocked(&cl->chunks, opt);
	mutex_unlock(&dev_priv->clients->lock);*/
}
#endif

static int
pscnv_swapping_prepare_for_swap_out_unlocked(uint64_t *will_free, struct list_head *swaptasks, struct pscnv_chunk *cnk)
{
	struct pscnv_bo *bo = cnk->bo;
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_swaptask *st;
	struct pscnv_client *cl = cnk->bo->client;
	int ret = 0;
	
	size_t cnk_size = pscnv_chunk_size(cnk);
	char size_str[16];
	pscnv_mem_human_readable(size_str, cnk_size);
	
	if (!cl) {
		NV_ERROR(dev, "pscnv_swapping_prapare: chunk %08x/%d-%u has "
			"no client attached, can not swap out\n",
			cnk->bo->cookie, cnk->bo->serial, cnk->idx);
		return -EINVAL;
	}
	
	switch (cnk->alloc_type) {
	case PSCNV_CHUNK_UNALLOCATED:
		if (pscnv_swapping_debug >= 1) {
			NV_INFO(dev, "Swapping: allocating chunk %08x/%d-%u "
				"(%s) of client %d as SYSRAM\n",
				cnk->bo->cookie, cnk->bo->serial, cnk->idx,
				size_str, cl->pid);
		}
		ret = pscnv_sysram_alloc_chunk(cnk);
		if (ret) {
			return ret;
		}
		
		pscnv_chunk_list_add_unlocked(&cl->already_swapped, cnk);
			
		atomic64_add(cnk_size, &dev_priv->vram_swapped);
		atomic64_add(cnk_size, &cl->vram_swapped);
		atomic64_sub(cnk_size, &dev_priv->vram_demand);
		atomic64_sub(cnk_size, &cl->vram_demand);
		*will_free += cnk_size;
		
		return 0;
	
	case PSCNV_CHUNK_VRAM:
		st = pscnv_swaptask_add_chunk_unlocked(swaptasks, cnk);
		if (!st) {
			NV_ERROR(dev, "pscnv_swapping_prepare: failed to add "
					"chunk %08x/%d-%u\n", cnk->bo->cookie,
					cnk->bo->serial, cnk->idx);
		}
		if (st && pscnv_swapping_debug >= 1) {
			NV_INFO(dev, "Swapping: scheduling chunk %08x/%d-%u "
				" (%s) of client %d for swapping in task %d\n",
				cnk->bo->cookie, cnk->bo->serial, cnk->idx,
				size_str, cl->pid, st->serial);
		}
		
		atomic64_sub(cnk_size, &dev_priv->vram_demand);
		atomic64_sub(cnk_size, &cl->vram_demand);
		*will_free += cnk_size;
		
		return 0;
	
	default:
		NV_ERROR(dev, "pscnv_swapping_prepare_for_swap_out: "
			"chunk %08x/%d-%u is allocated as %s\n",
			cnk->bo->cookie, cnk->bo->serial, cnk->idx,
			pscnv_chunk_alloc_type_str(cnk->alloc_type));
		return -EINVAL;
	}
}

static void
pscnv_swapping_reduce_vram_of_client_unlocked(struct pscnv_client *victim, uint64_t req, uint64_t *will_free, struct list_head *swaptasks)
{
	struct drm_device *dev = victim->dev;
	int ret;
	
	struct pscnv_chunk *cnk;
	int ops = 0;
	
	while (*will_free < req && ops < PSCNV_SWAPPING_OPS_PER_VICTIM && 
	      (cnk = pscnv_chunk_list_take_random_unlocked(&victim->swapping_options))) {
		
		ret = pscnv_swapping_prepare_for_swap_out_unlocked(
			will_free, swaptasks, cnk);
		
		if (ret) {
			/* something has gone wrong, return chunk to swapping
			 * options */
			pscnv_chunk_list_add_unlocked(&victim->swapping_options, cnk);
			
			NV_ERROR(dev, "failed to prepare chunk %08x/%d-%u for "
				"swapping. ret = %d\n", cnk->bo->cookie,
				cnk->bo->serial, cnk->idx, ret);
		}
		
		ops++;
	}
}

static struct pscnv_client*
pscnv_swapping_choose_victim_unlocked(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cur, *victim = NULL;
	uint64_t cur_demand;
	uint64_t max = 0;
	
	list_for_each_entry(cur, &dev_priv->clients->list, clients) {
		cur_demand = atomic64_read(&cur->vram_demand);
		if (cur_demand > max &&
		    !pscnv_chunk_list_empty(&cur->swapping_options)) {
			victim = cur;
			max = cur_demand;
		}
	}
	
	return victim;
}

#if 0
static struct pscnv_client*
pscnv_swapping_choose_winner_unlocked(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cur, *winner = NULL;
	uint64_t cur_demand;
	uint64_t min = ((uint64_t)~0ULL);
	
	list_for_each_entry(cur, &dev_priv->clients->list, clients) {
		cur_demand = atomic64_read(&cur->vram_demand);
		if (cur_demand < min &&
		    !pscnv_chunk_list_empty(&cur->already_swapped)) {
			winner = cur;
			min = cur_demand;
		}
	}
	
	return winner;
}
#endif

int
pscnv_swapping_reduce_vram(struct drm_device *dev, uint64_t req, uint64_t *will_free)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *victim;
	int ops = 0;
	
	LIST_HEAD(swaptasks);
	
	*will_free = 0;
	
	mutex_lock(&dev_priv->clients->lock);
	
	while (*will_free < req && ops < PSCNV_SWAPPING_MAXOPS &&
		(victim = pscnv_swapping_choose_victim_unlocked(dev))) {

		pscnv_swapping_reduce_vram_of_client_unlocked(
			victim, req, will_free, &swaptasks);
		
		ops++;
	}
	
	mutex_unlock(&dev_priv->clients->lock);
	
	if (*will_free < req) {
		char req_str[16], will_free_str[16];
		pscnv_mem_human_readable(req_str, req);
		pscnv_mem_human_readable(will_free_str, *will_free);
		NV_INFO(dev, "pscnv_swapping_reduce_vram: could not satisfy "
			     "request for %s, stuck with %s after "
			     "%d ops\n", req_str, will_free_str, ops);
		/* no return here, this function may be called again */
	}
	
	pscnv_swaptask_fire(&swaptasks);
	
	return pscnv_swaptask_wait_for_completions(dev, __func__, &swaptasks);
}

int
pscnv_swapping_increase_vram(struct drm_device *dev) { return 0; }

#if 0
int
pscnv_swapping_increase_vram(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *winner;
	struct pscnv_chunk *opt;
	int ops = 0;
	
	uint64_t vram_demand; 
	uint64_t will_alloc = 0;
	
	mutex_lock(&dev_priv->clients->lock);
	
	while (ops < PSCNV_SWAPPING_MAXOPS &&
		(winner = pscnv_swapping_choose_winner_unlocked(dev))) {
		vram_demand = atomic64_read(&dev_priv->vram_demand);
		
		opt = pscnv_chunk_list_take_random_unlocked(&winner->already_swapped);
		
		if (opt->bo->size + will_alloc < dev_priv->vram_limit-vram_demand) {
			NV_INFO(dev, "Swapping: scheduling BO %08x/%d of client %d for swapIN\n",
				opt->bo->cookie, opt->bo->serial, winner->pid);
			
			pscnv_client_do_on_empty_fifo_unlocked(winner,
				pscnv_swapping_swap_in, opt);
		
			atomic64_add(opt->bo->size, &winner->vram_demand);
			atomic64_add(opt->bo->size, &dev_priv->vram_demand);
			will_alloc += opt->bo->size;
		}
		
		ops++;
	}
	
	mutex_unlock(&dev_priv->clients->lock);
	
	return 0;
}
#endif

int
pscnv_swapping_required(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	uint64_t limit = dev_priv->vram_limit;
	uint64_t demand = atomic64_read(&dev_priv->vram_demand);
	
	if (limit == 0) {
		/* swapping disabled */
		return false;
	} else {	
		return demand > limit;
	}
}
