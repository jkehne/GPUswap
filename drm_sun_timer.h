/* BEGIN CSTYLED */
#ifndef __DRM_TIMER_H__
#define __DRM_TIMER_H__

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ksynch.h>
#include "drm_linux_list.h"

#define del_timer_sync del_timer

struct timer_list {
	struct list_head *head;
	void (*func)(void *);
	void *arg;
	clock_t expires;
	timeout_id_t timer_id;
	kmutex_t lock;
};

inline void
init_timer(struct timer_list *timer)
{
	mutex_init(&timer->lock, NULL, MUTEX_DRIVER, NULL);
}

inline void
destroy_timer(struct timer_list *timer)
{
	mutex_destroy(&timer->lock);
}

inline void
setup_timer(struct timer_list *timer, void (*func)(void *), void *arg)
{
	timer->func = func;
	timer->arg = arg;
}

inline void
mod_timer(struct timer_list *timer, clock_t expires)
{
	mutex_enter(&timer->lock);
	untimeout(timer->timer_id);
	timer->expires = expires;
	timer->timer_id = timeout(timer->func, timer->arg, timer->expires);
	mutex_exit(&timer->lock);
}

inline void
del_timer(struct timer_list *timer)
{
	untimeout(timer->timer_id);
}

#endif
