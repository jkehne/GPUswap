#ifndef PSCNV_CLIENT_H
#define PSCNV_CLIENT_H

#include "nouveau_drv.h"
#include "pscnv_swapping.h"

/* main structure for all the client related code, one instance per
   driver instance */
struct pscnv_clients {
	struct drm_device *dev;
	
	/* list of all clients */
	struct list_head list;
	
	/* lock on all clients */
	struct mutex lock;
};

/* instance per pid */
struct pscnv_client {
	struct drm_device *dev;
	
	pid_t pid;
	
	struct kref ref;
	
	uint64_t vram_usage;
	uint64_t vram_swapped;
	
	/* list the client is in, see pscnv_clients.list */
	struct list_head clients;
	
	/* list of memory areas that meight be taken away from this client */
	struct list_head possible_swap;
	
	/* list of work to do, next time that this client has an empty fifo */
	struct list_head on_empty_fifo;
};

/* setup the clients structure */
int
pscnv_clients_init(struct drm_device *dev);

/* create a new client and add it to the clients list */
struct pscnv_client*
pscnv_client_new(struct drm_device *dev, pid_t pid);

/* remove the client from the clients list and free memory */
extern void
pscnv_client_ref_free(struct kref *ref);

/* increase the refcount on some client */
static inline void
pscnv_client_ref(struct pscnv_client *cl) {
	kref_get(&cl->ref);
}

/* decrease the refcount on some client */
static inline void
pscnv_client_unref(struct pscnv_client *cl) {
	kref_put(&cl->ref, pscnv_client_ref_free);
}

/* get the client instance for the current process, or NULL */
struct pscnv_client*
pscnv_client_get_current(struct drm_device *dev);

/* called every time some application opens a drm device */
int
pscnv_client_open(struct drm_device *dev, struct drm_file *file_priv);

/* called after an application closes a drm device. This is postclose,
   so all vspaces and channels should have already been free'd by
   nouveau_preclose() */
void
pscnv_client_postclose(struct drm_device *dev, struct drm_file *file_priv);

#endif /* end of include guard: PSCNV_CLIENT_H */
