#ifndef PSCNV_CLIENT_H
#define PSCNV_CLIENT_H

#include "nouveau_drv.h"
#include "pscnv_swapping.h"

struct pscnv_client_timetrack {
	struct list_head list;
	const char *type;
	s64 start;
	s64 duration;
};

/* main structure for all the client related code, one instance per
   driver instance */
struct pscnv_clients {
	struct drm_device *dev;
	
	/* list of all clients */
	struct list_head list;
	
	/* lock on all clients */
	struct mutex lock;
	
	/* thread that pauses channels and performs the empty fifo work */
	struct task_struct *pause_thread;
	
	/* up on every call to do on empty_fifo and down on every operation */
	struct semaphore need_pause;
};

/* instance per pid */
struct pscnv_client {
	struct drm_device *dev;
	
	pid_t pid;
	
	struct kref ref;
	
	atomic64_t vram_usage;
	atomic64_t vram_swapped;
	atomic64_t vram_demand;
	
	/* list the client is in, see pscnv_clients.list */
	struct list_head clients;
	
	/* list of all channels held by this client */
	struct list_head channels;
	
	/* list of memory areas that meight be taken away from this client */
	struct pscnv_chunk_list swapping_options;
	
	/* list of memory areas that have been taken away from this client */
	struct pscnv_chunk_list already_swapped;
	
	/* list of work to do, next time that this client has an empty fifo */
	struct list_head on_empty_fifo;
	
	/* list of times that have been tracked for this client */
	struct list_head time_trackings;
};

typedef void (*client_workfunc_t)(void *data, struct pscnv_client *cl);

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
pscnv_client_search_pid(struct drm_device *dev, pid_t pid);

/* called every time some application opens a drm device */
int
pscnv_client_open(struct drm_device *dev, struct drm_file *file_priv);

/* called after an application closes a drm device. This is postclose,
   so all vspaces and channels should have already been free'd by
   nouveau_preclose() */
void
pscnv_client_postclose(struct drm_device *dev, struct drm_file *file_priv);

void
pscnv_client_do_on_empty_fifo_unlocked(struct pscnv_client *cl, client_workfunc_t func, void *data);

void
pscnv_client_run_empty_fifo_work(struct pscnv_client *cl);

/* we caught some process, because it trap'd into the driver. Lets use this
   oportunity and run_empty_fifo_work */
void
pscnv_client_wait_for_empty_fifo(struct pscnv_client *cl);



#endif /* end of include guard: PSCNV_CLIENT_H */
