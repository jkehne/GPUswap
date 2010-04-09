/* BEGIN CSTYLED */

#ifndef __DRM_SUN_WORKQUEUE_H__
#define __DRM_SUN_WORKQUEUE_H__

#include <sys/sunddi.h>
#include <sys/types.h>

typedef void (* taskq_func_t)(void *);

#define INIT_WORK(work, func) \
	init_work((work), ((taskq_func_t)(func)))

struct work_struct {
	void (*func) (void *);
};

struct workqueue_struct {
	ddi_taskq_t *taskq;
	char *name;
};

inline int queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	return (ddi_taskq_dispatch(wq->taskq, work->func, work, DDI_SLEEP));
}

inline void init_work(struct work_struct *work, void (*func)(void *))
{
	work->func = func;
}

inline struct workqueue_struct *create_workqueue(dev_info_t *dip, char *name)
{
	struct workqueue_struct *wq;

	wq = kmem_zalloc(sizeof (struct workqueue_struct), KM_SLEEP);
	wq->taskq = ddi_taskq_create(dip, name, 1, TASKQ_DEFAULTPRI, 0);
	if (wq->taskq == NULL)
		goto fail;
	wq->name = name;

	return wq;

fail :
	kmem_free(wq, sizeof (struct workqueue_struct));
	return (NULL);
}

inline void destroy_workqueue(struct workqueue_struct *wq)
{
	if (wq) {
		ddi_taskq_destroy(wq->taskq);
		kmem_free(wq, sizeof (struct workqueue_struct));
	}
}

#endif
