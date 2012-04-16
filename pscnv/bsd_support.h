// Note: Need to change /sys/conf/kmod.conf for FreeBSD
// set CSTD to gnu99 to enable support for nameless unions/structs

// Some headers used from /sys/ofed/include which all bear the following copyright:
/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/limits.h>
#include <sys/kdb.h>
#include "drmP.h"

#define PSCNV_KAPI_GETPARAM_BUS_TYPE

typedef DRM_SPINTYPE spinlock_t;

#ifndef cpu_to_le16
#define cpu_to_le16(x) htole16(x)
#define le16_to_cpu(x) le16toh(x)
#endif

#define spin_lock_init(lock) DRM_SPININIT(lock, #lock)
#define spin_lock_destroy(lock) DRM_SPINUNINIT(lock)
#define spin_lock(lock) DRM_SPINLOCK(lock)
#define spin_unlock(lock) DRM_SPINUNLOCK(lock)
#define spin_lock_irqsave(lock, flags) DRM_SPINLOCK_IRQSAVE(lock, flags)
#define spin_unlock_irqrestore(lock, flags) DRM_SPINUNLOCK_IRQRESTORE(lock, flags)

#define kfree(x) drm_free(x, 0, DRM_MEM_DRIVER)
#define kzalloc(x, y) drm_calloc(x, 1, DRM_MEM_DRIVER)
#define kcalloc(x, y, z) drm_calloc(x, y, DRM_MEM_DRIVER)
#define kmalloc(x, y) drm_alloc(x, DRM_MEM_DRIVER)

struct device_attribute {};
struct notifier_block {};
typedef struct pm_message_t { } pm_message_t;
struct fb_info;
struct fb_copyarea;
struct fb_fillrect;
struct fb_image;

struct vm_fault {};
struct vm_area_struct {};

#ifndef HZ
#define HZ hz
#endif

#define power_supply_is_system_supplied() 1

/* Dog slow version, but only called once
 * Linux kernel has a faster software fallback
 * but I prefer not to GPL this or even worry about it
 */
static inline u32 hweight32(u32 val)
{
	u32 i, ret = 0;
	for (i = 1; i; i <<= 1)
		if (val & i)
			++ret;
	return ret;
}

#define WARN(arg1, args...) do { \
		printf("%s:%d/%s " arg1, \
			__FILE__, __LINE__, __FUNCTION__, ##args); \
		kdb_backtrace(); \
	} while (0)
#define BUG() panic("BUG()\n");
#define WARN_ON(x,y,z...) do { if ((x)) WARN(y, ##z); } while (0)
#define BUG_ON(x) do { if ((x)) { panic(#x "triggered\n"); } } while (0)

#ifndef __must_check
#define __must_check
#endif

#ifndef _LINUX_DELAY_H_
#define	_LINUX_DELAY_H_

static inline void
linux_msleep(int ms)
{
	pause("lnxsleep", msecs_to_jiffies(ms));
}

#undef msleep
#define	msleep	linux_msleep
#define udelay(x) DELAY((x))
#define mdelay(x) DELAY((x) * 1000)

#endif	/* _LINUX_DELAY_H_ */

#ifndef	_LINUX_MUTEX_H_
#define	_LINUX_MUTEX_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/sx.h>

typedef struct mutex {
	struct sx sx;
} mutex_t;

#define	mutex_lock(_m)			sx_xlock(&(_m)->sx)
#define	mutex_lock_nested(_m, _s)	mutex_lock(_m)
#define	mutex_lock_interruptible(_m)	({ mutex_lock((_m)); 0; })
#define	mutex_unlock(_m)		sx_xunlock(&(_m)->sx)
#define	mutex_trylock(_m)		!!sx_try_xlock(&(_m)->sx)

#define DEFINE_MUTEX(lock)						\
	mutex_t lock;							\
	SX_SYSINIT_FLAGS(lock, &(lock).sx, "lnxmtx", SX_NOWITNESS)

static inline void
linux_mutex_init(mutex_t *m)
{

	memset(&m->sx, 0, sizeof(m->sx));
	sx_init_flags(&m->sx, "lnxmtx",  SX_NOWITNESS);
}

