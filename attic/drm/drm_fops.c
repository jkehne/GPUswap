/* BEGIN CSTYLED */

/**
 * \file drm_fops.c
 * File operations for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009, Intel Corporation.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "drmP.h"

static int drm_open_helper(struct drm_minor *minor,
			int clone_id, int flags, cred_t *credp);

static int drm_setup(struct drm_device * dev)
{
	int i;
	int ret;
	static bool first_call = true;

	if (first_call) {
		/* OSOL_drm: moved from drm_fill_in_dev */
		if (drm_core_has_AGP(dev)) {
			if (drm_device_is_agp(dev))
				dev->agp = drm_agp_init(dev);
			if (drm_core_check_feature(dev, DRIVER_REQUIRE_AGP)
			    && (dev->agp == NULL)) {
				DRM_ERROR("Cannot initialize the agpgart module.\n");
				return -EINVAL;
			}
		}
	}

	if (dev->driver->firstopen) {
		ret = dev->driver->firstopen(dev);
		if (ret != 0)
			return ret;
	}

	if (first_call) {
		/* OSOL_drm: moved from drm_get_dev */
		/* setup the grouping for the legacy output */
		if (drm_core_check_feature(dev, DRIVER_MODESET)) {
			ret = drm_mode_group_init_legacy_group(dev, &dev->primary->mode_group);
			if (ret)
				return ret;
		}
	}

	atomic_set(&dev->ioctl_count, 0);

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA) &&
	    !drm_core_check_feature(dev, DRIVER_MODESET)) {
		dev->buf_use = 0;
		atomic_set(&dev->buf_alloc, 0);

		i = drm_dma_setup(dev);
		if (i < 0)
			return i;
	}

	for (i = 0; i < DRM_ARRAY_SIZE(dev->counts); i++)
		atomic_set(&dev->counts[i], 0);

	dev->context_flag = 0;
	dev->last_context = 0;
	dev->if_version = 0;


	DRM_DEBUG("\n");

	/*
	 * The kernel's context could be created here, but is now created
	 * in drm_dma_enqueue.  This is more resource-efficient for
	 * hardware that does not do DMA, but may mean that
	 * drm_select_queue fails between the time the interrupt is
	 * initialized and the time the queues are initialized.
	 */

	first_call = false;
	return 0;
}

/**
 * Open file.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches the DRM device with the same minor number, calls open_helper(), and
 * increments the device open count. If the open count was previous at zero,
 * i.e., it's the first that the device is open, then calls setup().
 */
int drm_open(struct drm_minor *minor, int clone_id, int flags, cred_t *credp)
{
	struct drm_device *dev = minor->dev;
	int retcode = 0;

	DRM_DEBUG("minor->index=%d, clone_id=%d", minor->index, clone_id);

	retcode = drm_open_helper(minor, clone_id, flags, credp);
	if (!retcode) {
		atomic_inc(&dev->counts[_DRM_STAT_OPENS]);
		spin_lock(&dev->count_lock);
		if (!dev->open_count++) {
			spin_unlock(&dev->count_lock);
			retcode = drm_setup(dev);
			goto out;
		}
		spin_unlock(&dev->count_lock);
	}
out:
	return retcode;
}

/**
 * Called whenever a process opens /dev/drm.
 *
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
static int drm_open_helper(struct drm_minor *minor,
			int clone_id, int flags, cred_t *credp)
{
	struct drm_device *dev = minor->dev;
	struct drm_file *priv;
	int minor_id = minor->index;
	int ret;

	if (flags & FEXCL)
		return -EBUSY;	/* No exclusive opens */

	DRM_DEBUG("pid = %d, minor = %d\n", ddi_get_pid(), minor_id);

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	memset(priv, 0, sizeof(*priv));
	idr_replace(&minor->clone_idr, priv, clone_id);  /* OSOL_drm */
	priv->uid = crgetsuid(credp);
	priv->pid = ddi_get_pid();
	priv->minor = minor;
	priv->ioctl_count = 0;
	/* for compatibility root is always authenticated */
	priv->authenticated = DRM_SUSER(credp);
	priv->lock_count = 0;

	INIT_LIST_HEAD(&priv->lhead);
	INIT_LIST_HEAD(&priv->fbs);

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_open(dev, priv);

	if (dev->driver->open) {
		ret = dev->driver->open(dev, priv);
		if (ret < 0)
			goto out_free;
	}


	/* if there is no current master make this fd it */
	mutex_lock(&dev->struct_mutex);
	if (!priv->minor->master) {
		/* create a new master */
		priv->minor->master = drm_master_create(priv->minor);

		priv->is_master = 1;
		/* take another reference for the copy in the local file priv */
		priv->master = drm_master_get(priv->minor->master);

		priv->authenticated = 1;

		mutex_unlock(&dev->struct_mutex);
		if (dev->driver->master_create) {
			ret = dev->driver->master_create(dev, priv->master);
			if (ret) {
				mutex_lock(&dev->struct_mutex);
				/* drop both references if this fails */
				drm_master_put(&priv->minor->master);
				drm_master_put(&priv->master);
				mutex_unlock(&dev->struct_mutex);
				goto out_free;
			}
		}
	} else {
		/* get a reference to the master */
		priv->master = drm_master_get(priv->minor->master);
		mutex_unlock(&dev->struct_mutex);
	}

	mutex_lock(&dev->struct_mutex);
	list_add(&priv->lhead, &dev->filelist, (caddr_t)priv);
	mutex_unlock(&dev->struct_mutex);

	return 0;
