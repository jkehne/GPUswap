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

#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_mem.h"
#include "pscnv_vm.h"
#include "pscnv_ramht.h"
#include "pscnv_chan.h"
#include "pscnv_fifo.h"
#include "pscnv_ioctl.h"
#include "pscnv_dma.h"

/*******************************************************************************
 * Channel state management
 ******************************************************************************/

const char *
pscnv_chan_state_str(enum pscnv_chan_state st)
{
	switch (st) {
		case PSCNV_CHAN_NEW:		return "NEW";
		case PSCNV_CHAN_INITIALIZED:	return "INITIALIZED";
		case PSCNV_CHAN_RUNNING:	return "RUNNING";
		case PSCNV_CHAN_PAUSING:	return "PAUSING";
		case PSCNV_CHAN_PAUSED:		return "PAUSED";
		case PSCNV_CHAN_FAILED:		return "FAILED";
	}
	
	return "UNKNOWN";
}

void
pscnv_chan_fail(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	
	if (pscnv_chan_get_state(ch) == PSCNV_CHAN_FAILED) {
		return; /* an error message has already been issued */
	}
	
	pscnv_chan_set_state(ch, PSCNV_CHAN_FAILED);
	
	NV_ERROR(dev, "channel %d FAILED\n", ch->cid);
}

int
pscnv_chan_pause(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	unsigned long flags;
	
	if (pscnv_pause_debug >= 2) {
		char comm[TASK_COMM_LEN];
		
		get_task_comm(comm, current);
		
		NV_INFO(dev, "%s (%d) requested channel pause for channel %d\n",
			comm, current->pid, ch->cid);
	}
	
	if (!dev_priv->chan->do_chan_pause) {
		NV_INFO(dev, "channel pausing not supported for this device\n");
		return -ENOSYS;
	}
	
	spin_lock_irqsave(&ch->state_lock, flags);
	if (ch->state == PSCNV_CHAN_PAUSING || ch->state == PSCNV_CHAN_PAUSED) {
		/* someone else was faster, good for us, nothing to do... */
		atomic_inc(&ch->pausing_threads);
		spin_unlock_irqrestore(&ch->state_lock, flags);
		return -EALREADY;
	}
	if (ch->state != PSCNV_CHAN_RUNNING) {
		spin_unlock_irqrestore(&ch->state_lock, flags);
		NV_ERROR(dev, "pscnv_chan_pause: channel %d is in unexpected "
			"state %s\n", ch->cid, pscnv_chan_state_str(ch->state));
		return -EINVAL;
	}
	
	atomic_inc(&ch->pausing_threads);
	init_completion(&ch->pause_completion);
	ch->state = PSCNV_CHAN_PAUSING;
	spin_unlock_irqrestore(&ch->state_lock, flags);
	
	return dev_priv->chan->do_chan_pause(ch);
}

int
pscnv_chan_pause_wait(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	enum pscnv_chan_state st;
	unsigned long flags;
	int res;
	
	if (!dev_priv->chan->do_chan_pause) {
		NV_INFO(dev, "channel pausing not supported for this device\n");
		return -ENOSYS;
	}
	
	spin_lock_irqsave(&ch->state_lock, flags);
	if (ch->state == PSCNV_CHAN_PAUSED) {
		/* oh. nothing to do... */
		spin_unlock_irqrestore(&ch->state_lock, flags);
		return 0;
	}
	if (ch->state != PSCNV_CHAN_PAUSING) {
		spin_unlock_irqrestore(&ch->state_lock, flags);
		NV_ERROR(dev, "pscnv_chan_pause_wait: channel %d is in unexpected "
			"state %s\n", ch->cid, pscnv_chan_state_str(ch->state));
		return -EINVAL;
	}
	spin_unlock_irqrestore(&ch->state_lock, flags);
	
	if (pscnv_pause_debug >= 2) {
		char comm[TASK_COMM_LEN];
		
		get_task_comm(comm, current);
		
		NV_INFO(dev, "%s (%d) is waiting for channel %d to pause\n",
			comm, current->pid, ch->cid);
	}
	
	res = wait_for_completion_interruptible_timeout(&ch->pause_completion, 5*HZ);
	
	if (res == -ERESTARTSYS) {
		NV_INFO(dev, "pscnv_chan_pause_wait: channel %d: interrupted\n",
				ch->cid);
		return -EINTR;
	}
	if (res == 0) {
		NV_INFO(dev, "pscnv_chan_pause_wait: channel %d: timeout\n",
				ch->cid);
		return -EBUSY;
	}
	
	st = pscnv_chan_get_state(ch); 
	if (st != PSCNV_CHAN_PAUSED) {
		NV_ERROR(dev, "pscnv_chan_pause_wait: channel %d: pause failed"
				" channel is in unexpected state %s\n",
				ch->cid, pscnv_chan_state_str(st));
		return -EFAULT;
	}
	
	return 0;
}

