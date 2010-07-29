#ifndef __PSCNV_ENGINE_H__
#define __PSCNV_ENGINE_H__

struct pscnv_chan;
struct pscnv_vspace;

struct pscnv_vm_engine {
	void (*takedown) (struct drm_device *dev);
	int (*do_vspace_new) (struct pscnv_vspace *vs);
	void (*do_vspace_free) (struct pscnv_vspace *vs);
	int (*do_map) (struct pscnv_vspace *vs, struct pscnv_bo *bo, uint64_t offset);
	int (*do_unmap) (struct pscnv_vspace *vs, uint64_t offset, uint64_t length);
	int (*map_user) (struct pscnv_bo *);
	int (*map_kernel) (struct pscnv_bo *);
	void (*bar_flush) (struct drm_device *dev);
};

struct pscnv_engine {
	struct drm_device *dev;
	int irq;
	uint32_t *oclasses;
	void (*takedown) (struct pscnv_engine *eng);
	void (*irq_handler) (struct pscnv_engine *eng);
	int (*tlb_flush) (struct pscnv_engine *eng, struct pscnv_vspace *vs);
	int (*chan_alloc) (struct pscnv_engine *eng, struct pscnv_chan *ch);
	void (*chan_free) (struct pscnv_engine *eng, struct pscnv_chan *ch);
	int (*chan_obj_new) (struct pscnv_engine *eng, struct pscnv_chan *ch, uint32_t handle, uint32_t oclass, uint32_t flags);
	void (*chan_kill) (struct pscnv_engine *eng, struct pscnv_chan *ch);
};

int nv50_vm_init(struct drm_device *dev);
int nv50_fifo_init(struct drm_device *dev);
int nv50_graph_init(struct drm_device *dev);

#define PSCNV_ENGINE_FIFO	0
#define PSCNV_ENGINE_GRAPH	1

#define PSCNV_ENGINES_NUM	16

int pscnv_ioctl_obj_eng_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv);

#endif