out_free:
	kfree(priv, sizeof (*priv));
	return ret;
}

void drm_master_release(struct drm_device *dev, struct drm_file *fpriv)
{
	struct drm_master *master = fpriv->master;

	if (drm_i_have_hw_lock(dev, fpriv)) {
		DRM_DEBUG("Process %d dead, freeing lock for context %d",
		    DRM_CURRENTPID,  _DRM_LOCKING_CONTEXT(master->lock.hw_lock->lock));
		if (dev->driver->reclaim_buffers_locked != NULL)
			dev->driver->reclaim_buffers_locked(dev, fpriv);
		(void) drm_lock_free(&master->lock,
		    _DRM_LOCKING_CONTEXT(master->lock.hw_lock->lock));
	} else if (dev->driver->reclaim_buffers_locked != NULL &&
	    master->lock.hw_lock != NULL) {
		DRM_ERROR("drm_close: "
		    "retake lock not implemented yet");
	}

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA)) {
		drm_reclaim_buffers(dev, fpriv);
	}

}

/**
 * Release file.
 *
 * \return zero on success or a negative number on failure.
 *
 * If the hardware lock is held then free it, and take it again for the kernel
 * context since it's necessary to reclaim buffers. Unlink the file private
 * data from its list and free it. Decreases the open count and if it reaches
 * zero calls drm_lastclose().
 */
int
drm_release(struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	int retcode = 0;



	DRM_DEBUG("open_count = %d\n", dev->open_count);

	if (dev->driver->preclose)
		dev->driver->preclose(dev, file_priv);

	/* ========================================================
	 * Begin inline drm_release
	 */

	/* if the master has gone away we can't do anything with the lock */
	if (file_priv->minor->master)
		drm_master_release(dev, file_priv);

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_release(dev, file_priv);

	mutex_lock(&dev->ctxlist_mutex);
	if (!list_empty(&dev->ctxlist)) {
		struct drm_ctx_list *pos, *n;

		list_for_each_entry_safe(pos, n, struct drm_ctx_list, &dev->ctxlist, head) {
			if (pos->tag == file_priv &&
			    pos->handle != DRM_KERNEL_CONTEXT) {
				if (dev->driver->context_dtor)
					dev->driver->context_dtor(dev,
								  pos->handle);

				drm_ctxbitmap_free(dev, pos->handle);

				list_del(&pos->head);
				kfree(pos, sizeof (*pos));
				--dev->ctx_count;
			}
		}
	}
	mutex_unlock(&dev->ctxlist_mutex);

	mutex_lock(&dev->struct_mutex);

	if (file_priv->is_master) {
		struct drm_master *master = file_priv->master;
		struct drm_file *temp;
		list_for_each_entry(temp, struct drm_file, &dev->filelist, lhead) {
			if ((temp->master == file_priv->master) &&
			    (temp != file_priv))
				temp->authenticated = 0;
		}

		/**
		 * Since the master is disappearing, so is the
		 * possibility to lock.
		 */

		if (master->lock.hw_lock) {
			master->lock.hw_lock = NULL;
			master->lock.file_priv = NULL;
		}

		if (file_priv->minor->master == file_priv->master) {
			/* drop the reference held my the minor */
			drm_master_put(&file_priv->minor->master);
		}
	}

	/* drop the reference held my the file priv */
	drm_master_put(&file_priv->master);
	file_priv->is_master = 0;
	list_del(&file_priv->lhead);
	mutex_unlock(&dev->struct_mutex);

	if (dev->driver->postclose)
		dev->driver->postclose(dev, file_priv);
	kfree(file_priv, sizeof (*file_priv));

	/* ========================================================
	 * End inline drm_release
	 */

	atomic_inc(&dev->counts[_DRM_STAT_CLOSES]);
	spin_lock(&dev->count_lock);
	if (!--dev->open_count) {
		if (atomic_read(&dev->ioctl_count)) {
			DRM_ERROR("Device busy: %d\n",
				  atomic_read(&dev->ioctl_count));
			spin_unlock(&dev->count_lock);
			return -EBUSY;
		}
		spin_unlock(&dev->count_lock);
		return drm_lastclose(dev);
	}
	spin_unlock(&dev->count_lock);



	return retcode;
}
