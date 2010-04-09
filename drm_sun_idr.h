/* BEGIN CSTYLED */
#ifndef __DRM_IDR_H__
#define __DRM_IDR_H__

#include <sys/avl.h>

#define DRM_IDR_HASHNODE 1024

struct idr_used_id {
	struct avl_node link;
	uint32_t id;
	void *obj;
};

struct idr_free_id {
	struct idr_free_id *next;
	uint32_t id;
};

struct idr_free_id_range {
	struct idr_free_id_range *next;
	uint32_t start;
	uint32_t end;
	uint32_t min_unused_id;
	struct idr_free_id *free_ids;
};

struct idr {
	struct avl_tree used_ids;
	struct idr_free_id_range *free_id_ranges;
	kmutex_t lock;
};

extern void idr_init(struct idr *idrp);
extern int idr_get_new_above(struct idr *idrp, void *obj, int start, int *newid);
extern void* idr_find(struct idr *idrp, uint32_t id);
extern int idr_remove(struct idr *idrp, uint32_t id);
extern void* idr_replace(struct idr *idrp, void *obj, uint32_t id);
extern int idr_pre_get(struct idr *idrp, int flag);
extern int idr_for_each(struct idr *idrp, int (*fn)(int id, void *obj, void *data), void *data);
extern void idr_remove_all(struct idr *idrp);
extern void idr_destroy(struct idr* idrp);

#endif
