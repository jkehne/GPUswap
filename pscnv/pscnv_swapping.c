#include "pscnv_swapping.h"
#include "pscnv_dma.h"
#include "pscnv_vm.h"
#include "pscnv_client.h"

#include <linux/random.h>
#include <linux/completion.h>

#define SWAPPING_OPTION_MIN_SIZE (4 << 20) /* 4 MB */

/* if we are not able to reduce the required memory space by swapping memory
   of 16 clients (clients may be asked more than once) that will free up to 32
   swapping_options (each up to 16*4MB), than there is not much hope that we
   can ever satisfy the request */
#define PSCNV_SWAPPING_MAXOPS 16

#define PSCNV_SWAPPING_TIMEOUT 5*HZ

static struct kmem_cache *swapping_option_cache = NULL;

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
	NV_INFO(dev, "pscnv_swapping: initalizing....\n");
	
	if (!swapping_option_cache) {
		swapping_option_cache = kmem_cache_create("pscnv_swapping_options",
			sizeof(struct pscnv_swapping_option), 0 /* offset */,
			 0 /* flags */, NULL /* ctor */);
	}
	
	if (!swapping_option_cache) {
		NV_INFO(dev, "pscnv_swapping_init: failed to init swapping_option_cache\n");
		return -ENOMEM;
	}
	
	return 0;
}

static void
pscnv_swapping_option_list_add_unlocked(struct pscnv_swapping_option_list *list,
					struct pscnv_swapping_option *opt)
{
	struct drm_device *dev = opt->bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	if (!list->options) {
		list->options = kzalloc(sizeof(struct pscnv_swapping_option*) *
			dev_priv->vram_size / SWAPPING_OPTION_MIN_SIZE,
			GFP_KERNEL);
	}
	if (!list->options) {
		NV_INFO(dev, "pscnv_swapping_option_list_add: out of memory\n");
		return;
	}
	
	list->options[list->size] = opt;
	list->size++;
}

