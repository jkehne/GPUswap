/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef __PSCNV_CHAN_H__
#define __PSCNV_CHAN_H__

#include "pscnv_vm.h"
#include "pscnv_ramht.h"
#include "pscnv_engine.h"

/* XXX */
extern uint64_t nvc0_fifo_ctrl_offs(struct drm_device *dev, int cid);

/* pscnv_chan.flags */

/* this channel serves a special purpose within this driver */
#define PSCNV_CHAN_KERNEL 0x1 

enum pscnv_chan_state {
	PSCNV_CHAN_NEW=0,        /* still under construction */
	PSCNV_CHAN_INITIALIZED,  /* after construction */
	PSCNV_CHAN_RUNNING,      /* after call to chan_init_ib() or pscnv_chan_continue() */
	PSCNV_CHAN_PAUSING,      /* on call to pscnv pscnv_chan_pause() */
	PSCNV_CHAN_PAUSED,       /* after pscnv_chan_pause_wait() completed */
	PSCNV_CHAN_FAILED,       /* after call to pscnv_chan_fail() */
};

struct pscnv_chan {
	struct drm_device *dev;
	int cid;
	/* protected by ch_lock below, used for lookup */
	uint32_t handle;
	uint32_t flags;
	enum pscnv_chan_state state;
	spinlock_t state_lock;
	atomic_t pausing_threads;
	struct completion pause_completion;
	/* pointer to the vma that remaps the fifo-regs for this channel */
	struct vm_area_struct *vma;
	struct pscnv_vspace *vspace;
	struct list_head vspace_list;
	struct pscnv_bo *bo;
	spinlock_t instlock;
	int instpos;
	struct pscnv_ramht ramht;
	uint32_t ramfc;
	struct pscnv_bo *cache;
	struct drm_file *filp;
	struct kref ref;
	void *engdata[PSCNV_ENGINES_NUM];
};

struct pscnv_chan_engine {
	void (*takedown) (struct drm_device *dev);
	
	/* allocate a struct pscnv_chan or a "subclass" of it */
	struct pscnv_chan* (*do_chan_alloc) (struct drm_device *dev);
	
	/* engine specific initialization code */
	int (*do_chan_new) (struct pscnv_chan *ch);
	void (*do_chan_free) (struct pscnv_chan *ch);
	void (*pd_dump_chan) (struct drm_device *dev, struct seq_file *m, int chid);
	/* when done, set channel state to PAUSED and fire pause_completion */
	int (*do_chan_pause) (struct pscnv_chan *ch);
	/* when done, don't make any modification to the channel state */
	int (*do_chan_continue) (struct pscnv_chan *ch);
	struct pscnv_chan *fake_chans[4];
	struct pscnv_chan *chans[128];
	spinlock_t ch_lock;
	const struct vm_operations_struct *vm_ops;
	int ch_min, ch_max;
};

extern struct pscnv_chan *pscnv_chan_new(struct drm_device *dev, struct pscnv_vspace *, int fake);

extern void pscnv_chan_ref_free(struct kref *ref);

static inline void pscnv_chan_ref(struct pscnv_chan *ch) {
	kref_get(&ch->ref);
}

static inline void pscnv_chan_unref(struct pscnv_chan *ch) {
	kref_put(&ch->ref, pscnv_chan_ref_free);
}

static inline enum pscnv_chan_state
pscnv_chan_get_state(struct pscnv_chan *ch)
{
	unsigned long flags;
	enum pscnv_chan_state ret;
	
	spin_lock_irqsave(&ch->state_lock, flags);
	ret = ch->state;
	spin_unlock_irqrestore(&ch->state_lock, flags);
	
	return ret;
}

/* if in doubt, use the specialized functions below */
static inline void
pscnv_chan_set_state(struct pscnv_chan *ch, enum pscnv_chan_state st)
{
	unsigned long flags;
	
	spin_lock_irqsave(&ch->state_lock, flags);
	ch->state = st;
	spin_unlock_irqrestore(&ch->state_lock, flags);
}

void
pscnv_chan_fail(struct pscnv_chan *ch);

const char *
pscnv_chan_state_str(enum pscnv_chan_state st);

/* asynchronously pause a channel
 *
 * You still have to wait for the pause to complete.
 *
 * This function returns -EALREADY if the channel is already pausing or paused.
 *
 * In any case, the counter for pausing threads will be increased.
 *
 * be aware that a paused channel will not be removed from the fifo runqueue.
 * Instead it is available for command submission by the kernel at this point.*/
int
pscnv_chan_pause(struct pscnv_chan *ch);

/* wait for the pausing to complete
 *
 * this returns 0, even if the channel is already paused */
int
pscnv_chan_pause_wait(struct pscnv_chan *ch);

/* let the channel run again
 *
 * This decreases the number of pausing threads, and only if this thread is
 * the last one, who want's this channel to continue, it wall actually be
 * allowed to continue.
 *
 * Don't call this unless you called pscnv_chan_pause before! */
int
pscnv_chan_continue(struct pscnv_chan *ch);


extern int pscnv_chan_mmap(struct file *filp, struct vm_area_struct *vma);


/*
 * some interrupts return an 'inst' code. This is the page frame number in
 * vspace of the ch->bo which caused the fault.
 *
 * The 'inst' code may not be confused with the chid.
 *
 * This function searches for the channel with ch->bo starting at handle << 12 */
extern int pscnv_chan_handle_lookup(struct drm_device *dev, uint32_t handle);

/*
 * return the channel with given chid or NULL */
struct pscnv_chan *
pscnv_chan_chid_lookup(struct drm_device *dev, int chid);

int nv50_chan_init(struct drm_device *dev);
int nvc0_chan_init(struct drm_device *dev);

/* for vm_operations_struct */
void pscnv_chan_vm_open(struct vm_area_struct *vma);

void pscnv_chan_vm_close(struct vm_area_struct *vma);

#endif