int
pscnv_chan_continue(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	unsigned long flags;
	int res = 0;
	
	if (!dev_priv->chan->do_chan_pause) {
		NV_INFO(dev, "channel pausing not supported for this device\n");
		return -ENOSYS;
	}
	
	spin_lock_irqsave(&ch->state_lock, flags);
	if (ch->state != PSCNV_CHAN_PAUSED) {
		spin_unlock_irqrestore(&ch->state_lock, flags);
		NV_ERROR(dev, "pscnv_chan_continue: channel %d is in unexpected "
			"state %s\n", ch->cid, pscnv_chan_state_str(ch->state));
		return -EINVAL;
	}
	
	if (atomic_dec_and_test(&ch->pausing_threads)) {
		/* ch->pausing_threads == 0 -> do the work */
		
		/* this should not take long */
		res = dev_priv->chan->do_chan_continue(ch);
		
		if (!res) {
			ch->state = PSCNV_CHAN_RUNNING;
		}
		
	}
	spin_unlock_irqrestore(&ch->state_lock, flags);
	
	if (res) {
		pscnv_chan_fail(ch);
	}
	
	return res;
}

/*******************************************************************************
 * Channel initialization and freeing
 ******************************************************************************/

static int pscnv_chan_bind (struct pscnv_chan *ch, int fake) {
	struct drm_nouveau_private *dev_priv = ch->dev->dev_private;
	unsigned long flags;
	int i;
	BUG_ON(ch->cid);
	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	switch(fake) {
	case PSCNV_DMA_CHAN:
		if (dev_priv->chan->chans[PSCNV_DMA_CHAN]) {
			NV_ERROR(ch->dev, "CHAN: Channel %d already allocated\n", fake);
			return -ENOSPC;
		}
		ch->cid = fake;
		dev_priv->chan->chans[fake] = ch;
		spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
		return 0;
	case 0:
		for (i = dev_priv->chan->ch_min; i <= dev_priv->chan->ch_max; i++) {
			if (!dev_priv->chan->chans[i]) {
				ch->cid = i;
				dev_priv->chan->chans[i] = ch;
				spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
				return 0;
			}
		}
		spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
		NV_ERROR(ch->dev, "CHAN: Out of channels\n");
		return -ENOSPC;
	default:
		ch->cid = -fake;
		BUG_ON(dev_priv->chan->fake_chans[fake]);
		dev_priv->chan->fake_chans[fake] = ch;
		spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
		return 0;
	}
}

static void pscnv_chan_unbind (struct pscnv_chan *ch) {
	struct drm_nouveau_private *dev_priv = ch->dev->dev_private;
	unsigned long flags;
	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	if (ch->cid < 0) {
		BUG_ON(dev_priv->chan->fake_chans[-ch->cid] != ch);
		dev_priv->chan->fake_chans[-ch->cid] = 0;
	} else {
		BUG_ON(dev_priv->chan->chans[ch->cid] != ch);
		dev_priv->chan->chans[ch->cid] = 0;
	}
	ch->cid = 0;
	spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
}

struct pscnv_chan *
pscnv_chan_new (struct drm_device *dev, struct pscnv_vspace *vs, int fake) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	struct pscnv_chan *res = kzalloc(sizeof *res, GFP_KERNEL);
	if (!res) {
		NV_ERROR(vs->dev, "CHAN: Couldn't alloc channel\n");
		return 0;
	}
	res->dev = dev;
	res->vspace = vs;
	res->handle = 0xffffffff;
	if (vs)
		pscnv_vspace_ref(vs);
	spin_lock_init(&res->instlock);
	spin_lock_init(&res->ramht.lock);
	kref_init(&res->ref);
	atomic_set(&res->pausing_threads, 0);
	
	if (fake) {
		res->flags |= PSCNV_CHAN_KERNEL;
	}
	
	if (pscnv_chan_bind(res, fake)) {
		if (vs)
			pscnv_vspace_unref(vs);
		kfree(res);
		return 0;
	}
	
	NV_INFO(vs->dev, "CHAN: Allocating channel %d in vspace %d\n", res->cid, vs->vid);
	if (dev_priv->chan->do_chan_new(res)) {
		if (vs)
			pscnv_vspace_unref(vs);
		pscnv_chan_unbind(res);
		kfree(res);
		return 0;
	}

	res->bo->chan = res;
	
	pscnv_chan_set_state(res, PSCNV_CHAN_INITIALIZED);
	
	return res;
}

