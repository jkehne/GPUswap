#include "pscnv_client.h"

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
	INIT_LIST_HEAD(&new->possible_swap);
	INIT_LIST_HEAD(&new->on_empty_fifo);
	
	list_add(&new->clients, &dev_priv->clients->list);
	
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

/* get the client instance for the current process, or NULL */
struct pscnv_client*
pscnv_client_get_current(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_client *cl;
	
	mutex_lock(&dev_priv->clients->lock);
	cl = pscnv_client_search_pid_unlocked(dev, task_pid_nr(current));
	mutex_unlock(&dev_priv->clients->lock);
	
	return cl;
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