#define	mutex_init	linux_mutex_init

#endif	/* _LINUX_MUTEX_H_ */

#ifndef _LINUX_TIMER_H_
#define _LINUX_TIMER_H_

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/callout.h>

struct timer_list {
	struct callout	timer_callout;
	void		(*function)(unsigned long);
        unsigned long	data;
};

#define	expires	timer_callout.c_time

static inline void
_timer_fn(void *context)
{
	struct timer_list *timer;

	timer = context;
	timer->function(timer->data);
}

#define	setup_timer(timer, func, dat)					\
do {									\
	(timer)->function = (func);					\
	(timer)->data = (dat);						\
	callout_init(&(timer)->timer_callout, CALLOUT_MPSAFE);		\
} while (0)

#define	init_timer(timer)						\
do {									\
	(timer)->function = NULL;					\
	(timer)->data = 0;						\
	callout_init(&(timer)->timer_callout, CALLOUT_MPSAFE);		\
} while (0)

#define	mod_timer(timer, expire)					\
	callout_reset(&(timer)->timer_callout, (expire) - jiffies,	\
	    _timer_fn, (timer))

#define	add_timer(timer)						\
	callout_reset(&(timer)->timer_callout,				\
	    (timer)->timer_callout.c_time - jiffies, _timer_fn, (timer))

#define	del_timer(timer)	callout_stop(&(timer)->timer_callout)
#define	del_timer_sync(timer)	callout_drain(&(timer)->timer_callout)

#define	timer_pending(timer)	callout_pending(&(timer)->timer_callout)

static inline unsigned long
round_jiffies(unsigned long j)
{
	return roundup(j, hz);
}

#endif /* _LINUX_TIMER_H_ */

#ifndef	_LINUX_WORKQUEUE_H_
#define	_LINUX_WORKQUEUE_H_

#include <sys/taskqueue.h>

struct workqueue_struct {
	struct taskqueue	*taskqueue;
};

struct work_struct {
	struct	task 		work_task;
	struct	taskqueue	*taskqueue;
	void			(*fn)(struct work_struct *);
};

struct delayed_work {
	struct work_struct	work;
	struct callout		timer;
};

static inline struct delayed_work *
to_delayed_work(struct work_struct *work)
{

 	return container_of(work, struct delayed_work, work);
}


static inline void
_work_fn(void *context, int pending)
{
	struct work_struct *work;

	work = context;
	work->fn(work);
}

#define	INIT_WORK(work, func) 	 					\
do {									\
	(work)->fn = (func);						\
	(work)->taskqueue = NULL;					\
	TASK_INIT(&(work)->work_task, 0, _work_fn, (work));		\
} while (0)

#define	INIT_DELAYED_WORK(_work, func)					\
do {									\
	INIT_WORK(&(_work)->work, func);				\
	callout_init(&(_work)->timer, CALLOUT_MPSAFE);			\
} while (0)

#define	INIT_DELAYED_WORK_DEFERRABLE	INIT_DELAYED_WORK

#define	schedule_work(work)						\
do {									\
	(work)->taskqueue = taskqueue_thread;				\
	taskqueue_enqueue(taskqueue_thread, &(work)->work_task);	\
} while (0)

#define	flush_scheduled_work()	flush_taskqueue(taskqueue_thread)

#define	queue_work(q, work)						\
do {									\
	(work)->taskqueue = (q)->taskqueue;				\
	taskqueue_enqueue((q)->taskqueue, &(work)->work_task);		\
} while (0)

static inline void
_delayed_work_fn(void *arg)
{
	struct delayed_work *work;

	work = arg;
	taskqueue_enqueue(work->work.taskqueue, &work->work.work_task);
}

static inline int
queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *work,
    unsigned long delay)
{
	int pending;

	pending = work->work.work_task.ta_pending;
	work->work.taskqueue = wq->taskqueue;
	if (delay != 0)
		callout_reset(&work->timer, delay, _delayed_work_fn, work);
	else
		_delayed_work_fn((void *)work);

	return (!pending);
}