void pscnv_chan_ref_free(struct kref *ref) {
	struct pscnv_chan *ch = container_of(ref, struct pscnv_chan, ref);
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_INFO(dev, "CHAN: Freeing channel %d\n", ch->cid);
	if (ch->cid >= 0) {
		int i;
		dev_priv->fifo->chan_kill(ch);
		for (i = 0; i < PSCNV_ENGINES_NUM; i++)
			if (ch->engdata[i]) {
				struct pscnv_engine *eng = dev_priv->engines[i];
				eng->chan_kill(eng, ch);
				eng->chan_free(eng, ch);
			}
	}
	dev_priv->chan->do_chan_free(ch);
	pscnv_chan_unbind(ch);
	if (ch->vspace)
		pscnv_vspace_unref(ch->vspace);
	kfree(ch);
}

/*******************************************************************************
 * Channel userspace support
 ******************************************************************************/

static void pscnv_chan_vm_open(struct vm_area_struct *vma) {
	struct pscnv_chan *ch = vma->vm_private_data;
	pscnv_chan_ref(ch);
}

static void pscnv_chan_vm_close(struct vm_area_struct *vma) {
	struct pscnv_chan *ch = vma->vm_private_data;
	pscnv_chan_unref(ch);
}

static struct vm_operations_struct pscnv_chan_vm_ops = {
	.open = pscnv_chan_vm_open,
	.close = pscnv_chan_vm_close,
};	

int pscnv_chan_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cid;
	struct pscnv_chan *ch;

	switch (dev_priv->card_type) {
	case NV_50:
		if ((vma->vm_pgoff * PAGE_SIZE & ~0x7f0000ull) == 0xc0000000) {
			if (vma->vm_end - vma->vm_start > 0x2000)
				return -EINVAL;
			cid = (vma->vm_pgoff * PAGE_SIZE >> 16) & 0x7f;
			ch = pscnv_get_chan(dev, filp->private_data, cid);
			if (!ch)
				return -ENOENT;

			vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
			vma->vm_ops = &pscnv_chan_vm_ops;
			vma->vm_private_data = ch;

			vma->vm_file = filp;

			return remap_pfn_range(vma, vma->vm_start, 
				(dev_priv->mmio_phys + 0xc00000 + cid * 0x2000) >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start, PAGE_SHARED);
		}
		break;
	case NV_D0:
	case NV_C0:
		if ((vma->vm_pgoff * PAGE_SIZE & ~0x7f0000ull) == 0xc0000000) {
			if (vma->vm_end - vma->vm_start > 0x1000)
				return -EINVAL;
			cid = (vma->vm_pgoff * PAGE_SIZE >> 16) & 0x7f;
			ch = pscnv_get_chan(dev, filp->private_data, cid);
			if (!ch)
				return -ENOENT;

			vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
			vma->vm_ops = &pscnv_chan_vm_ops;
			vma->vm_private_data = ch;
			vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

			vma->vm_file = filp;

			return remap_pfn_range(vma, vma->vm_start, 
					(dev_priv->fb_phys + nvc0_fifo_ctrl_offs(dev, ch->cid)) >> PAGE_SHIFT,
					vma->vm_end - vma->vm_start, PAGE_SHARED);
		}
	default:
		return -ENOSYS;
	}
	return -EINVAL;
}

int pscnv_chan_handle_lookup(struct drm_device *dev, uint32_t handle) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;
	struct pscnv_chan *res;
	int i;
	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	for (i = 0; i < 128; i++) {
		res = dev_priv->chan->chans[i];
		if (!res)
			continue;
		if (res->bo->start >> 12 != handle)
			continue;
		spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
		return i;
	}
	for (i = 0; i < 4; i++) {
		res = dev_priv->chan->fake_chans[i];
		if (!res)
			continue;
		if (res->handle != handle)
			continue;
		spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
		return -i;
	}
	spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
	return 128;
}

struct pscnv_chan *
pscnv_chan_chid_lookup(struct drm_device *dev, int chid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct pscnv_chan *ret = NULL;
	unsigned long flags;
	
	spin_lock_irqsave(&dev_priv->chan->ch_lock, flags);
	
	if (0 <= chid && chid <= 127) {
		ret = dev_priv->chan->chans[chid];
	}
	if (-3 <= chid && chid < 0) {
		ret = dev_priv->chan->fake_chans[-chid];
	}
	
	spin_unlock_irqrestore(&dev_priv->chan->ch_lock, flags);
	
	return ret;
}