/* remove the n-th option from the list and return it */
static struct pscnv_swapping_option*
pscnv_swapping_option_list_take_unlocked(struct pscnv_swapping_option_list *list, size_t n)
{
	struct pscnv_swapping_option* ret;
	
	WARN_ON(n >= list->size);
	if (n >= list->size) {
		return NULL;
	}
	
	ret = list->options[n];
	
	/* we always keep the options tightly packed, for better random pick */
	list->size--;
	list->options[n] = list->options[list->size];
	
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

static struct pscnv_swapping_option*
pscnv_swapping_option_list_take_random_unlocked(struct pscnv_swapping_option_list *list)
{
	if (pscnv_swapping_option_list_empty(list)) {
		return NULL;
	}
	
	return pscnv_swapping_option_list_take_unlocked(list, pscnv_swapping_roll_dice(list->size));
}

static void
pscnv_swapping_option_free(struct pscnv_swapping_option *opt)
{
	kmem_cache_free(swapping_option_cache, opt);
}

static size_t
pscnv_swapping_option_list_remove_bo_unlocked(struct pscnv_swapping_option_list *list, struct pscnv_bo *bo)
{
	struct pscnv_swapping_option *opt;
	size_t count = 0;
	size_t i = 0;
	size_t size_before = list->size;
	
	while (i < list->size) {
		if (list->options[i]->bo == bo) {
			opt = pscnv_swapping_option_list_take_unlocked(list, i);
			pscnv_swapping_option_free(opt);
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
pscnv_swapping_list_search_bo_unlocked(struct pscnv_swapping_option_list *list, struct pscnv_bo *bo)
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

static void
pscnv_swapping_add_bo_internal(struct pscnv_bo *bo)
{
	struct drm_device *dev = bo->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl = bo->client;
	struct pscnv_swapping_option *opt;
	
	opt = kmem_cache_alloc(swapping_option_cache, GFP_KERNEL);
	opt->bo = bo;
	
	mutex_lock(&dev_priv->clients->lock);
	pscnv_swapping_option_list_add_unlocked(&cl->swapping_options, opt);
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
	
	if (bo->size < SWAPPING_OPTION_MIN_SIZE) {
		/* we ignore small bo's at the moment */
		return;
	}
	
	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			NV_INFO(dev, "pscnv_swapping_add_bo: adding %08x/%d to the swapping options of client %d\n", bo->cookie, bo->serial, bo->client->pid);
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
	
	pscnv_swapping_option_list_remove_bo_unlocked(&cl->swapping_options, bo);
	pscnv_swapping_option_list_remove_bo_unlocked(&cl->already_swapped, bo);
	
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
	
	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			/* NV_INFO(dev, "pscnv_swapping_remove_bo: removing %08x/%d to the swapping options of client %d\n", bo->cookie, bo->serial, bo->client->pid);*/
			pscnv_swapping_remove_bo_internal(bo);
	}
}

static int
pscnv_swapping_replace(struct pscnv_bo* vram, struct pscnv_bo* sysram)
{
	struct drm_device *dev = vram->dev;
	struct pscnv_mm_node *primary_node = vram->primary_node;
	struct pscnv_mm_node *swapped_node;
	struct pscnv_vspace *vs;
	uint64_t start, end;
	int res;
	
	if (!primary_node) {
		NV_INFO(dev, "pscnv_swapping_replace: BO %08x/%d has no "
			"primary node attached, nothing to do\n",
			vram->cookie, vram->serial);
		return -EINVAL;
	}
	
	BUG_ON(primary_node->bo != vram);
	vs = primary_node->vspace;
	
	start = primary_node->start;
	end = primary_node->start + primary_node->size;
	
	res = pscnv_vspace_unmap_node(primary_node);
	if (res) {
		NV_INFO(dev, "pscnv_swapping_replace: vid=%d BO %08x/%d: failed "
			"to unmap node in range %llx-%llx\n",
			vs->vid, vram->cookie, vram->serial, start, end);
		return res;
	}
	
	res = pscnv_vspace_map(vs, sysram, start, end, 0 /* back */, &swapped_node);
	if (res) {
		NV_INFO(dev, "pscnv_swapping_replace: vid=%d BO %08x/%d: failed "
			"to map in range %llx-%llx\n",
			vs->vid, sysram->cookie, sysram->serial, start, end);
		return res;
	}
	
	return 0;
}

static int
pscnv_vram_to_host(struct pscnv_bo* vram)
{
	struct drm_device *dev = vram->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_bo *sysram;
	struct pscnv_client *cl;
	uint32_t cookie = (0xa1de << 16) | (vram->cookie & 0xffff);
	int res;
	
	if (!dev_priv->dma) {
		pscnv_dma_init(dev);
	}
	
	if (!dev_priv->dma) {
		NV_ERROR(dev, "pscnv_vram_to_host: no DMA available\n");
		return -EINVAL;
	}
	
	sysram = pscnv_mem_alloc(dev, vram->size,
			    PSCNV_GEM_SYSRAM_NOSNOOP,
			    0 /* tile flags */,
			    cookie,
			    NULL /*client */);
	
	if (!sysram) {
		NV_INFO(dev, "pscnv_vram_to_host: failed to allocate SYSRAM!\n");
		return -ENOMEM;
	}
	
	res = pscnv_dma_bo_to_bo(sysram, vram);
	
	if (res) {
		NV_INFO(dev, "copy_to_host: failed to DMA- Transfer!\n");
		return res;
	}
	
	//pscnv_swapping_memdump(sysram);
	
	res = pscnv_swapping_replace(vram, sysram);
	if (res) {
		NV_INFO(dev, "copy_to_host: failed to replace mapping\n");
		return res;
	}
	
	/* free's the allocated vram, but does not remove the bo itself
	 * aslo updates vram_usage */
	pscnv_vram_free(vram);
	
	vram->backing_store = sysram;
	dev_priv->vram_swapped += vram->size;
	cl = vram->client;
	if (cl) {
		cl->vram_swap_pending -= vram->size;
		cl->vram_swapped += vram->size;
	} else {
		NV_ERROR(dev, "pscnv_vram_to_host: can not account client vram usage\n");
	}
	
	/* refcnt of sysram now belongs to the vram bo, it will unref it,
	   when it gets free'd itself */
	
	return 0;
}

static int
pscnv_swapping_wait_for_completions(struct drm_device *dev, const char *fname, struct completion *completions, int ops)
{
	int i;
	long res;
	
	for (i = 0; i < ops; i++) {
		res = wait_for_completion_interruptible_timeout(&completions[i],
			PSCNV_SWAPPING_TIMEOUT);
		
		if (res == -ERESTARTSYS) {
			NV_INFO(dev, "%s: interrupted while waiting for "
				     "completion %d\n", fname, i);
			return -EINTR;
		}
		if (res == 0) {
			NV_INFO(dev, "%s: timed out while waiting for completion %d\n",
					fname, i);
			return -EBUSY;
		}
	}
	
	/* memory leak in case something goes wrong - still better than risking
	   that someone crashes the system when he finally calls complete() */
	kfree(completions);
	
	return 0;
}

static void
pscnv_swapping_swap_out(void *data, struct pscnv_client *cl)
{
	struct drm_device *dev = cl->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_swapping_option *opt = data;
	
	pscnv_vram_to_host(opt->bo);
	
	mutex_lock(&dev_priv->clients->lock);
	pscnv_swapping_option_list_add_unlocked(&cl->already_swapped, opt);
	mutex_unlock(&dev_priv->clients->lock);
}

static void
pscnv_swapping_complete_wrapper(void *data, struct pscnv_client *cl)
{
	struct completion *c = data;
	complete(c);
}

static void
pscnv_swapping_reduce_vram_of_client_unlocked(struct pscnv_client *victim, uint64_t req, uint64_t *will_free, struct completion *completion)
{
	struct drm_device *dev = victim->dev;
	struct pscnv_swapping_option *opt;
	int ops = 0;
	
	while (*will_free < req && ops < PSCNV_SWAPPING_MAXOPS &&
		(opt = pscnv_swapping_option_list_take_random_unlocked(&victim->swapping_options))) {
		
		NV_INFO(dev, "Swapping: scheduling BO %08x/%d of client %d for swapping\n",
			opt->bo->cookie, opt->bo->serial, victim->pid);
			
		pscnv_client_do_on_empty_fifo_unlocked(victim,
			pscnv_swapping_swap_out, opt);
		
		victim->vram_swap_pending += opt->bo->size;
		*will_free += opt->bo->size;
		
		ops++;
	}
	
	if (ops > 0) {
		pscnv_client_do_on_empty_fifo_unlocked(victim,
			pscnv_swapping_complete_wrapper, completion);
	} else {
		/* nothing to do for this client, we just mark it as completed */
		complete(completion);
	}	
}

static struct pscnv_client*
pscnv_swapping_choose_victim_unlocked(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cur, *victim = NULL;
	uint64_t max = 0;
	
	list_for_each_entry(cur, &dev_priv->clients->list, clients) {
		if (cur->vram_usage - cur->vram_swap_pending > max &&
		    !pscnv_swapping_option_list_empty(&cur->swapping_options)) {
			victim = cur;
		}
	}
	
	return victim;
}

int
pscnv_swapping_reduce_vram(struct drm_device *dev, struct pscnv_client *me, uint64_t req, uint64_t *will_free)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *victim;
	struct completion *completions;
	int ops = 0;
	
	*will_free = 0;
	
	completions = kzalloc(sizeof(struct completion) * PSCNV_SWAPPING_MAXOPS,
			GFP_KERNEL);
			
	if (!completions) {
		NV_ERROR(dev, "pscnv_swapping_reduce_vram: out of memory\n");
		return -ENOMEM;
	}
	
	mutex_lock(&dev_priv->clients->lock);
	
	while (*will_free < req && ops < PSCNV_SWAPPING_MAXOPS &&
		(victim = pscnv_swapping_choose_victim_unlocked(dev))) {
		
		init_completion(&completions[ops]);
		
		pscnv_swapping_reduce_vram_of_client_unlocked(
			victim, req, will_free, &completions[ops]);
		
		ops++;
	}
	
	mutex_unlock(&dev_priv->clients->lock);
	
	if (*will_free < req) {
		NV_INFO(dev, "pscnv_swapping_reduce_vram: could not satisfy "
			     "request for %llx bytes, stuck with %llx after "
			     "%d ops\n", req, *will_free, ops);
		return -ENOMEM;
	}
	
	/* me meight be zero, but this function can handle that */
	pscnv_client_wait_for_empty_fifo(me); 
	
	return pscnv_swapping_wait_for_completions(dev, __func__, completions, ops);
}

int
pscnv_swapping_required(struct drm_device *dev, uint64_t req)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	if (dev_priv->vram_limit == 0) {
		/* swapping disabled */
		return false;
	} else {	
		return dev_priv->vram_usage + req > dev_priv->vram_limit;
	}
}

#if 0
int
pscnv_bo_copy_to_host(struct pscnv_bo* bo)
{
	struct drm_nouveau_private *dev_priv = bo->dev->dev_private;
	char rnd;
	int ret;
	static atomic_t sync = ATOMIC_INIT(1);
	
	if (dev_priv->vram_usage < 512 << 20) { /* 512 MB */
		/* still more than enough memory */
		return 0;
	}
	
	if (bo->backing_store) {
		/* already swapped out, nothing to do */
		return 0;
	}
	
	get_random_bytes(&rnd, 1);
	
	if (rnd > 70) {
		/* good luck, we choose another one */
		return 0;
	}
	
	if (!atomic_dec_and_test(&sync)) {
		atomic_inc(&sync);
		NV_INFO(bo->dev, "copy_to_host: collision\n");
		/* already someone else here */
		return 0;
	}
	
	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
		case PSCNV_GEM_VRAM_SMALL:
		case PSCNV_GEM_VRAM_LARGE:
			if (bo->pages) {
				NV_INFO(bo->dev, "copy_to_host: %x is VRAM, but has Sysram already allocated, wtf\n", bo->cookie);
				ret = -EINVAL;
			} else {
				ret =  pscnv_vram_to_host(bo);
			}
		case PSCNV_GEM_SYSRAM_SNOOP:
		case PSCNV_GEM_SYSRAM_NOSNOOP:
			NV_INFO(bo->dev, "copy_to_host: %x is already sysram!, doing nothing\n", bo->cookie);
			ret = -EINVAL;
		default:
			NV_INFO(bo->dev, "copy_to_host: %x has unknown storage type, doing nothing\n", bo->cookie);
			ret = -ENOSYS;
	}
	
	atomic_inc(&sync);
	
	return ret;
}
#endif