static inline struct workqueue_struct *
_create_workqueue_common(char *name, int cpus)
{
	struct workqueue_struct *wq;

	wq = kmalloc(sizeof(*wq), M_WAITOK);
	wq->taskqueue = taskqueue_create((name), M_WAITOK,
	    taskqueue_thread_enqueue,  &wq->taskqueue);
	taskqueue_start_threads(&wq->taskqueue, cpus, PWAIT, (name));

	return (wq);
}


#define	create_singlethread_workqueue(name)				\
	_create_workqueue_common(name, 1)

#define	create_workqueue(name)						\
	_create_workqueue_common(name, MAXCPU)

static inline void
destroy_workqueue(struct workqueue_struct *wq)
{
	taskqueue_free(wq->taskqueue);
	kfree(wq);
}

#define	flush_workqueue(wq)	flush_taskqueue((wq)->taskqueue)

static inline void
_flush_fn(void *context, int pending)
{
}

static inline void
flush_taskqueue(struct taskqueue *tq)
{
	struct task flushtask;

	PHOLD(curproc);
	TASK_INIT(&flushtask, 0, _flush_fn, NULL);
	taskqueue_enqueue(tq, &flushtask);
	taskqueue_drain(tq, &flushtask);
	PRELE(curproc);
}

static inline int
cancel_work_sync(struct work_struct *work)
{
	if (work->taskqueue &&
	    taskqueue_cancel(work->taskqueue, &work->work_task, NULL))
		taskqueue_drain(work->taskqueue, &work->work_task);
	return 0;
}

/*
 * This may leave work running on another CPU as it does on Linux.
 */
static inline int
cancel_delayed_work(struct delayed_work *work)
{

	callout_stop(&work->timer);
	if (work->work.taskqueue &&
	    taskqueue_cancel(work->work.taskqueue, &work->work.work_task, NULL))
		taskqueue_drain(work->work.taskqueue, &work->work.work_task);
	return 0;
}

#endif	/* _LINUX_WORKQUEUE_H_ */

#ifndef _LINUX_KREF_H_
#define _LINUX_KREF_H_

#include <sys/refcount.h>

struct kref {
        volatile u_int count;
};

static inline void
kref_init(struct kref *kref)
{

	refcount_init(&kref->count, 1);
}

static inline void
kref_get(struct kref *kref)
{

	refcount_acquire(&kref->count);
}

static inline int
kref_put(struct kref *kref, void (*rel)(struct kref *kref))
{

	if (refcount_release(&kref->count)) {
		rel(kref);
		return 1;
	}
	return 0;
}

#endif /* _KREF_H_ */

#ifndef	_LINUX_LOG2_H_
#define	_LINUX_LOG2_H_

#include <sys/libkern.h>

static inline unsigned long
roundup_pow_of_two(unsigned long x)
{
	return (1UL << flsl(x - 1));
}

static inline int
is_power_of_2(unsigned long n)
{
	return (n == roundup_pow_of_two(n));
}

static inline unsigned long
rounddown_pow_of_two(unsigned long x)
{
        return (1UL << (flsl(x) - 1));
}

static inline unsigned long
ilog2(unsigned long x)
{
	return (flsl(x) - 1);
}

#endif	/* _LINUX_LOG2_H_ */

#define EREMOTEIO ENXIO

#ifndef	_LINUX_ERR_H_
#define	_LINUX_ERR_H_

#define MAX_ERRNO	4095

#define IS_ERR_VALUE(x) ((x) >= (unsigned long)-MAX_ERRNO)

static inline void *
ERR_PTR(long error)
{
	return (void *)error;
}

static inline long
PTR_ERR(const void *ptr)
{
	return (long)ptr;
}

static inline long
IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

static inline void *
ERR_CAST(void *ptr)
{
	return (void *)ptr;
}

#endif	/* _LINUX_ERR_H_ */

// Not defined inside _LINUX_WORKQUEUE_H_ header
static inline int
work_pending(struct work_struct *work)
{
	return to_delayed_work(work)->work.work_task.ta_pending;
}
