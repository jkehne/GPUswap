#include "pscnv_client.h"

struct pscnv_client_work {
	struct list_head entry;
	client_workfunc_t func;
	void *data;
};

/* pscnv_client_work structures are short lived and small, we use the
   slab allocator here */
static struct kmem_cache *client_work_cache = NULL;

static void
pscnv_client_work_ctor(void *data)
{
	struct pscnv_client_work *work = data;
	
	INIT_LIST_HEAD(&work->entry);
}

int
pscnv_clients_init(struct drm_device *dev)
{
	struct pscnv_clients *clients;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	NV_INFO(dev, "Clients: Initializing...\n");
	
	if (dev_priv->clients) {
		NV_INFO(dev, "Clients: already initialized!\n");
		return -EINVAL;
	}
	
	clients = kzalloc(sizeof(struct pscnv_clients), GFP_KERNEL);
	if (!clients) {
		NV_INFO(dev, "Clients: out of memory\n");
		return -ENOMEM;
	}
	
	clients->dev = dev;
	INIT_LIST_HEAD(&clients->list);
	mutex_init(&clients->lock);
	
	if (!client_work_cache) {
		client_work_cache = kmem_cache_create("pscnv_client_work",
			sizeof(struct pscnv_client_work), 0 /* offset */, 0, /* flags */
			pscnv_client_work_ctor);
	}
	
	if (!client_work_cache) {
		NV_INFO(dev, "Clients: failed to init client_work cache\n");
		kfree(clients);
		return -ENOMEM;
	}
	
	dev_priv->clients = clients;
	return 0;
}

static struct pscnv_client*
pscnv_client_search_pid_unlocked(struct drm_device *dev, pid_t pid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cur;
	
	BUG_ON(!dev_priv->clients);
	BUG_ON(!mutex_is_locked(&dev_priv->clients->lock));
	
	list_for_each_entry(cur, &dev_priv->clients->list, clients) {
		if (cur->pid == pid) {
			return cur;
		}
	}
	
	return NULL;
}

/* get the client instance for the current process, or NULL */
struct pscnv_client*
pscnv_client_search_pid(struct drm_device *dev, pid_t pid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl;
	
	mutex_lock(&dev_priv->clients->lock);
	cl = pscnv_client_search_pid_unlocked(dev, pid);
	mutex_unlock(&dev_priv->clients->lock);
	
	return cl;
}

static struct pscnv_client*
pscnv_client_new_unlocked(struct drm_device *dev, pid_t pid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *new;
	
	BUG_ON(!dev_priv->clients);
	BUG_ON(!mutex_is_locked(&dev_priv->clients->lock));
	
	if (pscnv_client_search_pid_unlocked(dev, pid) != NULL) {
		NV_ERROR(dev, "pscnv_client_new: client with pid %d already "
			      " in list\n", pid);
		return NULL;
	}
	
	new = kzalloc(sizeof(struct pscnv_client), GFP_KERNEL);
	if (!new) {
		NV_ERROR(dev, "pscnv_client_new: out of memory\n");
		return NULL;
	}
	
	new->dev = dev;
	new->pid = pid;
	kref_init(&new->ref);
	INIT_LIST_HEAD(&new->clients);
	INIT_LIST_HEAD(&new->on_empty_fifo);
	pscnv_swapping_option_list_init(&new->swapping_options);
	pscnv_swapping_option_list_init(&new->already_swapped);
	
	list_add_tail(&new->clients, &dev_priv->clients->list);
	
	BUG_ON(!pscnv_client_search_pid_unlocked(dev, pid));
	
	return new;
}

/* create a new client and add it to the clients list */
struct pscnv_client*
pscnv_client_new(struct drm_device *dev, pid_t pid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *new;
	
	mutex_lock(&dev_priv->clients->lock);
	new = pscnv_client_new_unlocked(dev, pid);
	mutex_unlock(&dev_priv->clients->lock);
	
	return new;
	
}

static void
pscnv_client_free_unlocked(struct pscnv_client *cl)
{
	struct drm_device *dev = cl->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	BUG_ON(!dev_priv->clients);
	BUG_ON(!mutex_is_locked(&dev_priv->clients->lock));
	
	/* remove from clients->list */
	list_del(&cl->clients);
	
	pscnv_swapping_option_list_free(&cl->swapping_options);
	pscnv_swapping_option_list_free(&cl->already_swapped);
	
	kfree(cl);
}
	

/* remove the client from the clients list and free memory */
void
pscnv_client_ref_free(struct kref *ref)
{
	struct pscnv_client *cl = container_of(ref, struct pscnv_client, ref);
	struct drm_device *dev = cl->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	mutex_lock(&dev_priv->clients->lock);
	pscnv_client_free_unlocked(cl);
	mutex_unlock(&dev_priv->clients->lock);
}

int
pscnv_client_open(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl;
	const pid_t pid = file_priv->pid;
	
	mutex_lock(&dev_priv->clients->lock);
	
	cl = pscnv_client_search_pid_unlocked(dev, pid);
	if (cl) {
		pscnv_client_ref(cl);
	} else {
		cl = pscnv_client_new_unlocked(dev, pid);
	}
	
	mutex_unlock(&dev_priv->clients->lock);
	
	if (!cl) {
		NV_ERROR(dev, "pscnv_client_open: failed for pid %d\n", pid);
		return -EINVAL;
	}
	
	return 0;
}

void
pscnv_client_postclose(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl;
	const pid_t pid = file_priv->pid;
	
	mutex_lock(&dev_priv->clients->lock);
	cl = pscnv_client_search_pid_unlocked(dev, pid);
	mutex_unlock(&dev_priv->clients->lock);
	
	if (!cl) {
		NV_ERROR(dev, "pscnv_client_postclose: pid %d not in list\n", pid);
		return;
	}

	/* must be called without lock! */
	pscnv_client_unref(cl);
}

void
pscnv_client_do_on_empty_fifo_unlocked(struct pscnv_client *cl, client_workfunc_t func, void *data)
{
	struct pscnv_client_work *work;
	
	work = kmem_cache_alloc(client_work_cache, GFP_KERNEL);
	BUG_ON(!work);
	
	work->func = func;
	work->data = data;
	
	list_add_tail(&work->entry, &cl->on_empty_fifo);
}

void
pscnv_client_run_empty_fifo_work(struct pscnv_client *cl)
{
	struct drm_device *dev = cl->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client_work *work;
	
	mutex_lock(&dev_priv->clients->lock);
	while (!list_empty(&cl->on_empty_fifo)) {
		/* the work in the queue meight take some time, so we always
		   pick just one element and then release the lock again.
		   this also allows others to queue additional work. */
		
		work = list_first_entry(&cl->on_empty_fifo, struct pscnv_client_work, entry);
		/* use del_init here, so that the work may be safely  reused */
		list_del_init(&work->entry);
		
		mutex_unlock(&dev_priv->clients->lock);
		
		/* release lock here, because this may take some time */
		work->func(work->data, cl);
		kmem_cache_free(client_work_cache, work);
		
		mutex_lock(&dev_priv->clients->lock);
	} /* ^^ list_empty checked again */
	
	mutex_unlock(&dev_priv->clients->lock);
}

/* we caught some process, because it trap'd into the driver. Lets use this
   oportunity and run_empty_fifo_work */
void
pscnv_client_wait_for_empty_fifo(struct pscnv_client *cl)
{
	if (!cl) {
		return;
	}
	
	pscnv_client_run_empty_fifo_work(cl);
}