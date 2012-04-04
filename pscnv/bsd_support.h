// Note: Need to change /sys/conf/kmod.conf for FreeBSD
// set CSTD to gnu99 to enable support for nameless unions/structs

#include <sys/param.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/limits.h>
#include "drmP.h"
#undef container_of
#define _LINUX_BITOPS_H_
#define _LINUX_LIST_H_
#define _LINUX_WAIT_H_
#define _ASM_BYTEORDER_H_
#define _ASM_ATOMIC_H_
#define _LINUX_JIFFIES_H_

#define PSCNV_KAPI_GETPARAM_BUS_TYPE

typedef DRM_SPINTYPE spinlock_t;

#define spinlock_init(lock, name) DRM_SPININIT(lock, name)
#define spinlock_destroy(lock) DRM_SPINUNINIT(lock)
#define spin_lock(lock) DRM_SPINLOCK(lock)
#define spin_unlock(lock) DRM_SPINUNLOCK(lock)
#define spin_lock_irqsave(lock, flags) DRM_SPINLOCK_IRQSAVE(lock, flags)
#define spin_unlock_irqrestore(lock, flags) DRM_SPINLOCK_IRQRESTORE(lock, flags)

#define kfree(x) drm_free(x, 0, DRM_MEM_DRIVER)
#define kzalloc(x, y) drm_calloc(x, 1, DRM_MEM_DRIVER)
#define kmalloc(x, y) drm_alloc(x, DRM_MEM_DRIVER)

struct device_attribute {};
struct notifier_block {};
typedef struct pm_message_t { } pm_message_t;

#define pci_dev device

#define ioread8(x) (*(volatile u_int8_t*)(x))
#define iowrite8(y, x) ((*(volatile u_int8_t*)(x)) = (y))
#define ioread32(x) (*(volatile u_int32_t*)(x))
#define iowrite32(y, x) ((*(volatile u_int32_t*)(x)) = (y))

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
