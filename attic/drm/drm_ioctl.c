/* BEGIN CSTYLED */

/**
 * \file drm_ioctl.c
 * IOCTL processing for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Jan  8 09:01:26 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
#include "drm_core.h"
#include "drm_io32.h"

/**
 * Get the bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_unique structure.
 * \return zero on success or a negative number on failure.
 *
 * Copies the bus id from drm_device::unique into user space.
 */
int drm_getunique(DRM_IOCTL_ARGS)
{
	struct drm_unique *u = data;
	struct drm_master *master = file_priv->master;

	if (u->unique_len >= master->unique_len) {
		if (DRM_COPY_TO_USER(u->unique, master->unique, master->unique_len))
			return -EFAULT;
	}
	u->unique_len = master->unique_len;

	return 0;
}

/*
 * Deprecated in DRM version 1.1, and will return EBUSY when setversion has
 * requested version 1.1 or greater.
 */
int drm_setunique(DRM_IOCTL_ARGS)
{
	return -EINVAL;
}

static int drm_set_busid(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_master *master = file_priv->master;
	int len;

	if (master->unique != NULL)
		return -EBUSY;

	master->unique_len = 40;
	master->unique_size = master->unique_len;
	master->unique = kmalloc(master->unique_size, GFP_KERNEL);
	if (master->unique == NULL)
		return -ENOMEM;

	len = snprintf(master->unique, master->unique_len, "pci:%04x:%02x:%02x.%1x",
	    dev->pdev->domain, dev->pdev->bus, dev->pdev->slot, dev->pdev->func);
	if (len >= master->unique_len)
		DRM_ERROR("buffer overflow");
	else
		master->unique_len = len;

	return 0;
}

/**
 * Get a mapping information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_map structure.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches for the mapping with the specified offset and copies its information
 * into userspace
 */
int drm_getmap(DRM_IOCTL_ARGS)
{
	struct drm_map *map = data;
	struct drm_map_list *r_list = NULL;
	struct list_head *list;
	int idx;
	int i;

	idx = (int)map->offset;

	mutex_lock(&dev->struct_mutex);
	if (idx < 0) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	i = 0;
	list_for_each(list, &dev->maplist) {
		if (i == idx) {
			r_list = list_entry(list, struct drm_map_list, head);
			break;
		}
		i++;
	}
	if (!r_list || !r_list->map) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	map->offset = r_list->map->offset;
	map->size = r_list->map->size;
	map->type = r_list->map->type;
	map->flags = r_list->map->flags;
	map->handle = (unsigned long long)(uintptr_t)r_list->user_token;
	map->mtrr = r_list->map->mtrr;
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/**
 * Get client information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_client structure.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches for the client with the specified index and copies its information
 * into userspace
 */
int drm_getclient(DRM_IOCTL_ARGS)
{
	struct drm_client *client = data;
	struct drm_file *pt;
	int idx;
	int i;

	idx = client->idx;
	mutex_lock(&dev->struct_mutex);

	i = 0;
	list_for_each_entry(pt, struct drm_file, &dev->filelist, lhead){
		if (i++ >= idx) {
			client->auth = pt->authenticated;
			client->pid = pt->pid;
			client->uid = pt->uid;
			client->magic = pt->magic;
			client->iocs = pt->ioctl_count;
			mutex_unlock(&dev->struct_mutex);

			return 0;
		}
	}
	mutex_unlock(&dev->struct_mutex);

	return -EINVAL;
}

/**
 * Get statistics information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_stats structure.
 *
 * \return zero on success or a negative number on failure.
 */
int drm_getstats(DRM_IOCTL_ARGS)
{
	struct drm_stats *stats = data;
	int i;

	memset(stats, 0, sizeof(*stats));

	mutex_lock(&dev->struct_mutex);

	for (i = 0; i < dev->counters; i++) {
		if (dev->types[i] == _DRM_STAT_LOCK)
			stats->data[i].value =
			    (file_priv->master->lock.hw_lock ? file_priv->master->lock.hw_lock->lock : 0);
		else
			stats->data[i].value = atomic_read(&dev->counts[i]);
		stats->data[i].type = dev->types[i];
	}

	stats->count = dev->counters;

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/**
 * Setversion ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Sets the requested interface version
 */
int drm_setversion(DRM_IOCTL_ARGS)
{
	struct drm_set_version *sv = data;
	int if_version, retcode = 0;

	if (sv->drm_di_major != -1) {
		if (sv->drm_di_major != DRM_IF_MAJOR ||
		    sv->drm_di_minor < 0 || sv->drm_di_minor > DRM_IF_MINOR) {
			retcode = -EINVAL;
			goto done;
		}
		if_version = DRM_IF_VERSION(sv->drm_di_major,
					    sv->drm_di_minor);
		dev->if_version = DRM_MAX(if_version, dev->if_version);
		if (sv->drm_di_minor >= 1) {
			/*
			 * Version 1.1 includes tying of DRM to specific device
			 */
			(void) drm_set_busid(dev, file_priv);
		}
	}

	if (sv->drm_dd_major != -1) {
		if (sv->drm_dd_major != dev->driver->major ||
		    sv->drm_dd_minor < 0 || sv->drm_dd_minor >
		    dev->driver->minor) {
			retcode = -EINVAL;
			goto done;
		}

		/* OSOL_drm: if (dev->driver->set_version)
			dev->driver->set_version(dev, sv); */
	}

done:
	sv->drm_di_major = DRM_IF_MAJOR;
	sv->drm_di_minor = DRM_IF_MINOR;
	sv->drm_dd_major = dev->driver->major;
	sv->drm_dd_minor = dev->driver->minor;

	return retcode;
}

/** No-op ioctl. */
int drm_noop(DRM_IOCTL_ARGS)
{
	DRM_DEBUG("\n");
	return 0;
}

